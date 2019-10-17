#include "rprApi.h"
#include "rprcpp/rprFramebufferGL.h"
#include "rprcpp/rprContext.h"
#include "rprcpp/rprImage.h"
#include "rifcpp/rifFilter.h"
#include "rifcpp/rifImage.h"

#include "config.h"
#include "material.h"
#include "materialFactory.h"
#include "materialAdapter.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/imaging/pxOsd/tokens.h"

#include <RadeonProRender.h>
#include <RadeonProRender_Baikal.h>
#include <vector>
#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdRprAovTokens, HD_RPR_AOV_TOKENS);

#define RPR_API_OBJECT_ACTION_TOKENS \
    (attach)                         \
    (contextSetScene)

TF_DEFINE_PRIVATE_TOKENS(RprApiObjectActionTokens, RPR_API_OBJECT_ACTION_TOKENS);

namespace
{

const uint32_t k_diskVertexCount = 32;

using RecursiveLockGuard = std::lock_guard<std::recursive_mutex>;
std::recursive_mutex g_rprAccessMutex;

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
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

    int m_iter = 0;
    int m_maxSamples = 0;

    void CreateScene() {
        if (!m_rprContext) {
            return;
        }

        rpr_scene scene;
        if (RPR_ERROR_CHECK(rprContextCreateScene(m_rprContext->GetHandle(), &scene), "Fail to create scene")) return;
        m_scene = RprApiObject::Wrap(scene);

        if (RPR_ERROR_CHECK(rprContextSetScene(m_rprContext->GetHandle(), scene), "Fail to set scene")) return;
        m_scene->AttachOnReleaseAction(RprApiObjectActionTokens->contextSetScene, [this](void* scene) {
            if (!RPR_ERROR_CHECK(rprContextSetScene(m_rprContext->GetHandle(), nullptr), "Failed to unset scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        });
    }

    void CreateCamera() {
        if (!m_rprContext) {
            return;
        }

        rpr_camera camera;
        RPR_ERROR_CHECK(rprContextCreateCamera(m_rprContext->GetHandle(), &camera), "Fail to create camera");
        m_camera = RprApiObject::Wrap(camera);

        RPR_ERROR_CHECK(rprSceneSetCamera(m_scene->GetHandle(), camera), "Fail to to set camera to scene");
        m_camera->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* camera) {
            if (!RPR_ERROR_CHECK(rprSceneSetCamera(m_scene->GetHandle(), nullptr), "Failed to unset camera")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        });

        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    RprApiObjectPtr CreateMesh(const VtVec3fArray & points, const VtIntArray & pointIndexes, const VtVec3fArray & normals, const VtIntArray & normalIndexes, const VtVec2fArray & uv, const VtIntArray & uvIndexes, const VtIntArray & vpf) {
        if (!m_rprContext) {
            return nullptr;
        }

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

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        rpr_shape mesh = nullptr;
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
        auto meshObject = RprApiObject::Wrap(mesh);

        if (RPR_ERROR_CHECK(rprSceneAttachShape(m_scene->GetHandle(), mesh), "Fail attach mesh to scene")) {
            return nullptr;
        }
        meshObject->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* mesh) {
            if (!RPR_ERROR_CHECK(rprSceneDetachShape(m_scene->GetHandle(), mesh), "Failed to dettach mesh from scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        });

        m_dirtyFlags |= ChangeTracker::DirtyScene;

        return meshObject;
    }

    void SetMeshTransform(rpr_shape mesh, const GfMatrix4f& transform) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);
        if (!RPR_ERROR_CHECK(rprShapeSetTransform(mesh, false, transform.GetArray()), "Fail set mesh transformation")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void SetMeshRefineLevel(rpr_shape mesh, const int level, TfToken const& boundaryInterpolation) {
        if (!m_rprContext) {
            return;
        }

        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            // Not supported
            return;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);

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
        RecursiveLockGuard rprLock(g_rprAccessMutex);
        m_rprMaterialFactory->AttachMaterialToShape(mesh, material);
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    void SetMeshHeteroVolume(rpr_shape mesh, rpr_hetero_volume heteroVolume) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);
        if (!RPR_ERROR_CHECK(rprShapeSetHeteroVolume(mesh, heteroVolume), "Fail set mesh hetero volume")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void SetCurveMaterial(rpr_shape curve, const RprApiMaterial* material) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);
        m_rprMaterialFactory->AttachMaterialToCurve(curve, material);
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    RprApiObjectPtr CreateMeshInstance(rpr_shape mesh) {
        if (!m_rprContext) {
            return nullptr;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        rpr_shape meshInstance;
        if (RPR_ERROR_CHECK(rprContextCreateInstance(m_rprContext->GetHandle(), mesh, &meshInstance), "Fail to create mesh instance")) {
            return nullptr;
        }
        auto meshInstanceObject = RprApiObject::Wrap(meshInstance);

        if (RPR_ERROR_CHECK(rprSceneAttachShape(m_scene->GetHandle(), meshInstance), "Fail to attach mesh instance")) {
            return nullptr;
        }
        m_dirtyFlags |= ChangeTracker::DirtyScene;

        meshInstanceObject->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* instance) {
            if (!RPR_ERROR_CHECK(rprSceneDetachShape(m_scene->GetHandle(), instance), "Failed to dettach mesh instance from scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        });

        return meshInstanceObject;
    }

    void SetMeshVisibility(rpr_shape mesh, bool isVisible) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        if (!RPR_ERROR_CHECK(rprShapeSetVisibility(mesh, isVisible), "Fail to set mesh visibility")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    RprApiObjectPtr CreateCurve(const VtVec3fArray& points, const VtIntArray& indexes, float width) {
        if (!m_rprContext || points.empty() || indexes.empty()) {
            return nullptr;
        }

        const size_t k_segmentSize = 4;

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

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        rpr_curve curve = nullptr;
        if (RPR_ERROR_CHECK(rprContextCreateCurve(m_rprContext->GetHandle(),
            &curve
            , newPoints.size()
            , (float*)newPoints.data()
            , sizeof(GfVec3f)
            , newIndexes.size()
            , 1
            , (const rpr_uint*)newIndexes.data()
            , &width, nullptr
            , segmentsPerCurve.data()), "Fail to create curve")) {
            return nullptr;
        }
        auto curveObject = RprApiObject::Wrap(curve);

        if (RPR_ERROR_CHECK(rprSceneAttachCurve(m_scene->GetHandle(), curve), "Fail to attach curve")) {
            return nullptr;
        }
        m_dirtyFlags |= ChangeTracker::DirtyScene;

        curveObject->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* curve) {
            if (!RPR_ERROR_CHECK(rprSceneDetachCurve(m_scene->GetHandle(), curve), "Failed to dettach curve from scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        });

        return RprApiObject::Wrap(curve);
    }

    RprApiObjectPtr CreateEnvironmentLight(std::unique_ptr<rpr::Image>&& image, float intensity) {
        rpr_light light;

        if (RPR_ERROR_CHECK(rprContextCreateEnvironmentLight(m_rprContext->GetHandle(), &light), "Fail to create environment light")) return nullptr;
        auto lightObject = RprApiObject::Wrap(light);

        if (RPR_ERROR_CHECK(rprEnvironmentLightSetImage(light, image->GetHandle()), "Fail to set image to environment light")) return nullptr;
        lightObject->AttachDependency(std::move(image));

        if (RPR_ERROR_CHECK(rprEnvironmentLightSetIntensityScale(light, intensity), "Fail to set environment light intencity")) return nullptr;
        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            if (RPR_ERROR_CHECK(rprSceneSetEnvironmentLight(m_scene->GetHandle(), light), "Fail to set environment light")) return nullptr;
            lightObject->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* light) {
                if (!RPR_ERROR_CHECK(rprSceneSetEnvironmentLight(m_scene->GetHandle(), nullptr), "Fail to unset environment light")) {
                    m_dirtyFlags |= ChangeTracker::DirtyScene;
                }
            });
        } else {
            if (RPR_ERROR_CHECK(rprSceneAttachLight(m_scene->GetHandle(), light), "Fail to attach environment light to scene")) return nullptr;
            lightObject->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* light) {
                if (!RPR_ERROR_CHECK(rprSceneDetachLight(m_scene->GetHandle(), light), "Fail to dettach environment light")) {
                    m_dirtyFlags |= ChangeTracker::DirtyScene;
                }
            });
        }

        m_isLightPresent = true;
        m_dirtyFlags |= ChangeTracker::DirtyScene;

        return lightObject;
    }

    RprApiObjectPtr CreateEnvironmentLight(const std::string& path, float intensity) {
        if (!m_rprContext || path.empty()) {
            return nullptr;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);
        try {
            auto image = make_unique<rpr::Image>(m_rprContext->GetHandle(), path.c_str());
            return CreateEnvironmentLight(std::move(image), intensity);
        } catch (rpr::Error const& error) {
            TF_RUNTIME_ERROR(error.what());
        }

        return nullptr;
    }

    RprApiObjectPtr CreateEnvironmentLight(GfVec3f color, float intensity) {
        if (!m_rprContext) {
            return nullptr;
        }

        std::array<float, 3> backgroundColor = {color[0], color[1], color[2]};
        rpr_image_format format = {3, RPR_COMPONENT_TYPE_FLOAT32};
        rpr_uint imageSize = m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID ? 64 : 1;
        std::vector<std::array<float, 3>> imageData(imageSize * imageSize, backgroundColor);

        try {
            auto image = make_unique<rpr::Image>(m_rprContext->GetHandle(), imageSize, imageSize, format, imageData[0].data());
            return CreateEnvironmentLight(std::move(image), intensity);
        } catch (rpr::Error const& error) {
            TF_RUNTIME_ERROR(error.what());
        }

        return nullptr;
    }

    RprApiObjectPtr CreateRectLightMesh(float width, float height) {
        constexpr const size_t rectVertexCount = 4;
        VtVec3fArray positions(rectVertexCount);
        positions[0] = GfVec3f(width * 0.5f, height * 0.5f, 0.f);
        positions[1] = GfVec3f(width * 0.5f, height * -0.5f, 0.f);
        positions[2] = GfVec3f(width * -0.5f, height * -0.5f, 0.f);
        positions[3] = GfVec3f(width * -0.5f, height * 0.5f, 0.f);

        // All normals -z
        VtVec3fArray normals(rectVertexCount, GfVec3f(0.f,0.f,-1.f));

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

    RprApiObjectPtr CreateDiskLightMesh(float width, float height, const GfVec3f& color) {
        VtVec3fArray positions;
        VtVec3fArray normals;
        VtVec2fArray uv; // empty
        VtIntArray idx;
        VtIntArray vpf;

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

        /*
        rpr_material_node material = nullptr;
        {
            RecursiveLockGuard rprLock(g_rprAccessMutex);

            if (RPR_ERROR_CHECK(rprMaterialSystemCreateNode(m_matsys, RPR_MATERIAL_NODE_EMISSIVE, &material), "Fail create emmisive material")) return nullptr;
            m_rprObjectsToRelease.push_back(material);
            if (RPR_ERROR_CHECK(rprMaterialNodeSetInputF(material, "color", color[0], color[1], color[2], 0.0f),"Fail set material color")) return nullptr;

            m_isLightPresent = true;
        }
        */

        return CreateMesh(positions, idx, normals, VtIntArray(), uv, VtIntArray(), vpf);
    }

    RprApiObjectPtr CreateSphereLightMesh(float radius) {
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
                positions.push_back(d * GfVec3f(sin(t)*cos(p), cos(t), sin(t)*sin(p)));
                normals.push_back(GfVec3f(sin(t)*cos(p), cos(t), sin(t)*sin(p)));
            }
        }

        for (int j = 0; j < ny; j++) {
            for (int i = 0; i < nx - 1; i++) {
                int o0 = j*nx;
                int o1 = ((j + 1) % ny)*nx;
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

    void SetLightTransform(rpr_light light, GfMatrix4f const& transform) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);
        if (!RPR_ERROR_CHECK(rprLightSetTransform(light, false, transform.GetArray()), "Fail set light transformation")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    RprApiObjectPtr CreateMaterial(const MaterialAdapter& materialAdapter) {
        if (!m_rprContext || !m_rprMaterialFactory) {
            return nullptr;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);
        auto material = m_rprMaterialFactory->CreateMaterial(materialAdapter.GetType(), materialAdapter);

        return make_unique<RprApiObject>(material, [this](void* material) {
            m_rprMaterialFactory->DeleteMaterial(static_cast<RprApiMaterial*>(material));
        });
    }

    RprApiObjectPtr CreateHeterVolume(const VtArray<float>& gridDencityData, const VtArray<size_t>& indexesDencity, const VtArray<float>& gridAlbedoData, const VtArray<unsigned int>& indexesAlbedo, const GfVec3i& grigSize) {
        if (!m_rprContext) {
            return nullptr;
        }

        rpr_hetero_volume heteroVolume = nullptr;
        if (RPR_ERROR_CHECK(rprContextCreateHeteroVolume(m_rprContext->GetHandle(), &heteroVolume), "Fail create hetero dencity volume")) return nullptr;
        auto heteroVolumeObject = RprApiObject::Wrap(heteroVolume);

        rpr_grid rprGridDencity;
        if (RPR_ERROR_CHECK(rprContextCreateGrid(m_rprContext->GetHandle(), &rprGridDencity
            , grigSize[0], grigSize[1], grigSize[2], &indexesDencity[0]
            , indexesDencity.size(), RPR_GRID_INDICES_TOPOLOGY_I_U64
            , &gridDencityData[0], gridDencityData.size() * sizeof(gridDencityData[0])
            , 0)
            , "Fail create dencity grid")) return nullptr;
        heteroVolumeObject->AttachDependency(RprApiObject::Wrap(rprGridDencity));

        rpr_grid rprGridAlbedo;
        if (RPR_ERROR_CHECK(rprContextCreateGrid(m_rprContext->GetHandle(), &rprGridAlbedo
            , grigSize[0], grigSize[1], grigSize[2], &indexesAlbedo[0]
            , indexesAlbedo.size() / 3, RPR_GRID_INDICES_TOPOLOGY_XYZ_U32
            , &gridAlbedoData[0], gridAlbedoData.size() * sizeof(gridAlbedoData[0])
            , 0)
            , "Fail create albedo grid")) return nullptr;
        heteroVolumeObject->AttachDependency(RprApiObject::Wrap(rprGridAlbedo));

        if (RPR_ERROR_CHECK(rprHeteroVolumeSetDensityGrid(heteroVolume, rprGridDencity), "Fail to set density hetero volume")) return nullptr;
        if (RPR_ERROR_CHECK(rprHeteroVolumeSetAlbedoGrid(heteroVolume, rprGridAlbedo), "Fail to set albedo hetero volume")) return nullptr;

        if (RPR_ERROR_CHECK(rprSceneAttachHeteroVolume(m_scene->GetHandle(), heteroVolume), "Fail attach hetero volume to scene")) return nullptr;
        m_dirtyFlags |= ChangeTracker::DirtyScene;

        heteroVolumeObject->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* volume) {
            if (!RPR_ERROR_CHECK(rprSceneDetachHeteroVolume(m_scene->GetHandle(), volume), "Failed to dettach hetero volume from scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        });

        return heteroVolumeObject;
    }

    void SetHeteroVolumeTransform(rpr_hetero_volume heteroVolume, const GfMatrix4f& m) {
        RPR_ERROR_CHECK(rprHeteroVolumeSetTransform(heteroVolume, false, m.GetArray()), "Fail to set hetero volume transform");
    }

    RprApiObjectPtr CreateVolume(const VtArray<float>& gridDencityData, const VtArray<size_t>& indexesDencity, const VtArray<float>& gridAlbedoData, const VtArray<unsigned int>& indexesAlbedo, const GfVec3i& grigSize, const GfVec3f& voxelSize) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        auto heteroVolume = CreateHeterVolume(gridDencityData, indexesDencity, gridAlbedoData, indexesAlbedo, grigSize);
        if (!heteroVolume) {
            return nullptr;
        }

        auto cubeMesh = CreateCubeMesh(0.5f, 0.5f, 0.5f);
        if (!cubeMesh) {
            return nullptr;
        }

        MaterialAdapter matAdapter = MaterialAdapter(EMaterialType::TRANSPERENT,
             MaterialParams{ { TfToken("color"), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f))
        } }); // TODO: use token

        auto transparentMaterial = CreateMaterial(matAdapter);
        if (!transparentMaterial) {
            return nullptr;
        }

        GfMatrix4f meshTransform(1.0f);
        GfVec3f volumeSize = GfVec3f(voxelSize[0] * grigSize[0], voxelSize[1] * grigSize[1], voxelSize[2] * grigSize[2]);
        meshTransform.SetScale(volumeSize);

        SetMeshMaterial(cubeMesh->GetHandle(), static_cast<RprApiMaterial*>(transparentMaterial->GetHandle()));
        SetMeshHeteroVolume(cubeMesh->GetHandle(), heteroVolume->GetHandle());
        SetMeshTransform(cubeMesh->GetHandle(), meshTransform);
        SetHeteroVolumeTransform(heteroVolume->GetHandle(), meshTransform);

        heteroVolume->AttachDependency(std::move(cubeMesh));
        heteroVolume->AttachDependency(std::move(transparentMaterial));

        return heteroVolume;
    }

    void CreatePosteffects() {
        if (!m_rprContext) {
            return;
        }

        if (m_rprContext->GetActivePluginType() == rpr::PluginType::TAHOE) {
            rpr_post_effect tonemap;
            if (RPR_ERROR_CHECK(rprContextCreatePostEffect(m_rprContext->GetHandle(), RPR_POST_EFFECT_TONE_MAP, &tonemap), "Fail to create post effect")) return;
            m_tonemap = RprApiObject::Wrap(tonemap);

            if (RPR_ERROR_CHECK(rprContextAttachPostEffect(m_rprContext->GetHandle(), tonemap), "Fail to attach posteffect")) return;
            m_tonemap->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* tonemap) {
                rprContextDetachPostEffect(m_rprContext->GetHandle(), tonemap);
            });
        }
    }

    void SetCameraViewMatrix(const GfMatrix4d& m) {
        if (!m_camera) {
            return;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        m_cameraViewMatrix = m;
        m_dirtyFlags |= ChangeTracker::DirtyCamera;
    }

    void SetCameraProjectionMatrix(const GfMatrix4d& proj) {
        if (!m_camera) {
            return;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        m_cameraProjectionMatrix = proj;
        m_dirtyFlags |= ChangeTracker::DirtyCamera;
    }

    const GfMatrix4d& GetCameraViewMatrix() const {
        return m_cameraViewMatrix;
    }

    const GfMatrix4d& GetCameraProjectionMatrix() const {
        return m_cameraProjectionMatrix;
    }

    bool EnableAov(TfToken const& aovName, int width, int height, HdFormat format = HdFormatInvalid, bool setAsActive = false) {
        if (!m_rprContext ||
            width < 1 || height < 1) {
            return false;
        }

        auto rprAovIt = kAovTokenToRprAov.find(aovName);
        if (rprAovIt == kAovTokenToRprAov.end()) {
            TF_WARN("Unsupported aov type: %s", aovName.GetText());
            return false;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        if (IsAovEnabled(aovName)) {
            // While usdview does not have correct AOV system
            // we have ambiguity in currently selected AOV that we can't distinguish
            if (aovName == HdRprAovTokens->depth) {
                return true;
            }

            if (setAsActive) {
                if (m_activeAov != aovName) {
                    m_dirtyFlags |= ChangeTracker::DirtyActiveAOV;
                }
                m_activeAov = aovName;
            }
            return true;
        }

        try {
            AovFrameBuffer aovFrameBuffer;
            aovFrameBuffer.format = format;

            // We compute depth from worldCoordinate in the postprocess step
            if (aovName == HdRprAovTokens->depth) {
                EnableAov(HdRprAovTokens->worldCoordinate, width, height);
            } else {
                aovFrameBuffer.aov = make_unique<rpr::FrameBuffer>(m_rprContext->GetHandle(), width, height);
                if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID && aovName == HdRprAovTokens->normal) {
                    // TODO: remove me when Hybrid gain RPR_AOV_GEOMETRIC_NORMAL support
                    aovFrameBuffer.aov->AttachAs(RPR_AOV_SHADING_NORMAL);
                } else {
                    aovFrameBuffer.aov->AttachAs(rprAovIt->second);
                }

                // XXX: Hybrid plugin does not support framebuffer resolving (rprContextResolveFrameBuffer)
                if (m_rprContext->GetActivePluginType() != rpr::PluginType::HYBRID) {
                    aovFrameBuffer.resolved = make_unique<rpr::FrameBuffer>(m_rprContext->GetHandle(), width, height);
                }
            }

            m_aovFrameBuffers.emplace(aovName, std::move(aovFrameBuffer));
            m_dirtyFlags |= ChangeTracker::DirtyAOVFramebuffers;

            if (setAsActive) {
                m_activeAov = aovName;
            }
        } catch (rpr::Error const& e) {
            TF_CODING_ERROR("Failed to enable %s AOV: %s", aovName.GetText(), e.what());
            return false;
        }

        return true;
    }

    void DisableAov(TfToken const& aovName, bool force = false) {
        // XXX: RPR bug - rprContextRender requires RPR_AOV_COLOR to be set,
        // otherwise it fails with error RPR_ERROR_INVALID_OBJECT
        if (aovName == HdRprAovTokens->color && !force) {
            return;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);

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

    GfVec2i GetAovSize(TfToken const& aovName) const {
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        auto it = m_aovFrameBuffers.find(aovName);
        if (it == m_aovFrameBuffers.end()) {
            return GfVec2i(-1, -1);
        }

        auto desc = it->second.aov->GetDesc();
        return GfVec2i(desc.fb_width, desc.fb_height);
    }

    std::shared_ptr<char> GetAovData(TfToken const& aovName, std::shared_ptr<char> buffer, size_t* bufferSize) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        auto it = m_aovFrameBuffers.find(aovName);
        if (it == m_aovFrameBuffers.end()) {
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
                rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_RENDER_QUALITY, int(preferences.GetHybridQuality()));
            }
        }

        // In case there is no Lights in scene - create default
        if (!m_isLightPresent) {
            const GfVec3f k_defaultLightColor(0.5f, 0.5f, 0.5f);
            m_defaultLightObject = CreateEnvironmentLight(k_defaultLightColor, 1.f);
        }

        UpdateCamera();
        UpdateSampling();
        UpdateDenoiseFilter();

        for (auto& aovFrameBufferEntry : m_aovFrameBuffers) {
            auto& aovFrameBuffer = aovFrameBufferEntry.second;
            if (aovFrameBuffer.isDirty) {
                aovFrameBuffer.isDirty = false;

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

                    auto fbDesc = worldCoordinateAovFb.aov->GetDesc();
                    ndcDepthFilter->SetOutput(GetRifImageDesc(fbDesc.fb_width, fbDesc.fb_height, aovFrameBuffer.format));

                    auto viewProjectionMatrix = m_cameraViewMatrix * m_cameraProjectionMatrix;
                    ndcDepthFilter->SetParam("viewProjMatrix", GfMatrix4f(viewProjectionMatrix.GetTranspose()));

                    aovFrameBuffer.postprocessFilter = std::move(ndcDepthFilter);
                } else if (aovFrameBufferEntry.first == HdRprAovTokens->normal &&
                           aovFrameBuffer.format != HdFormatInvalid) {
                    // Remap normal to [-1;1] range
                    auto remapFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_REMAP_RANGE, m_rifContext.get());

                    auto inputRprFrameBuffer = (aovFrameBuffer.resolved ? aovFrameBuffer.resolved : aovFrameBuffer.aov).get();
                    remapFilter->SetInput(rif::Color, inputRprFrameBuffer, 1.0f);

                    auto fbDesc = aovFrameBuffer.aov->GetDesc();
                    remapFilter->SetOutput(GetRifImageDesc(fbDesc.fb_width, fbDesc.fb_height, aovFrameBuffer.format));

                    remapFilter->SetParam("srcRangeAuto", 0);
                    remapFilter->SetParam("dstLo", -1.0f);
                    remapFilter->SetParam("dstHi", 1.0f);

                    aovFrameBuffer.postprocessFilter = std::move(remapFilter);
                } else if (aovFrameBuffer.format != HdFormatInvalid &&
                           aovFrameBuffer.format != HdFormatFloat32Vec4) {
                    // Convert from RPR native to Hydra format
                    auto inputRprFrameBuffer = (aovFrameBuffer.resolved ? aovFrameBuffer.resolved : aovFrameBuffer.aov).get();
                    auto fbDesc = aovFrameBuffer.aov->GetDesc();
                    rif_image_desc imageDesc = GetRifImageDesc(fbDesc.fb_width, fbDesc.fb_height, aovFrameBuffer.format);
                    if (inputRprFrameBuffer && imageDesc.type != 0 && imageDesc.num_components != 0) {
                        auto converter = rif::Filter::Create(rif::FilterType::Resample, m_rifContext.get(), fbDesc.fb_width, fbDesc.fb_height);
                        converter->SetInput(rif::Color, inputRprFrameBuffer, 1.0f);
                        converter->SetOutput(imageDesc);

                        aovFrameBuffer.postprocessFilter = std::move(converter);
                    }
                }
            } else if (aovFrameBufferEntry.first == HdRprAovTokens->depth &&
                       (m_dirtyFlags & ChangeTracker::DirtyCamera)) {
                if (auto ndcDepthFilter = aovFrameBuffer.postprocessFilter.get()) {
                    auto viewProjectionMatrix = m_cameraViewMatrix * m_cameraProjectionMatrix;
                    ndcDepthFilter->SetParam("viewProjMatrix", GfMatrix4f(viewProjectionMatrix.GetTranspose()));
                }
            }
        }

        if (m_dirtyFlags & ChangeTracker::DirtyScene ||
            m_dirtyFlags & ChangeTracker::DirtyAOVFramebuffers ||
            m_dirtyFlags & ChangeTracker::DirtyCamera) {
            m_iter = 0;
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

    void UpdateSampling() {
        auto& preferences = HdRprConfig::GetInstance();
        if (preferences.IsDirty(HdRprConfig::DirtySampling)) {
            preferences.CleanDirtyFlag(HdRprConfig::DirtySampling);

            m_maxSamples = preferences.GetMaxSamples();
            if (m_maxSamples < m_iter) {
                // Force framebuffers clear to render required number of samples
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }

            RPR_ERROR_CHECK(rprContextSetParameterByKey1f(m_rprContext->GetHandle(), RPR_CONTEXT_ADAPTIVE_SAMPLING_THRESHOLD, preferences.GetVariance()), "Failed to set as.threshold");
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_ADAPTIVE_SAMPLING_MIN_SPP, preferences.GetMinSamples()), "Failed to set as.minspp");
        }
    }

    void UpdateCamera() {
        if ((m_dirtyFlags & ChangeTracker::DirtyCamera) == 0) {
            return;
        }

        auto camera = m_camera->GetHandle();
        auto iwvm = m_cameraViewMatrix.GetInverse();
        auto& wvm = m_cameraViewMatrix;

        GfVec3f eye(iwvm[3][0], iwvm[3][1], iwvm[3][2]);
        GfVec3f up(wvm[0][1], wvm[1][1], wvm[2][1]);
        GfVec3f n(wvm[0][2], wvm[1][2], wvm[2][2]);
        GfVec3f at(eye - n);
        RPR_ERROR_CHECK(rprCameraLookAt(camera, eye[0], eye[1], eye[2], at[0], at[1], at[2], up[0], up[1], up[2]), "Fail to set camera Look At");

        bool isOrthographic = round(m_cameraProjectionMatrix[3][3]) == 1.0;
        if (isOrthographic) {
            GfVec3f ndcTopLeft(-1.0f, 1.0f, 0.0f);
            GfVec3f nearPlaneTrace = m_cameraProjectionMatrix.GetInverse().Transform(ndcTopLeft);

            auto orthoWidth = std::abs(nearPlaneTrace[0]) * 2.0;
            auto orthoHeight = std::abs(nearPlaneTrace[1]) * 2.0;

            RPR_ERROR_CHECK(rprCameraSetMode(camera, RPR_CAMERA_MODE_ORTHOGRAPHIC), "Failed to set camera mode");
            RPR_ERROR_CHECK(rprCameraSetOrthoWidth(camera, orthoWidth), "Failed to set camera ortho width");
            RPR_ERROR_CHECK(rprCameraSetOrthoHeight(camera, orthoHeight), "Failed to set camera ortho height");
        } else {
            auto fbDesc = m_aovFrameBuffers.begin()->second.aov->GetDesc();
            auto ratio = double(fbDesc.fb_width) / fbDesc.fb_height;
            auto focalLength = m_cameraProjectionMatrix[1][1] / (2.0 * ratio);
            auto sensorWidth = 1.0f;
            auto sensorHeight = 1.0f / ratio;
            if (ratio < 1.0f) {
                sensorWidth = ratio;
                sensorHeight = 1.0f;
            }

            RPR_ERROR_CHECK(rprCameraSetMode(camera, RPR_CAMERA_MODE_PERSPECTIVE), "Failed to set camera mode");
            RPR_ERROR_CHECK(rprCameraSetFocalLength(camera, focalLength), "Fail to set camera focal length");
            RPR_ERROR_CHECK(rprCameraSetSensorSize(camera, sensorWidth, sensorHeight), "Failed to set camera sensor size");
        }
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
        auto& colorAovFb = m_aovFrameBuffers[HdRprAovTokens->color];
        auto fbDesc = colorAovFb.aov->GetDesc();
        m_denoiseFilterPtr = rif::Filter::Create(filterType, m_rifContext.get(), fbDesc.fb_width, fbDesc.fb_height);

        switch (filterType) {
        case rif::FilterType::AIDenoise: {
            EnableAov(HdRprAovTokens->albedo, fbDesc.fb_width, fbDesc.fb_height);
            EnableAov(HdRprAovTokens->linearDepth, fbDesc.fb_width, fbDesc.fb_height);
            EnableAov(HdRprAovTokens->normal, fbDesc.fb_width, fbDesc.fb_height);

            m_denoiseFilterPtr->SetInput(rif::Color, colorAovFb.resolved.get(), 1.0f);
            m_denoiseFilterPtr->SetInput(rif::Normal, m_aovFrameBuffers[HdRprAovTokens->normal].resolved.get(), 1.0f);
            m_denoiseFilterPtr->SetInput(rif::Depth, m_aovFrameBuffers[HdRprAovTokens->linearDepth].resolved.get(), 1.0f);
            m_denoiseFilterPtr->SetInput(rif::Albedo, m_aovFrameBuffers[HdRprAovTokens->albedo].resolved.get(), 1.0f);
            break;
        }
        case rif::FilterType::EawDenoise: {
            EnableAov(HdRprAovTokens->albedo, fbDesc.fb_width, fbDesc.fb_height);
            EnableAov(HdRprAovTokens->linearDepth, fbDesc.fb_width, fbDesc.fb_height);
            EnableAov(HdRprAovTokens->normal, fbDesc.fb_width, fbDesc.fb_height);
            EnableAov(HdRprAovTokens->primId, fbDesc.fb_width, fbDesc.fb_height);
            EnableAov(HdRprAovTokens->worldCoordinate, fbDesc.fb_width, fbDesc.fb_height);

            m_denoiseFilterPtr->SetInput(rif::Color, colorAovFb.resolved.get(), 1.0f);
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

        m_denoiseFilterPtr->SetOutput(GetRifImageDesc(fbDesc.fb_width, fbDesc.fb_height, colorAovFb.format));
    }

    void Render() {
        if (!m_rprContext || m_aovFrameBuffers.empty()) {
            return;
        }
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        Update();
        if (IsConverged()) {
            return;
        }

        if (RPR_ERROR_CHECK(rprContextRender(m_rprContext->GetHandle()), "Fail contex render framebuffer")) return;

        ResolveFramebuffers();

        try {
            m_rifContext->ExecuteCommandQueue();
        } catch (std::runtime_error& e) {
            TF_RUNTIME_ERROR("%s", e.what());
        }

        m_iter++;
    }

    bool IsConverged() { 
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        // return Converged if max samples is reached
        if (m_iter > m_maxSamples) {
            return true;
        } else {
            // if not max samples check if all pixels converged to threshold
            auto& preferences = HdRprConfig::GetInstance();

            float varianceSetting = preferences.GetVariance();
            if (varianceSetting > 0.0f) {
                int activePixels = 0;
                if (RPR_ERROR_CHECK(rprContextGetInfo(m_rprContext->GetHandle(), RPR_CONTEXT_ACTIVE_PIXEL_COUNT, sizeof(activePixels), &activePixels, NULL), "Failed to query active pixels")) return false;
                return activePixels == 0;
            }
            return false;
        }
    }

    void DeleteMesh(void* mesh) {
        if (!mesh) {
            return;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        RPR_ERROR_CHECK(rprShapeSetMaterial(mesh, nullptr), "Fail reset mesh material");
        RPR_ERROR_CHECK(rprSceneDetachShape(m_scene->GetHandle(), mesh), "Fail detach mesh from scene");

        rprObjectDelete(mesh);
    }

    void DeleteInstance(void* instance) {
        if (!instance) {
            return;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);
        rprObjectDelete(instance);
    }

    TfToken const& GetActiveAov() const {
        return m_activeAov;
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

        RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_Y_FLIP, 0), "Fail to set context YFLIP parameter");
        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_RENDER_QUALITY, int(HdRprConfig::GetInstance().GetHybridQuality())), "Fail to set context hybrid render quality");
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

        rpr_material_system matsys;
        if (RPR_ERROR_CHECK(rprContextCreateMaterialSystem(m_rprContext->GetHandle(), 0, & matsys), "Fail create Material System resolve")) return;
        m_matsys = RprApiObject::Wrap(matsys);
        m_rprMaterialFactory.reset(new RprMaterialFactory(matsys, m_rprContext->GetHandle()));
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
                const int commonVertex = *idxIt;
                constexpr int triangleVertexCount = 3;
                for (int i = 1; i < vCount - 1; ++i) {
                    out_newIndexes.push_back(commonVertex);
                    out_newIndexes.push_back(*(idxIt + i + 0));
                    out_newIndexes.push_back(*(idxIt + i + 1));
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
                const int commonVertex = *idxIt;
                for (int i = 1; i < vCount - 1; ++i) {
                    out_newIndexes.push_back(commonVertex);
                    out_newIndexes.push_back(*(idxIt + i + 0));
                    out_newIndexes.push_back(*(idxIt + i + 1));
                }
                idxIt += vCount;
            }
        }
    }

    RprApiObjectPtr CreateCubeMesh(const float & width, const float & height, const float & depth) {
        constexpr const size_t cubeVertexCount = 24;
        constexpr const size_t cubeNormalCount = 24;
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
        DirtyAOVFramebuffers = 1 << 2,
        DirtyCamera = 1 << 3
    };
    uint32_t m_dirtyFlags = ChangeTracker::AllDirty;

    std::unique_ptr<rpr::Context> m_rprContext;
    std::unique_ptr<rif::Context> m_rifContext;
    RprApiObjectPtr m_scene;
    RprApiObjectPtr m_camera;
    RprApiObjectPtr m_tonemap;
    RprApiObjectPtr m_matsys;
    std::unique_ptr<RprMaterialFactory> m_rprMaterialFactory;

    struct AovFrameBuffer {
        std::unique_ptr<rpr::FrameBuffer> aov;
        std::unique_ptr<rpr::FrameBuffer> resolved;
        std::unique_ptr<rif::Filter> postprocessFilter;
        HdFormat format = HdFormatInvalid;
        bool isDirty = true;
    };
    std::map<TfToken, AovFrameBuffer> m_aovFrameBuffers;
    TfToken m_activeAov;

    GfMatrix4d m_cameraViewMatrix = GfMatrix4d(1.f);
    GfMatrix4d m_cameraProjectionMatrix = GfMatrix4d(1.f);

    bool m_isLightPresent = false;
    RprApiObjectPtr m_defaultLightObject;

    std::unique_ptr<rif::Filter> m_denoiseFilterPtr;
};

std::unique_ptr<RprApiObject> RprApiObject::Wrap(void* handle) {
    return make_unique<RprApiObject>(handle);
}

void DefaultRprApiObjectDeleter(void* handle) {
    RPR_ERROR_CHECK(rprObjectDelete(handle), "Failed to release rpr object");
}

RprApiObject::RprApiObject(void* handle) : RprApiObject(handle, DefaultRprApiObjectDeleter) {

}

RprApiObject::RprApiObject(void* handle, std::function<void (void*)> deleter)
    : m_handle(handle)
    , m_deleter(deleter) {

}

RprApiObject::~RprApiObject() {
    RecursiveLockGuard rprLock(g_rprAccessMutex);

    for (auto& onReleaseActionEntry : m_onReleaseActions) {
        if (onReleaseActionEntry.second) {
            onReleaseActionEntry.second(m_handle);
        }
    }
    m_dependencyObjects.clear();
    if (m_deleter && m_handle) {
        m_deleter(m_handle);
    }
}

void RprApiObject::AttachDependency(std::unique_ptr<rpr::Object>&& dependencyObject) {
    m_dependencyRprObjects.push_back(std::move(dependencyObject));
}

void RprApiObject::AttachDependency(RprApiObjectPtr&& dependencyObject) {
    m_dependencyObjects.push_back(std::move(dependencyObject));
}

void RprApiObject::AttachOnReleaseAction(TfToken const& actionName, std::function<void(void*)> action) {
    TF_VERIFY(m_onReleaseActions.count(actionName) == 0);
    m_onReleaseActions.emplace(actionName, std::move(action));
}

void RprApiObject::DettachOnReleaseAction(TfToken const& actionName) {
    m_onReleaseActions.erase(actionName);
}

void* RprApiObject::GetHandle() const {
    return m_handle;
}

HdRprApi::HdRprApi() : m_impl(new HdRprApiImpl) {

}

HdRprApi::~HdRprApi() {
    delete m_impl;
}

TfToken const& HdRprApi::GetActiveAov() const {
    return m_impl->GetActiveAov();
}

RprApiObjectPtr HdRprApi::CreateMesh(const VtVec3fArray& points, const VtIntArray& pointIndexes, const VtVec3fArray& normals, const VtIntArray& normalIndexes, const VtVec2fArray& uv, const VtIntArray& uvIndexes, const VtIntArray& vpf) {
    return m_impl->CreateMesh(points, pointIndexes, normals, normalIndexes, uv, uvIndexes, vpf);
}

RprApiObjectPtr HdRprApi::CreateCurve(const VtVec3fArray& points, const VtIntArray& indexes, float width) {
    return m_impl->CreateCurve(points, indexes, width);
}

RprApiObjectPtr HdRprApi::CreateMeshInstance(RprApiObject* prototypeMesh) {
    return m_impl->CreateMeshInstance(prototypeMesh->GetHandle());
}

RprApiObjectPtr HdRprApi::CreateEnvironmentLight(GfVec3f color, float intensity) {
    return m_impl->CreateEnvironmentLight(color, intensity);
}

RprApiObjectPtr HdRprApi::CreateEnvironmentLight(const std::string& prthTotexture, float intensity) {
    return m_impl->CreateEnvironmentLight(prthTotexture, intensity);
}

RprApiObjectPtr HdRprApi::CreateRectLightMesh(float width, float height) {
    return m_impl->CreateRectLightMesh(width, height);
}

RprApiObjectPtr HdRprApi::CreateSphereLightMesh(float radius) {
    return m_impl->CreateSphereLightMesh(radius);
}

RprApiObjectPtr HdRprApi::CreateDiskLightMesh(float width, float height, const GfVec3f& emmisionColor) {
    return m_impl->CreateDiskLightMesh(width, height, emmisionColor);
}

void HdRprApi::SetLightTransform(RprApiObject* light, GfMatrix4d const& transform) {
    GfMatrix4f transformF(transform);
    m_impl->SetLightTransform(light->GetHandle(), transformF);
}

RprApiObjectPtr HdRprApi::CreateVolume(const VtArray<float>& gridDencityData, const VtArray<size_t>& indexesDencity, const VtArray<float>& gridAlbedoData, const VtArray<unsigned int>& indexesAlbedo, const GfVec3i& gridSize, const GfVec3f& voxelSize) {
    return m_impl->CreateVolume(gridDencityData, indexesDencity, gridAlbedoData, indexesAlbedo, gridSize, voxelSize);
}

RprApiObjectPtr HdRprApi::CreateMaterial(MaterialAdapter& materialAdapter) {
    return m_impl->CreateMaterial(materialAdapter);
}

void HdRprApi::SetMeshTransform(RprApiObject* mesh, const GfMatrix4d& transform) {
    GfMatrix4f transformF(transform);
    m_impl->SetMeshTransform(mesh->GetHandle(), transformF);
}

void HdRprApi::SetMeshRefineLevel(RprApiObject* mesh, int level, TfToken boundaryInterpolation) {
    m_impl->SetMeshRefineLevel(mesh->GetHandle(), level, boundaryInterpolation);
}

void HdRprApi::SetMeshMaterial(RprApiObject* mesh, RprApiObject const* material) {
    m_impl->SetMeshMaterial(mesh->GetHandle(), static_cast<RprApiMaterial*>(material->GetHandle()));
}

void HdRprApi::SetMeshVisibility(RprApiObject* mesh, bool isVisible) {
    m_impl->SetMeshVisibility(mesh->GetHandle(), isVisible);
}

void HdRprApi::SetCurveMaterial(RprApiObject* curve, RprApiObject const* material) {
    m_impl->SetCurveMaterial(curve->GetHandle(), static_cast<RprApiMaterial*>(material->GetHandle()));
}

const GfMatrix4d& HdRprApi::GetCameraViewMatrix() const {
    return m_impl->GetCameraViewMatrix();
}

const GfMatrix4d& HdRprApi::GetCameraProjectionMatrix() const {
    return m_impl->GetCameraProjectionMatrix();
}

void HdRprApi::SetCameraViewMatrix(const GfMatrix4d& m) {
    m_impl->SetCameraViewMatrix(m);
}

void HdRprApi::SetCameraProjectionMatrix(const GfMatrix4d& m) {
    m_impl->SetCameraProjectionMatrix(m);
}

bool HdRprApi::EnableAov(TfToken const& aovName, int width, int height, HdFormat format) {
    return m_impl->EnableAov(aovName, width, height, format, true);
}

void HdRprApi::DisableAov(TfToken const& aovName) {
    m_impl->DisableAov(aovName);
}

bool HdRprApi::IsAovEnabled(TfToken const& aovName) {
    return m_impl->IsAovEnabled(aovName);
}

GfVec2i HdRprApi::GetAovSize(TfToken const& aovName) const {
    return m_impl->GetAovSize(aovName);
}

std::shared_ptr<char> HdRprApi::GetAovData(TfToken const& aovName, std::shared_ptr<char> buffer, size_t* bufferSize) {
    return m_impl->GetAovData(aovName, buffer, bufferSize);
}

void HdRprApi::Render() {
    m_impl->Render();
}

bool HdRprApi::IsConverged() {
    return m_impl->IsConverged();
}

bool HdRprApi::IsGlInteropEnabled() const {
    return m_impl->IsGlInteropEnabled();
}

const char* HdRprApi::GetTmpDir() {
    return rpr::Context::GetCachePath();
}

PXR_NAMESPACE_CLOSE_SCOPE
