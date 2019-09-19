#include "rprApi.h"
#include "rprcpp/rprFramebufferGL.h"
#include "rprcpp/rprContext.h"
#include "rifcpp/rifFilter.h"
#include "rifcpp/rifImage.h"

#include "RadeonProRender.h"
#include "RadeonProRender_CL.h"
#include "RadeonProRender_GL.h"

#include "config.h"
#include "material.h"
#include "materialFactory.h"
#include "materialAdapter.h"

#include <vector>
#include <mutex>

#include <pxr/imaging/pxOsd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdRprAovTokens, HD_RPR_AOV_TOKENS);

namespace
{

using RecursiveLockGuard = std::lock_guard<std::recursive_mutex>;

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&& ... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

} // namespace anonymous

static const std::map<TfToken, rpr_aov> kAovTokenToRprAov = {
    {HdRprAovTokens->color, RPR_AOV_COLOR},
    {HdRprAovTokens->albedo, RPR_AOV_DIFFUSE_ALBEDO},
    {HdRprAovTokens->depth, RPR_AOV_DEPTH},
    {HdRprAovTokens->linearDepth, RPR_AOV_DEPTH},
    {HdRprAovTokens->primId, RPR_AOV_OBJECT_ID},
    {HdRprAovTokens->normal, RPR_AOV_SHADING_NORMAL},
    {HdRprAovTokens->worldCoordinate, RPR_AOV_WORLD_COORDINATE},
    {HdRprAovTokens->primvarsSt, RPR_AOV_UV},
};

class HdRprApiImpl {
public:
    HdRprApiImpl() {
        InitRpr();
        InitRif();
        InitMaterialSystem();
        CreateScene();
        CreatePosteffects();
        CreateCamera();
    }

    ~HdRprApiImpl() {
        for (auto material : m_materialsToRelease) {
            DeleteMaterial(material);
        }
        m_materialsToRelease.clear();

        DisableAovs();

        for (auto rprObject : m_rprObjectsToRelease) {
            if (rprObject) {
                rprObjectDelete(rprObject);
            }
        }
    }

    void CreateScene() {
        if (!m_rprContext) {
            return;
        }

        if (RPR_ERROR_CHECK(rprContextCreateScene(m_rprContext->GetHandle(), &m_scene), "Fail to create scene")) return;
        m_rprObjectsToRelease.push_back(m_scene);
        if (RPR_ERROR_CHECK(rprContextSetScene(m_rprContext->GetHandle(), m_scene), "Fail to set scene")) return;
    }

    void CreateCamera() {
        if (!m_rprContext) {
            return;
        }

        RPR_ERROR_CHECK(rprContextCreateCamera(m_rprContext->GetHandle(), &m_camera), "Fail to create camera");
        m_rprObjectsToRelease.push_back(m_camera);
        RPR_ERROR_CHECK(rprCameraLookAt(m_camera, 20.0f, 60.0f, 40.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f), "Fail to set camera Look At");

        const rpr_float  sensorSize[] = { 1.f , 1.f};
        RPR_ERROR_CHECK(rprCameraSetSensorSize(m_camera, sensorSize[0], sensorSize[1]), "Fail to to set camera sensor size");
        RPR_ERROR_CHECK(rprSceneSetCamera(m_scene, m_camera), "Fail to to set camera to scene");

        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    void* CreateMesh(const VtVec3fArray & points, const VtIntArray & pointIndexes, const VtVec3fArray & normals, const VtIntArray & normalIndexes, const VtVec2fArray & uv, const VtIntArray & uvIndexes, const VtIntArray & vpf, rpr_material_node material = nullptr) {
        if (!m_rprContext) {
            return nullptr;
        }

        rpr_shape mesh = nullptr;

        VtIntArray newIndexes, newVpf;
        SplitPolygons(pointIndexes, vpf, newIndexes, newVpf);

        VtIntArray newUvIndexes;
        if (!uvIndexes.empty()) {
            SplitPolygons(uvIndexes, vpf, newUvIndexes);
        }

        VtIntArray newNormalIndexes;
        if (!normalIndexes.empty()) {
            SplitPolygons(normalIndexes, vpf, newNormalIndexes);
        }

        RecursiveLockGuard rprLock(m_rprAccessMutex);

        if (RPR_ERROR_CHECK(rprContextCreateMesh(m_rprContext->GetHandle(),
            (rpr_float const*)points.data(), points.size(), sizeof(GfVec3f),
            (rpr_float const*)((normals.size() == 0) ? 0 : normals.data()), normals.size(), sizeof(GfVec3f),
            (rpr_float const*)((uv.size() == 0) ? 0 : uv.data()), uv.size(), sizeof(GfVec2f),
            (rpr_int const*)newIndexes.data(), sizeof(rpr_int),
            (rpr_int const*)(!newNormalIndexes.empty() ? newNormalIndexes.data() : newIndexes.data()), sizeof(rpr_int),
            (rpr_int const*)(!newUvIndexes.empty() ? newUvIndexes.data() : newIndexes.data()), sizeof(rpr_int),
            newVpf.data(), newVpf.size(), &mesh)
            , "Fail create mesh")) {
            return nullptr;
        }

        if (RPR_ERROR_CHECK(rprSceneAttachShape(m_scene, mesh), "Fail attach mesh to scene")) {
            rprObjectDelete(mesh);
            return nullptr;
        }

        if (material) {
            rprShapeSetMaterial(mesh, material);
        }

        m_dirtyFlags |= ChangeTracker::DirtyScene;
        return mesh;
    }

    void SetMeshTransform(rpr_shape mesh, const GfMatrix4f& transform) {
        RecursiveLockGuard rprLock(m_rprAccessMutex);
        if (!RPR_ERROR_CHECK(rprShapeSetTransform(mesh, false, transform.GetArray()), "Fail set mesh transformation")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void SetMeshRefineLevel(rpr_shape mesh, const int level, const TfToken boundaryInterpolation) {
        if (!m_rprContext) {
            return;
        }

        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            // Not supported
            return;
        }

        RecursiveLockGuard rprLock(m_rprAccessMutex);

        if (RPR_ERROR_CHECK(rprShapeSetSubdivisionFactor(mesh, level), "Fail set mesh subdividion")) return;
        m_dirtyFlags |= ChangeTracker::DirtyScene;

        if (level > 0) {
            rpr_subdiv_boundary_interfop_type interfopType = boundaryInterpolation == PxOsdOpenSubdivTokens->edgeAndCorner ?
                RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_AND_CORNER :
                RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_ONLY;
            if (RPR_ERROR_CHECK(rprShapeSetSubdivisionBoundaryInterop(mesh, interfopType), "Fail set mesh subdividion boundary")) return;
        }
    }

    void SetMeshMaterial(rpr_shape mesh, const RprApiMaterial* material) {
        RecursiveLockGuard rprLock(m_rprAccessMutex);
        m_rprMaterialFactory->AttachMaterialToShape(mesh, material);
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    void SetMeshHeteroVolume(rpr_shape mesh, const RprApiObject heteroVolume) {
        RecursiveLockGuard rprLock(m_rprAccessMutex);
        if (!RPR_ERROR_CHECK(rprShapeSetHeteroVolume(mesh, heteroVolume), "Fail set mesh hetero volume")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void SetCurveMaterial(rpr_shape curve, const RprApiMaterial* material) {
        RecursiveLockGuard rprLock(m_rprAccessMutex);
        m_rprMaterialFactory->AttachCurveToShape(curve, material);
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    void* CreateMeshInstance(rpr_shape mesh) {
        if (!m_rprContext) {
            return nullptr;
        }

        RecursiveLockGuard rprLock(m_rprAccessMutex);

        rpr_shape meshInstance;
        if (RPR_ERROR_CHECK(rprContextCreateInstance(m_rprContext->GetHandle(), mesh, &meshInstance), "Fail to create mesh instance")) {
            return nullptr;
        }

        if (RPR_ERROR_CHECK(rprSceneAttachShape(m_scene, meshInstance), "Fail to attach mesh instance")) {
            rprObjectDelete(meshInstance);
            return nullptr;
        }

        m_dirtyFlags |= ChangeTracker::DirtyScene;

        return meshInstance;
    }

    void SetMeshVisibility(rpr_shape mesh, bool isVisible) {
        RecursiveLockGuard rprLock(m_rprAccessMutex);

        if (!RPR_ERROR_CHECK(rprShapeSetVisibility(mesh, isVisible), "Fail to set mesh visibility")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void* CreateCurve(const VtVec3fArray& points, const VtIntArray& indexes, const float& width) {
        if (!m_rprContext || points.empty() || indexes.empty()) {
            return nullptr;
        }

        const size_t k_segmentSize = 4;

        rpr_curve curve = 0;

        VtVec3fArray newPoints = points;
        VtIntArray newIndexes = indexes;

        if (size_t extraPoints = newPoints.size() % k_segmentSize) {
            newPoints.resize(points.size() + k_segmentSize - extraPoints);
            newIndexes.resize(indexes.size() + k_segmentSize - extraPoints);

            for (int i = 0; i < k_segmentSize; ++i) {
                newPoints[newPoints.size() - i - 1] = points[points.size() - i - 1];
                newIndexes[newIndexes.size() - i - 1] = indexes[indexes.size() - i - 1];
            }
        }

        const size_t segmentPerCurve = newPoints.size() / k_segmentSize;
        std::vector<float> curveWidths(points.size(), width);
        std::vector<int> segmentsPerCurve(points.size(), segmentPerCurve);

        RecursiveLockGuard rprLock(m_rprAccessMutex);

        if (RPR_ERROR_CHECK(rprContextCreateCurve(m_rprContext->GetHandle(),
            &curve
            , newPoints.size()
            , (float*)newPoints.data()
            , sizeof(GfVec3f)
            , newIndexes.size()
            , 1
            , (const rpr_uint*)newIndexes.data()
            , &width, NULL
            , segmentsPerCurve.data()), "Fail to create curve")) {
            return nullptr;
        }

        if (RPR_ERROR_CHECK(rprSceneAttachCurve(m_scene, curve), "Fail to attach curve")) {
            rprObjectDelete(curve);
            return nullptr;
        }

        m_dirtyFlags |= ChangeTracker::DirtyScene;
        return curve;
    }

    void CreateEnvironmentLight(const std::string& path, float intensity) {
        if (!m_rprContext || path.empty()) {
            return;
        }

        rpr_light light;
        rpr_image image = nullptr;

        RecursiveLockGuard rprLock(m_rprAccessMutex);
        if (RPR_ERROR_CHECK(rprContextCreateImageFromFile(m_rprContext->GetHandle(), path.c_str(), &image), std::string("Fail to load image ") + path)) return;
        m_rprObjectsToRelease.push_back(image);
        if (RPR_ERROR_CHECK(rprContextCreateEnvironmentLight(m_rprContext->GetHandle(), &light), "Fail to create environment light")) return;
        m_rprObjectsToRelease.push_back(light);
        if (RPR_ERROR_CHECK(rprEnvironmentLightSetImage(light, image), "Fail to set image to environment light")) return;
        if (RPR_ERROR_CHECK(rprEnvironmentLightSetIntensityScale(light, intensity), "Fail to set environment light intencity")) return;
        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            if (RPR_ERROR_CHECK(rprSceneSetEnvironmentLight(m_scene, light), "Fail to set environment light")) return;
        } else {
            if (RPR_ERROR_CHECK(rprSceneAttachLight(m_scene, light), "Fail to attach environment light to scene")) return;
        }

        m_isLightPresent = true;
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    void CreateEnvironmentLight(GfVec3f color, float intensity) {
        if (!m_rprContext) {
            return;
        }

        rpr_image image = nullptr;

        // Add an environment light to the scene with the image attached.
        rpr_light light;

        // Set the background image to a solid color.
        std::array<float, 3> backgroundColor = { color[0],  color[1],  color[2] };
        rpr_image_format format = { 3, RPR_COMPONENT_TYPE_FLOAT32 };
        rpr_uint imageSize = m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID ? 64 : 1;
        rpr_image_desc desc = { imageSize, imageSize, 0, static_cast<rpr_uint>(imageSize * imageSize * 3 * sizeof(float)), 0 };
        std::vector<std::array<float, 3>> imageData(imageSize * imageSize, backgroundColor);

        if (RPR_ERROR_CHECK(rprContextCreateImage(m_rprContext->GetHandle(), format, &desc, imageData.data(), &image), "Fail to create image from color")) return;
        m_rprObjectsToRelease.push_back(image);
        if (RPR_ERROR_CHECK(rprContextCreateEnvironmentLight(m_rprContext->GetHandle(), &light), "Fail to create environment light")) return;
        m_rprObjectsToRelease.push_back(light);
        if (RPR_ERROR_CHECK(rprEnvironmentLightSetImage(light, image), "Fail to set image to environment light")) return;
        if (RPR_ERROR_CHECK(rprEnvironmentLightSetIntensityScale(light, intensity), "Fail to set environment light intensity")) return;
        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            if (RPR_ERROR_CHECK(rprSceneSetEnvironmentLight(m_scene, light), "Fail to set environment light")) return;
        } else {
            if (RPR_ERROR_CHECK(rprSceneAttachLight(m_scene, light), "Fail to attach environment light to scene")) return;
        }
        m_isLightPresent = true;
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    void* CreateRectLightGeometry(const float& width, const float& height) {
        constexpr const size_t rectVertexCount = 4;
        VtVec3fArray positions(rectVertexCount);
        positions[0] = GfVec3f(width * 0.5f, height * 0.5f, 0.f);
        positions[1] = GfVec3f(width * 0.5f, height * -0.5f, 0.f);
        positions[2] = GfVec3f(width * -0.5f, height * -0.5f, 0.f);
        positions[3] = GfVec3f(width * -0.5f, height * 0.5f, 0.f);

        // All normals -z
        VtVec3fArray normals(rectVertexCount, GfVec3f(0.f, 0.f, -1.f));

        VtIntArray idx(rectVertexCount);
        idx[0] = 0;
        idx[1] = 1;
        idx[2] = 2;
        idx[3] = 3;

        VtIntArray vpf(1, rectVertexCount);

        VtVec2fArray uv; // empty

        m_isLightPresent = true;

        return CreateMesh(positions, idx, normals, VtIntArray(), uv, VtIntArray(), vpf);
    }

    void* CreateDiskLight(const float& width, const float& height, const GfVec3f& color) {
        VtVec3fArray positions;
        VtVec3fArray normals;
        VtVec2fArray uv; // empty
        VtIntArray idx;
        VtIntArray vpf;

        const uint32_t k_diskVertexCount = 32;
        const float step = M_PI * 2 / k_diskVertexCount;
        for (int i = 0; i < k_diskVertexCount; ++i) {
            positions.push_back(GfVec3f(width * sin(step * i), height * cos(step * i), 0.f));
            positions.push_back(GfVec3f(width * sin(step * (i + 1)), height * cos(step * (i + 1)), 0.f));
            positions.push_back(GfVec3f(0., 0., 0.f));

            normals.push_back(GfVec3f(0.f, 0.f, -1.f));
            normals.push_back(GfVec3f(0.f, 0.f, -1.f));
            normals.push_back(GfVec3f(0.f, 0.f, -1.f));

            idx.push_back(i * 3);
            idx.push_back(i * 3 + 1);
            idx.push_back(i * 3 + 2);

            vpf.push_back(3);
        }

        rpr_material_node material = NULL;
        {
            RecursiveLockGuard rprLock(m_rprAccessMutex);

            if (RPR_ERROR_CHECK(rprMaterialSystemCreateNode(m_matsys, RPR_MATERIAL_NODE_EMISSIVE, &material), "Fail create emmisive material")) return nullptr;
            m_rprObjectsToRelease.push_back(material);
            if (RPR_ERROR_CHECK(rprMaterialNodeSetInputF(material, "color", color[0], color[1], color[2], 0.0f), "Fail set material color")) return nullptr;

            m_isLightPresent = true;
        }

        return CreateMesh(positions, idx, normals, VtIntArray(), uv, VtIntArray(), vpf, material);
    }

    void* CreateSphereLightGeometry(const float& radius) {
        VtVec3fArray positions;
        VtVec3fArray normals;
        VtVec2fArray uv;
        VtIntArray idx;
        VtIntArray vpf;

        constexpr int nx = 16, ny = 16;

        const float d = radius;

        for (int j = ny - 1; j >= 0; j--) {
            for (int i = 0; i < nx; i++) {
                float t = i / (float)nx * M_PI;
                float p = j / (float)ny * 2.f * M_PI;
                positions.push_back(d * GfVec3f(sin(t) * cos(p), cos(t), sin(t) * sin(p)));
                normals.push_back(GfVec3f(sin(t) * cos(p), cos(t), sin(t) * sin(p)));
            }
        }

        for (int j = 0; j < ny; j++) {
            for (int i = 0; i < nx - 1; i++) {
                int o0 = j * nx;
                int o1 = ((j + 1) % ny) * nx;
                idx.push_back(o0 + i);
                idx.push_back(o0 + i + 1);
                idx.push_back(o1 + i + 1);
                idx.push_back(o1 + i);
                vpf.push_back(4);
            }
        }

        m_isLightPresent = true;

        return CreateMesh(positions, idx, normals, VtIntArray(), uv, VtIntArray(), vpf);
    }

    RprApiMaterial* CreateMaterial(const MaterialAdapter& materialAdapter) {
        if (!m_rprContext) {
            return nullptr;
        }

        RprApiMaterial* material = nullptr;
        if (m_rprMaterialFactory) {
            RecursiveLockGuard rprLock(m_rprAccessMutex);
            material = m_rprMaterialFactory->CreateMaterial(materialAdapter.GetType(), materialAdapter);
        }

        return material;
    }

    void DeleteMaterial(RprApiMaterial* material) {
        RecursiveLockGuard rprLock(m_rprAccessMutex);
        m_rprMaterialFactory->DeleteMaterial(material);
    }

    void* CreateHeterVolume(const VtArray<float>& gridDencityData, const VtArray<size_t>& indexesDencity, const VtArray<float>& gridAlbedoData, const VtArray<unsigned int>& indexesAlbedo, const GfVec3i& grigSize) {
        if (!m_rprContext) {
            return nullptr;
        }

        rpr_hetero_volume heteroVolume = nullptr;
        rpr_grid rprGridDencity;
        if (RPR_ERROR_CHECK(rprContextCreateGrid(m_rprContext->GetHandle(), &rprGridDencity
            , grigSize[0], grigSize[1], grigSize[2], &indexesDencity[0]
            , indexesDencity.size(), RPR_GRID_INDICES_TOPOLOGY_I_U64
            , &gridDencityData[0], gridDencityData.size() * sizeof(gridDencityData[0])
            , 0)
            , "Fail create dencity grid")) return nullptr;
        m_rprObjectsToRelease.push_back(rprGridDencity);

        rpr_grid rprGridAlbedo;
        if (RPR_ERROR_CHECK(rprContextCreateGrid(m_rprContext->GetHandle(), &rprGridAlbedo
            , grigSize[0], grigSize[1], grigSize[2], &indexesAlbedo[0]
            , indexesAlbedo.size() / 3, RPR_GRID_INDICES_TOPOLOGY_XYZ_U32
            , &gridAlbedoData[0], gridAlbedoData.size() * sizeof(gridAlbedoData[0])
            , 0)
            , "Fail create albedo grid")) return nullptr;
        m_rprObjectsToRelease.push_back(rprGridAlbedo);

        if (RPR_ERROR_CHECK(rprContextCreateHeteroVolume(m_rprContext->GetHandle(), &heteroVolume), "Fail create hetero dencity volume")) return nullptr;
        if (RPR_ERROR_CHECK(rprHeteroVolumeSetDensityGrid(heteroVolume, rprGridDencity), "Fail to set density hetero volume")) return nullptr;
        if (RPR_ERROR_CHECK(rprHeteroVolumeSetAlbedoGrid(heteroVolume, rprGridAlbedo), "Fail to set albedo hetero volume")) return nullptr;
        if (RPR_ERROR_CHECK(rprSceneAttachHeteroVolume(m_scene, heteroVolume), "Fail attach hetero volume to scene")) return nullptr;

        return heteroVolume;
    }

    void SetHeteroVolumeTransform(RprApiObject heteroVolume, const GfMatrix4f& m) {
        RPR_ERROR_CHECK(rprHeteroVolumeSetTransform(heteroVolume, false, m.GetArray()), "Fail to set hetero volume transform");
    }

    void* CreateVolume(const VtArray<float>& gridDencityData, const VtArray<size_t>& indexesDencity, const VtArray<float>& gridAlbedoData, const VtArray<unsigned int>& indexesAlbedo, const GfVec3i& grigSize, const GfVec3f& voxelSize, RprApiObject out_mesh, RprApiObject out_heteroVolume) {
        RecursiveLockGuard rprLock(m_rprAccessMutex);

        RprApiObject heteroVolume = CreateHeterVolume(gridDencityData, indexesDencity, gridAlbedoData, indexesAlbedo, grigSize);
        if (!heteroVolume) {
            return nullptr;
        }

        RprApiObject cubeMesh = CreateCubeMesh(0.5f, 0.5f, 0.5f);
        if (!cubeMesh) {
            // TODO: properly release created volume
            return nullptr;
        }

        MaterialAdapter matAdapter = MaterialAdapter(EMaterialType::TRANSPERENT,
            MaterialParams{ { TfToken("color"), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f))
        } }); // TODO: use token

        RprApiMaterial* transperantMaterial = CreateMaterial(matAdapter);
        if (!transperantMaterial) {
            // TODO: properly release created volume and mesh
            return nullptr;
        }
        m_materialsToRelease.push_back(transperantMaterial);

        GfMatrix4f meshTransform;
        GfVec3f volumeSize = GfVec3f(voxelSize[0] * grigSize[0], voxelSize[1] * grigSize[1], voxelSize[2] * grigSize[2]);
        meshTransform.SetScale(volumeSize);

        SetMeshMaterial(cubeMesh, transperantMaterial);
        SetMeshHeteroVolume(cubeMesh, heteroVolume);
        SetMeshTransform(cubeMesh, meshTransform);
        SetHeteroVolumeTransform(heteroVolume, meshTransform);

        out_mesh = cubeMesh;
        out_heteroVolume = heteroVolume;

        return heteroVolume;
    }

    void CreatePosteffects() {
        if (!m_rprContext) {
            return;
        }

        if (m_rprContext->GetActivePluginType() == rpr::PluginType::TAHOE) {
            if (!RPR_ERROR_CHECK(rprContextCreatePostEffect(m_rprContext->GetHandle(), RPR_POST_EFFECT_TONE_MAP, &m_tonemap), "Fail to create post effect")) {
                m_rprObjectsToRelease.push_back(m_tonemap);
                RPR_ERROR_CHECK(rprContextAttachPostEffect(m_rprContext->GetHandle(), m_tonemap), "Fail to attach posteffect");
            }
        }
    }

    void SetCameraViewMatrix(const GfMatrix4d& m) {
        if (!m_camera) return;

        const GfMatrix4d& iwvm = m.GetInverse();
        const GfMatrix4d& wvm = m;

        GfVec3f eye(iwvm[3][0], iwvm[3][1], iwvm[3][2]);
        GfVec3f up(wvm[0][1], wvm[1][1], wvm[2][1]);
        GfVec3f n(wvm[0][2], wvm[1][2], wvm[2][2]);
        GfVec3f at(eye - n);

        RecursiveLockGuard rprLock(m_rprAccessMutex);
        RPR_ERROR_CHECK(rprCameraLookAt(m_camera, eye[0], eye[1], eye[2], at[0], at[1], at[2], up[0], up[1], up[2]), "Fail to set camera Look At");

        m_cameraViewMatrix = m;
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    void SetCameraProjectionMatrix(const GfMatrix4d& proj) {
        if (!m_camera) return;

        RecursiveLockGuard rprLock(m_rprAccessMutex);

        float sensorSize[2];

        if (RPR_ERROR_CHECK(rprCameraGetInfo(m_camera, RPR_CAMERA_SENSOR_SIZE, sizeof(sensorSize), &sensorSize, NULL), "Fail to get camera swnsor size parameter")) return;

        const float focalLength = sensorSize[1] * proj[1][1] / 2;
        if (RPR_ERROR_CHECK(rprCameraSetFocalLength(m_camera, focalLength), "Fail to set focal length parameter")) return;

        m_cameraProjectionMatrix = proj;
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    const GfMatrix4d& GetCameraViewMatrix() const {
        return m_cameraViewMatrix;
    }

    const GfMatrix4d& GetCameraProjectionMatrix() const {
        return m_cameraProjectionMatrix;
    }

    void EnableAov(TfToken const& aovName, HdFormat format = HdFormatInvalid, bool setAsActive = false) {
        if (!m_rprContext) return;

        auto rprAovIt = kAovTokenToRprAov.find(aovName);
        if (rprAovIt == kAovTokenToRprAov.end()) {
            TF_WARN("Unsupported aov type: %s", aovName.GetText());
            return;
        }

        RecursiveLockGuard rprLock(m_rprAccessMutex);

        if (IsAovEnabled(aovName)) {
            // While usdview does not have correct AOV system
            // we have ambiguity in currently selected AOV that we can't distinguish
            if (aovName == HdRprAovTokens->depth) {
                return;
            }

            if (setAsActive) {
                if (m_currentAov != aovName) {
                    m_dirtyFlags |= ChangeTracker::DirtyActiveAOV;
                }
                m_currentAov = aovName;
            }
            return;
        }

        try {
            AovFrameBuffer aovFrameBuffer;
            aovFrameBuffer.format = format;

            // We compute depth from worldCoordinate in the postprocess step
            if (aovName == HdRprAovTokens->depth) {
                EnableAov(HdRprAovTokens->worldCoordinate);
            } else {
                aovFrameBuffer.aov = make_unique<rpr::FrameBuffer>(m_rprContext->GetHandle(), m_fbWidth, m_fbHeight);
                if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID && aovName == HdRprAovTokens->normal) {
                    // TODO: remove me when Hybrid gain RPR_AOV_GEOMETRIC_NORMAL support
                    aovFrameBuffer.aov->AttachAs(RPR_AOV_SHADING_NORMAL);
                } else {
                    aovFrameBuffer.aov->AttachAs(rprAovIt->second);
                }

                // XXX: Hybrid plugin does not support framebuffer resolving (rprContextResolveFrameBuffer)
                if (m_rprContext->GetActivePluginType() != rpr::PluginType::HYBRID) {
                    aovFrameBuffer.resolved = make_unique<rpr::FrameBuffer>(m_rprContext->GetHandle(), m_fbWidth, m_fbHeight);
                }
            }

            m_aovFrameBuffers.emplace(aovName, std::move(aovFrameBuffer));
            m_dirtyFlags |= ChangeTracker::DirtyAOVFramebuffers;

            if (setAsActive) {
                m_currentAov = aovName;
            }
        } catch (rpr::Error const& e) {
            TF_CODING_ERROR("Failed to enable %s AOV: %s", aovName.GetText(), e.what());
        }
    }

    void DisableAov(TfToken const& aovName, bool force = false) {
        // XXX: RPR bug - rprContextRender requires RPR_AOV_COLOR to be set,
        // otherwise it fails with error RPR_ERROR_INVALID_OBJECT
        if (aovName == HdRprAovTokens->color && !force) {
            return;
        }

        RecursiveLockGuard rprLock(m_rprAccessMutex);

        auto it = m_aovFrameBuffers.find(aovName);
        if (it != m_aovFrameBuffers.end()) {
            // We compute depth from worldCoordinate in the postprocess step
            if (aovName == HdRprAovTokens->worldCoordinate &&
                m_aovFrameBuffers.count(HdRprAovTokens->depth)) {
                return;
            }

            m_aovFrameBuffers.erase(it);
        }

        m_dirtyFlags |= ChangeTracker::DirtyAOVFramebuffers;
    }

    void DisableAovs() {
        RecursiveLockGuard rprLock(m_rprAccessMutex);

        m_aovFrameBuffers.clear();
        m_dirtyFlags |= ChangeTracker::DirtyAOVFramebuffers;
    }

    bool IsAovEnabled(TfToken const& aovName) {
        return m_aovFrameBuffers.count(aovName) != 0;
    }

    void ResolveFramebuffers() {
        for (auto& aovFb : m_aovFrameBuffers) {
            if (!aovFb.second.aov) {
                continue;
            }

            try {
                aovFb.second.aov->Resolve(aovFb.second.resolved.get());
            } catch (rpr::Error const& e) {
                TF_CODING_ERROR("Failed to resolve framebuffer: %s", e.what());
            }
        }
    }

    void ResizeAovFramebuffers(int width, int height) {
        if (!m_rprContext) return;

        if (width <= 0 || height <= 0 ||
            (width == m_fbWidth && height == m_fbHeight)) {
            return;
        }

        m_fbWidth = width;
        m_fbHeight = height;
        RPR_ERROR_CHECK(rprCameraSetSensorSize(m_camera, 1.0f, (float)height / (float)width), "Fail to set camera sensor size");

        for (auto& aovFb : m_aovFrameBuffers) {
            if (!aovFb.second.aov) {
                continue;
            }

            try {
                aovFb.second.aov->Resize(width, height);
                aovFb.second.resolved->Resize(width, height);
                aovFb.second.isDirty = true;
            } catch (rpr::Error const& e) {
                TF_CODING_ERROR("Failed to resize AOV framebuffer: %s", e.what());
            }
        }

        m_dirtyFlags |= ChangeTracker::DirtyAOVFramebuffers;
    }

    void GetFramebufferSize(GfVec2i* resolution) const {
        resolution->Set(m_fbWidth, m_fbHeight);
    }

    std::shared_ptr<char> GetFramebufferData(TfToken const& aovName, std::shared_ptr<char> buffer, size_t* bufferSize) {
        auto it = m_aovFrameBuffers.find(aovName);
        if (it == m_aovFrameBuffers.end()) {
            return nullptr;
        }

        if (!m_fbWidth || !m_fbHeight) {
            return nullptr;
        }

        auto readRifImage = [](rif_image image, size_t* bufferSize) -> std::shared_ptr<char> {
            size_t size;
            size_t dummy;
            auto rifStatus = rifImageGetInfo(image, RIF_IMAGE_DATA_SIZEBYTE, sizeof(size), &size, &dummy);
            if (rifStatus != RIF_SUCCESS) {
                return nullptr;
            }

            void* data = nullptr;
            rifStatus = rifImageMap(image, RIF_IMAGE_MAP_READ, &data);
            if (rifStatus != RIF_SUCCESS) {
                return nullptr;
            }

            auto buffer = std::shared_ptr<char>(new char[size]);
            std::memcpy(buffer.get(), data, size);

            rifStatus = rifImageUnmap(image, data);
            if (rifStatus != RIF_SUCCESS) {
                TF_WARN("Failed to unmap rif image");
            }

            if (bufferSize) {
                *bufferSize = size;
            }
            return buffer;
        };

        RecursiveLockGuard rprLock(m_rprAccessMutex);

        if (aovName == HdRprAovTokens->color && m_denoiseFilterPtr) {
            buffer = readRifImage(m_denoiseFilterPtr->GetOutput(), bufferSize);
        } else {
            auto& aovFrameBuffer = it->second;
            if (aovFrameBuffer.postprocessFilter) {
                buffer = readRifImage(aovFrameBuffer.postprocessFilter->GetOutput(), bufferSize);
            } else {
                auto resolvedFb = aovFrameBuffer.resolved.get();
                if (!resolvedFb) {
                    resolvedFb = aovFrameBuffer.aov.get();
                }
                if (!resolvedFb) {
                    assert(false);
                    return nullptr;
                }

                buffer = resolvedFb->GetData(buffer, bufferSize);
            }
        }

        return buffer;
    }

    void ClearFramebuffers() {
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    rif_image_desc GetRifImageDesc(uint32_t width, uint32_t height, HdFormat format) {
        rif_image_desc imageDesc = {};
        imageDesc.num_components = HdGetComponentCount(format);
        switch (HdGetComponentFormat(format)) {
        case HdFormatUNorm8:
            imageDesc.type = RIF_COMPONENT_TYPE_UINT8;
            break;
        case HdFormatFloat16:
            imageDesc.type = RIF_COMPONENT_TYPE_FLOAT16;
            break;
        case HdFormatFloat32:
            imageDesc.type = RIF_COMPONENT_TYPE_FLOAT32;
            break;
        default:
            imageDesc.type = 0;
            break;
        }
        imageDesc.image_width = width;
        imageDesc.image_height = height;
        imageDesc.image_depth = 1;
        imageDesc.image_row_pitch = width;
        imageDesc.image_slice_pitch = width * height;

        return imageDesc;
    }

    void Update() {
        auto& preferences = HdRprConfig::GetInstance();
        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            if (preferences.IsDirty(HdRprConfig::DirtyHybridQuality)) {
                rprContextSetParameter1u(m_rprContext->GetHandle(), "render_quality", int(preferences.GetHybridQuality()));
            }
        }

        // In case there is no Lights in scene - create default
        if (!m_isLightPresent) {
            const GfVec3f k_defaultLightColor(0.5f, 0.5f, 0.5f);
            CreateEnvironmentLight(k_defaultLightColor, 1.f);
        }

        UpdateDenoiseFilter();

        for (auto& aovFrameBufferEntry : m_aovFrameBuffers) {
            auto& aovFrameBuffer = aovFrameBufferEntry.second;
            if (aovFrameBuffer.isDirty) {
                aovFrameBuffer.isDirty = false;

                rif_image_desc imageDesc = GetRifImageDesc(m_fbWidth, m_fbHeight, aovFrameBuffer.format);                
    
                if (aovFrameBufferEntry.first == HdRprAovTokens->depth) {
                    // Calculate clip space depth from world coordinate AOV
                    if (m_aovFrameBuffers.count(HdRprAovTokens->worldCoordinate) == 0) {
                        assert(!"World coordinate AOV should be enabled");
                        return;
                    }

                    auto ndcDepthFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_NDC_DEPTH, m_rifContext.get());

                    auto& worldCoordinateAovFb = m_aovFrameBuffers[HdRprAovTokens->worldCoordinate];
                    auto inputRprFrameBuffer = (worldCoordinateAovFb.resolved ? worldCoordinateAovFb.resolved : worldCoordinateAovFb.aov).get();
                    ndcDepthFilter->SetInput(rif::Color, inputRprFrameBuffer, 1.0f);

                    ndcDepthFilter->SetOutput(imageDesc);

                    auto viewProjectionMatrix = m_cameraViewMatrix * m_cameraProjectionMatrix;
                    ndcDepthFilter->AddParam("viewProjMatrix", GfMatrix4f(viewProjectionMatrix));

                    aovFrameBuffer.postprocessFilter = std::move(ndcDepthFilter);
                } else if (aovFrameBufferEntry.first == HdRprAovTokens->normal &&
                           aovFrameBuffer.format != HdFormatInvalid) {
                    // Remap normal to [-1;1] range
                    auto remapFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_REMAP_RANGE, m_rifContext.get());

                    auto inputRprFrameBuffer = (aovFrameBuffer.resolved ? aovFrameBuffer.resolved : aovFrameBuffer.aov).get();
                    remapFilter->SetInput(rif::Color, inputRprFrameBuffer, 1.0f);

                    remapFilter->SetOutput(imageDesc);

                    remapFilter->AddParam("srcRangeAuto", 0);
                    remapFilter->AddParam("dstLo", -1.0f);
                    remapFilter->AddParam("dstHi", 1.0f);

                    aovFrameBuffer.postprocessFilter = std::move(remapFilter);
                } else if (aovFrameBuffer.format != HdFormatInvalid &&
                           aovFrameBuffer.format != HdFormatFloat32Vec4 &&
                           m_fbWidth != 0 && m_fbHeight != 0) {
                    // Convert from RPR native to Hydra format
                    auto inputRprFrameBuffer = (aovFrameBuffer.resolved ? aovFrameBuffer.resolved : aovFrameBuffer.aov).get();
                    if (inputRprFrameBuffer && imageDesc.type != 0 && imageDesc.num_components != 0) {
                        auto converter = rif::Filter::Create(rif::FilterType::Resample, m_rifContext.get(), m_fbWidth, m_fbHeight);
                        converter->SetInput(rif::Color, inputRprFrameBuffer, 1.0f);
                        converter->SetOutput(imageDesc);

                        aovFrameBuffer.postprocessFilter = std::move(converter);
                    }
                }
            }
        }

        if (m_dirtyFlags & ChangeTracker::DirtyScene) {
            for (auto& aovFb : m_aovFrameBuffers) {
                if (aovFb.second.aov) {
                    aovFb.second.aov->Clear();
                }
            }
        }

        try {
            if (m_denoiseFilterPtr) {
                m_denoiseFilterPtr->Update();
            }
            for (auto& aovFrameBuffer : m_aovFrameBuffers) {
                if (aovFrameBuffer.second.postprocessFilter) {
                    aovFrameBuffer.second.postprocessFilter->Update();
                }
            }
        }
        catch (std::runtime_error& e) {
            TF_RUNTIME_ERROR("%s", e.what());
        }

        m_dirtyFlags = ChangeTracker::Clean;
        preferences.ResetDirty();
    }

    void UpdateDenoiseFilter() {
        // XXX: RPR Hybrid context does not support filters. Discuss with Hybrid team possible workarounds
        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            return;
        }

        if (!HdRprConfig::GetInstance().IsDirty(HdRprConfig::DirtyDenoising) &&
            !(m_dirtyFlags & ChangeTracker::DirtyAOVFramebuffers)) {
            return;
        }

        if (!HdRprConfig::GetInstance().IsDenoisingEnabled() || !IsAovEnabled(HdRprAovTokens->color)) {
            m_denoiseFilterPtr.reset();
            return;
        }

        rif::FilterType filterType = rif::FilterType::EawDenoise;
#ifndef __APPLE__
        if (m_rprContext->GetActiveRenderDeviceType() == rpr::RenderDeviceType::GPU) {
            filterType = rif::FilterType::AIDenoise;
        }
#endif // __APPLE__
        m_denoiseFilterPtr = rif::Filter::Create(filterType, m_rifContext.get(), m_fbWidth, m_fbHeight);

        switch (filterType) {
        case rif::FilterType::AIDenoise: {
            EnableAov(HdRprAovTokens->albedo);
            EnableAov(HdRprAovTokens->linearDepth);
            EnableAov(HdRprAovTokens->normal);

            m_denoiseFilterPtr->SetInput(rif::Color, m_aovFrameBuffers[HdRprAovTokens->color].resolved.get(), 1.0f);
            m_denoiseFilterPtr->SetInput(rif::Normal, m_aovFrameBuffers[HdRprAovTokens->normal].resolved.get(), 1.0f);
            m_denoiseFilterPtr->SetInput(rif::Depth, m_aovFrameBuffers[HdRprAovTokens->linearDepth].resolved.get(), 1.0f);
            m_denoiseFilterPtr->SetInput(rif::Albedo, m_aovFrameBuffers[HdRprAovTokens->albedo].resolved.get(), 1.0f);
            break;
        }
        case rif::FilterType::EawDenoise: {
            m_denoiseFilterPtr->AddParam("colorSigma", 1.0f);
            m_denoiseFilterPtr->AddParam("normalSigma", 1.0f);
            m_denoiseFilterPtr->AddParam("depthSigma", 1.0f);
            m_denoiseFilterPtr->AddParam("transSigma", 1.0f);

            EnableAov(HdRprAovTokens->albedo);
            EnableAov(HdRprAovTokens->linearDepth);
            EnableAov(HdRprAovTokens->normal);
            EnableAov(HdRprAovTokens->primId);
            EnableAov(HdRprAovTokens->worldCoordinate);

            m_denoiseFilterPtr->SetInput(rif::Color, m_aovFrameBuffers[HdRprAovTokens->color].resolved.get(), 1.0f);
            m_denoiseFilterPtr->SetInput(rif::Normal, m_aovFrameBuffers[HdRprAovTokens->normal].resolved.get(), 1.0f);
            m_denoiseFilterPtr->SetInput(rif::Depth, m_aovFrameBuffers[HdRprAovTokens->linearDepth].resolved.get(), 1.0f);
            m_denoiseFilterPtr->SetInput(rif::ObjectId, m_aovFrameBuffers[HdRprAovTokens->primId].resolved.get(), 1.0f);
            m_denoiseFilterPtr->SetInput(rif::Albedo, m_aovFrameBuffers[HdRprAovTokens->albedo].resolved.get(), 1.0f);
            m_denoiseFilterPtr->SetInput(rif::WorldCoordinate, m_aovFrameBuffers[HdRprAovTokens->worldCoordinate].resolved.get(), 1.0f);
            break;
        }
        default:
            break;
        }

        m_denoiseFilterPtr->SetOutput(GetRifImageDesc(m_fbWidth, m_fbHeight, m_aovFrameBuffers[HdRprAovTokens->color].format));
    }

    void Render() {
        if (!m_rprContext || m_aovFrameBuffers.empty()) {
            return;
        }
        RecursiveLockGuard rprLock(m_rprAccessMutex);

        Update();

        if (RPR_ERROR_CHECK(rprContextRender(m_rprContext->GetHandle()), "Fail contex render framebuffer")) return;

        ResolveFramebuffers();

        /*auto testFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_NDC_DEPTH, m_rifContext.get());

        GfVec4f testPosition(0.0f, 0.0f, 0.0f, 1.0f);
        //auto testMatrix = m_cameraViewMatrix * m_cameraProjectionMatrix;
        GfMatrix4f testMatrix;
        testMatrix.SetTranslate(GfVec3f(0.0f, 0.0f, 2.0f));

        auto inputImage = m_rifContext->CreateImage(GetRifImageDesc(1, 1, HdFormatFloat32Vec4));
        void* data;
        rifImageMap(inputImage->GetHandle(), RIF_IMAGE_MAP_WRITE, &data);
        std::memcpy(data, testPosition.data(), sizeof(GfVec4f));
        rifImageUnmap(inputImage->GetHandle(), data);
        testFilter->SetInput(rif::Color, inputImage->GetHandle(), 1.0f);
        testFilter->SetOutput(GetRifImageDesc(1, 1, HdFormatFloat32));

        testFilter->AddParam("viewProjMatrix", GfMatrix4f(testMatrix.GetTranspose()));
        testFilter->Update();*/

        try {
            m_rifContext->ExecuteCommandQueue();
        } catch (std::runtime_error& e) {
            TF_RUNTIME_ERROR("%s", e.what());
        }

        /*rifImageMap(testFilter->GetOutput(), RIF_IMAGE_MAP_READ, &data);
        float depthFromFilter;
        std::memcpy(&depthFromFilter, data, sizeof(depthFromFilter));
        rifImageUnmap(testFilter->GetOutput(), data);

        float depthCpu = testMatrix.Transform(GfVec3f(testPosition.data()))[2];
        float depthCpuTranspose = testMatrix.GetTranspose().Transform(GfVec3f(testPosition.data()))[2];

        int a = 0;*/
    }

    void DeleteMesh(void* mesh) {
        if (!mesh) {
            return;
        }

        RecursiveLockGuard rprLock(m_rprAccessMutex);

        RPR_ERROR_CHECK(rprShapeSetMaterial(mesh, nullptr), "Fail reset mesh material");
        RPR_ERROR_CHECK(rprSceneDetachShape(m_scene, mesh), "Fail detach mesh from scene");

        rprObjectDelete(mesh);
    }

    TfToken GetActiveAov() const {
        return m_currentAov;
    }

    bool IsGlInteropEnabled() const {
        return m_rprContext && m_rprContext->IsGlInteropEnabled();
    }

private:
    void InitRpr() {
        auto plugin = HdRprConfig::GetInstance().GetPlugin();
        auto renderDevice = HdRprConfig::GetInstance().GetRenderDevice();
        m_rprContext = rpr::Context::Create(plugin, renderDevice, false);
        if (!m_rprContext) {
            return;
        }

        RPR_ERROR_CHECK(rprContextSetParameter1u(m_rprContext->GetHandle(), "yflip", 0), "Fail to set context YFLIP parameter");
        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            RPR_ERROR_CHECK(rprContextSetParameter1u(m_rprContext->GetHandle(), "render_quality", int(HdRprConfig::GetInstance().GetHybridQuality())), "Fail to set context hybrid render quality");
        }
    }

    void InitRif() {
        if (!m_rprContext) {
            return;
        }

        m_rifContext = rif::Context::Create(m_rprContext->GetHandle());
    }

    void InitMaterialSystem() {
        if (!m_rprContext) {
            return;
        }

        if (RPR_ERROR_CHECK(rprContextCreateMaterialSystem(m_rprContext->GetHandle(), 0, &m_matsys), "Fail create Material System resolve")) return;
        m_rprObjectsToRelease.push_back(m_matsys);
        m_rprMaterialFactory.reset(new RprMaterialFactory(m_matsys, m_rprContext->GetHandle()));
    }

    void SplitPolygons(const VtIntArray & indexes, const VtIntArray & vpf, VtIntArray & out_newIndexes, VtIntArray & out_newVpf) {
        out_newIndexes.clear();
        out_newVpf.clear();

        out_newIndexes.reserve(indexes.size());
        out_newVpf.reserve(vpf.size());

        VtIntArray::const_iterator idxIt = indexes.begin();
        for (const int vCount : vpf) {
            if (vCount == 3 || vCount == 4) {
                for (int i = 0; i < vCount; ++i) {
                    out_newIndexes.push_back(*idxIt);
                    ++idxIt;
                }
                out_newVpf.push_back(vCount);
            } else {
                constexpr int triangleVertexCount = 3;
                for (int i = 0; i < vCount - 2; ++i) {
                    out_newIndexes.push_back(*(idxIt + i + 0));
                    out_newIndexes.push_back(*(idxIt + i + 1));
                    out_newIndexes.push_back(*(idxIt + i + 2));
                    out_newVpf.push_back(triangleVertexCount);
                }
                idxIt += vCount;
            }
        }
    }

    void SplitPolygons(const VtIntArray & indexes, const VtIntArray & vpf, VtIntArray & out_newIndexes) {
        out_newIndexes.clear();
        out_newIndexes.reserve(indexes.size());

        VtIntArray::const_iterator idxIt = indexes.begin();
        for (const int vCount : vpf) {
            if (vCount == 3 || vCount == 4) {
                for (int i = 0; i < vCount; ++i) {
                    out_newIndexes.push_back(*idxIt);
                    ++idxIt;
                }
            } else {
                for (int i = 0; i < vCount - 2; ++i) {
                    out_newIndexes.push_back(*(idxIt + i + 0));
                    out_newIndexes.push_back(*(idxIt + i + 1));
                    out_newIndexes.push_back(*(idxIt + i + 2));
                }
                idxIt += vCount;
            }
        }
    }

    void* CreateCubeMesh(const float & width, const float & height, const float & depth) {
        constexpr const size_t cubeVertexCount = 24;
        constexpr const size_t cubeNormalCount = 24;
        constexpr const size_t cubeIndexCount = 36;
        constexpr const size_t cubeVpfCount = 12;

        VtVec3fArray position(cubeVertexCount);
        position[0] = GfVec3f(-width, height, -depth);
        position[1] = GfVec3f(width, height, -depth);
        position[2] = GfVec3f(width, height, depth);
        position[3] = GfVec3f(-width, height, depth);

        position[4] = GfVec3f(-width, -height, -depth);
        position[5] = GfVec3f(width, -height, -depth);
        position[6] = GfVec3f(width, -height, depth);
        position[7] = GfVec3f(-width, -height, depth);

        position[8] = GfVec3f(-width, -height, depth);
        position[9] = GfVec3f(-width, -height, -depth);
        position[10] = GfVec3f(-width, height, -depth);
        position[11] = GfVec3f(-width, height, depth);

        position[12] = GfVec3f(width, -height, depth);
        position[13] = GfVec3f(width, -height, -depth);
        position[14] = GfVec3f(width, height, -depth);
        position[15] = GfVec3f(width, height, depth);

        position[16] = GfVec3f(-width, -height, -depth);
        position[17] = GfVec3f(width, -height, -depth);
        position[18] = GfVec3f(width, height, -depth);
        position[19] = GfVec3f(-width, height, -depth);

        position[20] = GfVec3f(-width, -height, depth);
        position[21] = GfVec3f(width, -height, depth);
        position[22] = GfVec3f(width, height, depth);
        position[23] = GfVec3f(-width, height, depth);

        VtVec3fArray normals(cubeNormalCount);
        normals[0] = GfVec3f(0.f, 1.f, 0.f);
        normals[1] = GfVec3f(0.f, 1.f, 0.f);
        normals[2] = GfVec3f(0.f, 1.f, 0.f);
        normals[3] = GfVec3f(0.f, 1.f, 0.f);

        normals[4] = GfVec3f(0.f, -1.f, 0.f);
        normals[5] = GfVec3f(0.f, -1.f, 0.f);
        normals[6] = GfVec3f(0.f, -1.f, 0.f);
        normals[7] = GfVec3f(0.f, -1.f, 0.f);

        normals[8] = GfVec3f(-1.f, 0.f, 0.f);
        normals[9] = GfVec3f(-1.f, 0.f, 0.f);
        normals[10] = GfVec3f(-1.f, 0.f, 0.f);
        normals[11] = GfVec3f(-1.f, 0.f, 0.f);

        normals[12] = GfVec3f(1.f, 0.f, 0.f);
        normals[13] = GfVec3f(1.f, 0.f, 0.f);
        normals[14] = GfVec3f(1.f, 0.f, 0.f);
        normals[15] = GfVec3f(1.f, 0.f, 0.f);

        normals[16] = GfVec3f(0.f, 0.f, -1.f);
        normals[17] = GfVec3f(0.f, 0.f, -1.f);
        normals[18] = GfVec3f(0.f, 0.f, -1.f);
        normals[19] = GfVec3f(0.f, 0.f, -1.f);

        normals[20] = GfVec3f(0.f, 0.f, 1.f);
        normals[21] = GfVec3f(0.f, 0.f, 1.f);
        normals[22] = GfVec3f(0.f, 0.f, 1.f);
        normals[23] = GfVec3f(0.f, 0.f, 1.f);

        VtIntArray indexes = {
            3,1,0,
            2,1,3,

            6,4,5,
            7,4,6,

            11,9,8,
            10,9,11,

            14,12,13,
            15,12,14,

            19,17,16,
            18,17,19,

            22,20,21,
            23,20,22
        };

        VtIntArray vpf(cubeVpfCount, 3);
        VtVec2fArray uv; // empty

        return CreateMesh(position, indexes, normals, VtIntArray(), uv, VtIntArray(), vpf);
    }

    enum ChangeTracker : uint32_t {
        Clean = 0,
        AllDirty = ~0u,
        DirtyScene = 1 << 0,
        DirtyActiveAOV = 1 << 1,
        DirtyAOVFramebuffers = 1 << 2
    };
    uint32_t m_dirtyFlags = ChangeTracker::AllDirty;

    rpr_uint m_fbWidth = 0;
    rpr_uint m_fbHeight = 0;

    std::unique_ptr<rpr::Context> m_rprContext;
    std::unique_ptr<rif::Context> m_rifContext;
    rpr_scene m_scene = nullptr;
    rpr_camera m_camera = nullptr;
    rpr_post_effect m_tonemap = nullptr;

    rpr_material_system m_matsys = nullptr;
    std::unique_ptr<RprMaterialFactory> m_rprMaterialFactory;
    std::vector<RprApiMaterial*> m_materialsToRelease;

    std::vector<void*> m_rprObjectsToRelease;

    struct AovFrameBuffer {
        std::unique_ptr<rpr::FrameBuffer> aov;
        std::unique_ptr<rpr::FrameBuffer> resolved;
        std::unique_ptr<rif::Filter> postprocessFilter;
        HdFormat format = HdFormatInvalid;
        bool isDirty = true;
    };
    std::map<TfToken, AovFrameBuffer> m_aovFrameBuffers;
    TfToken m_currentAov;

    GfMatrix4d m_cameraViewMatrix = GfMatrix4d(1.f);
    GfMatrix4d m_cameraProjectionMatrix = GfMatrix4d(1.f);

    bool m_isLightPresent = false;

    std::recursive_mutex m_rprAccessMutex;

    std::unique_ptr<rif::Filter> m_denoiseFilterPtr;
};

    HdRprApi::HdRprApi() : m_impl(new HdRprApiImpl) {

    }

    HdRprApi::~HdRprApi() {
        delete m_impl;
    }

    TfToken HdRprApi::GetActiveAov() const {
        return m_impl->GetActiveAov();
    }

    RprApiObject HdRprApi::CreateMesh(const VtVec3fArray & points, const VtIntArray & pointIndexes, const VtVec3fArray & normals, const VtIntArray & normalIndexes, const VtVec2fArray & uv, const VtIntArray & uvIndexes, const VtIntArray & vpf) {
        return m_impl->CreateMesh(points, pointIndexes, normals, normalIndexes, uv, uvIndexes, vpf);
    }

    RprApiObject HdRprApi::CreateCurve(const VtVec3fArray & points, const VtIntArray & indexes, const float & width) {
        return m_impl->CreateCurve(points, indexes, width);
    }

    void HdRprApi::CreateInstances(RprApiObject prototypeMesh, const VtMatrix4dArray & transforms, VtArray<RprApiObject>& out_instances) {
        out_instances.clear();
        out_instances.reserve(transforms.size());
        for (const GfMatrix4d & transform : transforms) {
            if (void* meshInstamce = m_impl->CreateMeshInstance(prototypeMesh)) {
                m_impl->SetMeshTransform(meshInstamce, GfMatrix4f(transform));
                out_instances.push_back(meshInstamce);
            }
        }

        // Hide prototype
        m_impl->SetMeshVisibility(prototypeMesh, false);
    }

    void HdRprApi::CreateEnvironmentLight(const std::string & prthTotexture, float intensity) {
        m_impl->CreateEnvironmentLight(prthTotexture, intensity);
    }

    RprApiObject HdRprApi::CreateRectLightMesh(const float & width, const float & height) {
        return m_impl->CreateRectLightGeometry(width, height);
    }

    RprApiObject HdRprApi::CreateSphereLightMesh(const float & radius) {
        return m_impl->CreateSphereLightGeometry(radius);
    }

    RprApiObject HdRprApi::CreateDiskLight(const float & width, const float & height, const GfVec3f & emmisionColor) {
        return m_impl->CreateDiskLight(width, height, emmisionColor);
    }

    void HdRprApi::CreateVolume(const VtArray<float> & gridDencityData, const VtArray<size_t> & indexesDencity, const VtArray<float> & gridAlbedoData, const VtArray<unsigned int> & indexesAlbedo, const GfVec3i & gridSize, const GfVec3f & voxelSize, RprApiObject out_mesh, RprApiObject out_heteroVolume) {
        m_impl->CreateVolume(gridDencityData, indexesDencity, gridAlbedoData, indexesAlbedo, gridSize, voxelSize, out_mesh, out_heteroVolume);
    }

    RprApiMaterial * HdRprApi::CreateMaterial(MaterialAdapter & materialAdapter) {
        return m_impl->CreateMaterial(materialAdapter);
    }

    void HdRprApi::DeleteMaterial(RprApiMaterial *rprApiMaterial) {
        m_impl->DeleteMaterial(rprApiMaterial);
    }

    void HdRprApi::SetMeshTransform(RprApiObject mesh, const GfMatrix4d & transform) {
        GfMatrix4f transformF(transform);
        m_impl->SetMeshTransform(mesh, transformF);
    }

    void HdRprApi::SetMeshRefineLevel(RprApiObject mesh, int level, TfToken boundaryInterpolation) {
        m_impl->SetMeshRefineLevel(mesh, level, boundaryInterpolation);
    }

    void HdRprApi::SetMeshMaterial(RprApiObject mesh, const RprApiMaterial * material) {
        m_impl->SetMeshMaterial(mesh, material);
    }

    void HdRprApi::SetCurveMaterial(RprApiObject curve, const RprApiMaterial * material) {
        m_impl->SetCurveMaterial(curve, material);
    }

    const GfMatrix4d & HdRprApi::GetCameraViewMatrix() const {
        return m_impl->GetCameraViewMatrix();
    }

    const GfMatrix4d & HdRprApi::GetCameraProjectionMatrix() const {
        return m_impl->GetCameraProjectionMatrix();
    }

    void HdRprApi::SetCameraViewMatrix(const GfMatrix4d & m) {
        m_impl->SetCameraViewMatrix(m);
    }

    void HdRprApi::SetCameraProjectionMatrix(const GfMatrix4d & m) {
        m_impl->SetCameraProjectionMatrix(m);
    }

    void HdRprApi::EnableAov(TfToken const& aovName, HdFormat format) {
        m_impl->EnableAov(aovName, format, true);
    }

    void HdRprApi::DisableAov(TfToken const& aovName) {
        m_impl->DisableAov(aovName);
    }

    void HdRprApi::DisableAovs() {
        m_impl->DisableAovs();
    }

    bool HdRprApi::IsAovEnabled(TfToken const& aovName) {
        return m_impl->IsAovEnabled(aovName);
    }

    void HdRprApi::ClearFramebuffers() {
        m_impl->ClearFramebuffers();
    }

    void HdRprApi::ResizeAovFramebuffers(int width, int height) {
        m_impl->ResizeAovFramebuffers(width, height);
    }

    void HdRprApi::GetFramebufferSize(GfVec2i* resolution) const {
        m_impl->GetFramebufferSize(resolution);
    }

    std::shared_ptr<char> HdRprApi::GetFramebufferData(TfToken const& aovName, std::shared_ptr<char> buffer, size_t* bufferSize) {
        return m_impl->GetFramebufferData(aovName, buffer, bufferSize);
    }

    void HdRprApi::Render() {
        m_impl->Render();
    }

    void HdRprApi::DeleteMesh(RprApiObject mesh) {
        m_impl->DeleteMesh(mesh);
    }

    bool HdRprApi::IsGlInteropEnabled() const {
        return m_impl->IsGlInteropEnabled();
    }

    const char* HdRprApi::GetTmpDir() {
        return rpr::Context::GetCachePath();
    }

PXR_NAMESPACE_CLOSE_SCOPE
