#include "rprApi.h"
#include "rprApiAov.h"
#include "rprcpp/rprFramebufferGL.h"
#include "rprcpp/rprContext.h"
#include "rprcpp/rprImage.h"
#include "rifcpp/rifFilter.h"
#include "rifcpp/rifImage.h"
#include "rifcpp/rifError.h"

#include "config.h"
#include "imageCache.h"
#include "material.h"
#include "materialFactory.h"
#include "materialAdapter.h"
#include "renderBuffer.h"
#include "renderParam.h"

#include "pxr/base/gf/math.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/thisPlugin.h"
#include "pxr/imaging/glf/uvTextureData.h"

#include <RadeonProRender.h>
#include <RadeonProRender_Baikal.h>
#include <fstream>
#include <vector>
#include <mutex>

#ifdef WIN32
#include <shlobj_core.h>
#pragma comment(lib,"Shell32.lib")
#elif defined(__linux__)
#include <limits.h>
#include <sys/stat.h>
#endif // __APPLE__

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdRprAovTokens, HD_RPR_AOV_TOKENS);

TF_DEFINE_PRIVATE_TOKENS(RprApiObjectActionTokens,
    (attach) \
    (contextSetScene)
);

namespace {

using RecursiveLockGuard = std::lock_guard<std::recursive_mutex>;
std::recursive_mutex g_rprAccessMutex;

bool ArchCreateDirectory(const char* path) {
#ifdef WIN32
    return CreateDirectory(path, NULL) == TRUE;
#else
    return mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0;
#endif
}

template <typename T>
struct RenderSetting {
    T value;
    bool isDirty;
};

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
    HdRprApiImpl(HdRenderDelegate* delegate)
        : m_delegate(delegate) {
        // Postpone initialization as further as possible to allow Hydra user to set custom render settings before creating a context
        //InitIfNeeded();
    }

    void InitIfNeeded() {
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        if (m_state != kStateUninitialized) {
            return;
        }
        m_state = kStateRender;

        InitRpr();
        InitRif();
        InitMaterialSystem();
        CreateScene();
        CreateCamera();
    }

    void CreateScene() {
        if (!m_rprContext) {
            return;
        }

        rpr_scene scene;
        if (RPR_ERROR_CHECK(rprContextCreateScene(m_rprContext->GetHandle(), &scene), "Fail to create scene")) return;
        m_scene = RprApiObject::Wrap(scene);

        if (RPR_ERROR_CHECK(rprContextSetScene(m_rprContext->GetHandle(), scene), "Fail to set scene")) return;
        m_scene->AttachOnReleaseAction(RprApiObjectActionTokens->contextSetScene, [this](void* scene) {
            if (m_rprContext->GetActivePluginType() != rpr::PluginType::HYBRID) {
                if (!RPR_ERROR_CHECK(rprContextSetScene(m_rprContext->GetHandle(), nullptr), "Failed to unset scene")) {
                    m_dirtyFlags |= ChangeTracker::DirtyScene;
                }
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

    RprApiObjectPtr CreateMesh(const VtVec3fArray& points, const VtIntArray& pointIndexes,
                               VtVec3fArray normals, const VtIntArray& normalIndexes,
                               VtVec2fArray uvs, const VtIntArray& uvIndexes,
                               const VtIntArray& vpf, TfToken const& polygonWinding = HdTokens->rightHanded) {
        if (!m_rprContext) {
            return nullptr;
        }

        VtIntArray newIndexes, newVpf;
        SplitPolygons(pointIndexes, vpf, newIndexes, newVpf);
        ConvertIndices(&newIndexes, newVpf, polygonWinding);

        VtIntArray newNormalIndexes;
        if (normals.empty()) {
            if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
                // XXX (Hybrid): we need to generate geometry normals by ourself
                normals.reserve(newVpf.size());
                newNormalIndexes.clear();
                newNormalIndexes.reserve(newIndexes.size());

                size_t indicesOffset = 0u;
                for (auto numVerticesPerFace : newVpf) {
                    for (int i = 0; i < numVerticesPerFace; ++i) {
                        newNormalIndexes.push_back(normals.size());
                    }

                    auto indices = &newIndexes[indicesOffset];
                    indicesOffset += numVerticesPerFace;

                    auto p0 = points[indices[0]];
                    auto p1 = points[indices[1]];
                    auto p2 = points[indices[2]];

                    auto e0 = p0 - p1;
                    auto e1 = p2 - p1;

                    auto normal = GfCross(e1, e0);
                    GfNormalize(&normal);
                    normals.push_back(normal);
                }
            }
        } else {
            if (!normalIndexes.empty()) {
                SplitPolygons(normalIndexes, vpf, newNormalIndexes);
                ConvertIndices(&newNormalIndexes, newVpf, polygonWinding);
            } else if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
                // XXX: Hybrid should interpret normals w/o indices as vertex interpolated
                newNormalIndexes = newIndexes;
            }
        }

        VtIntArray newUvIndexes;
        if (uvs.empty()) {
            if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
                newUvIndexes = newIndexes;
                uvs = VtVec2fArray(points.size(), GfVec2f(0.0f));
            }
        } else {
            if (!uvIndexes.empty()) {
                SplitPolygons(uvIndexes, vpf, newUvIndexes);
                ConvertIndices(&newUvIndexes, newVpf, polygonWinding);
            } else if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
                // XXX: Hybrid should interpret uvs w/o indices as vertex interpolated
                newUvIndexes = newIndexes;
            }
        }

        auto normalIndicesData = !newNormalIndexes.empty() ? newNormalIndexes.data() : newIndexes.data();
        if (normals.empty()) {
            normalIndicesData = nullptr;
        }

        auto uvIndicesData = !newUvIndexes.empty() ? newUvIndexes.data() : uvIndexes.data();
        if (uvs.empty()) {
            uvIndicesData = nullptr;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        rpr_shape mesh = nullptr;
        if (RPR_ERROR_CHECK(rprContextCreateMesh(
            m_rprContext->GetHandle(),
            (rpr_float const*)points.data(), points.size(), sizeof(GfVec3f),
            (rpr_float const*)(normals.data()), normals.size(), sizeof(GfVec3f),
            (rpr_float const*)(uvs.data()), uvs.size(), sizeof(GfVec2f),
            newIndexes.data(), sizeof(rpr_int),
            normalIndicesData, sizeof(rpr_int),
            uvIndicesData, sizeof(rpr_int),
            newVpf.data(), newVpf.size(), &mesh), "Fail create mesh")) {
            return nullptr;
        }
        auto meshObject = RprApiObject::Wrap(mesh);

        if (RPR_ERROR_CHECK(rprSceneAttachShape(m_scene->GetHandle(), mesh), "Fail attach mesh to scene")) {
            return nullptr;
        }
        meshObject->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* mesh) {
            if (!RPR_ERROR_CHECK(rprSceneDetachShape(m_scene->GetHandle(), mesh), "Failed to detach mesh from scene")) {
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

    void SetMeshRefineLevel(rpr_shape mesh, const int level) {
        if (!m_rprContext) {
            return;
        }

        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            // Not supported
            return;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        bool dirty = true;

        size_t dummy;
        int oldLevel;
        if (!RPR_ERROR_CHECK(rprShapeGetInfo(mesh, RPR_SHAPE_SUBDIVISION_FACTOR, sizeof(oldLevel), &oldLevel, &dummy), "Failed to query mesh subdivision factor")) {
            dirty = level != oldLevel;
        }

        if (dirty) {
            if (RPR_ERROR_CHECK(rprShapeSetSubdivisionFactor(mesh, level), "Fail set mesh subdividion")) return;
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void SetMeshVertexInterpolationRule(rpr_shape mesh, TfToken const& boundaryInterpolation) {
        if (!m_rprContext) {
            return;
        }

        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            // Not supported
            return;
        }

        rpr_subdiv_boundary_interfop_type newInterfopType = boundaryInterpolation == PxOsdOpenSubdivTokens->edgeAndCorner ?
            RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_AND_CORNER :
            RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_ONLY;

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        bool dirty = true;

        size_t dummy;
        rpr_subdiv_boundary_interfop_type interfopType;
        if (!RPR_ERROR_CHECK(rprShapeGetInfo(mesh, RPR_SHAPE_SUBDIVISION_BOUNDARYINTEROP, sizeof(interfopType), &interfopType, &dummy), "Failed to query mesh subdivision interfopType")) {
            dirty = newInterfopType != interfopType;
        }

        if (dirty) {
            if (RPR_ERROR_CHECK(rprShapeSetSubdivisionBoundaryInterop(mesh, newInterfopType), "Fail set mesh subdividion boundary")) return;
            m_dirtyFlags |= ChangeTracker::DirtyScene;
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

    void SetCurveMaterial(rpr_curve curve, const RprApiMaterial* material) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);
        m_rprMaterialFactory->AttachMaterialToCurve(curve, material);
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    void SetCurveTransform(rpr_curve curve, GfMatrix4f const& transform) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);
        if (!RPR_ERROR_CHECK(rprCurveSetTransform(curve, false, transform.GetArray()), "Fail set curve transformation")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

#if 0
    void SetCurveVisibility(rpr_curve curve, bool isVisible) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        if (!RPR_ERROR_CHECK(rprCurveSetVisibility(curve, isVisible), "Fail to set curve visibility")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }
#else
    void AttachCurveToScene(RprApiObject* curve) {
        if (curve->HasOnReleaseAction(RprApiObjectActionTokens->attach)) {
            return;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);
        if (!RPR_ERROR_CHECK(rprSceneAttachCurve(m_scene->GetHandle(), curve->GetHandle()), "Failed to attach curve to scene")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;

            curve->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* curve) {
                if (!RPR_ERROR_CHECK(rprSceneDetachCurve(m_scene->GetHandle(), curve), "Failed to detach curve from scene")) {
                    m_dirtyFlags |= ChangeTracker::DirtyScene;
                }
            });
        }
    }

    void DetachCurveFromScene(RprApiObject* curve) {
        if (!curve->HasOnReleaseAction(RprApiObjectActionTokens->attach)) {
            return;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);
        if (!RPR_ERROR_CHECK(rprSceneDetachCurve(m_scene->GetHandle(), curve->GetHandle()), "Failed to detach curve from scene")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;

            curve->DetachOnReleaseAction(RprApiObjectActionTokens->attach);
        }
    }
#endif

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
            if (!RPR_ERROR_CHECK(rprSceneDetachShape(m_scene->GetHandle(), instance), "Failed to detach mesh instance from scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        });

        return meshInstanceObject;
    }

    void SetMeshVisibility(RprApiObject* mesh, bool isVisible) {
        if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            // XXX (Hybrid): rprShapeSetVisibility not supported, emulate visibility using attach/detach
            if (isVisible) {
                if (mesh->HasOnReleaseAction(RprApiObjectActionTokens->attach)) {
                    return;
                }

                RecursiveLockGuard rprLock(g_rprAccessMutex);
                if (!RPR_ERROR_CHECK(rprSceneAttachShape(m_scene->GetHandle(), mesh->GetHandle()), "Failed to attach shape to scene")) {
                    m_dirtyFlags |= ChangeTracker::DirtyScene;

                    mesh->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* mesh) {
                        if (!RPR_ERROR_CHECK(rprSceneDetachShape(m_scene->GetHandle(), mesh), "Failed to detach mesh from scene")) {
                            m_dirtyFlags |= ChangeTracker::DirtyScene;
                        }
                    });
                }
            } else {
                if (!mesh->HasOnReleaseAction(RprApiObjectActionTokens->attach)) {
                    return;
                }

                RecursiveLockGuard rprLock(g_rprAccessMutex);
                if (!RPR_ERROR_CHECK(rprSceneDetachShape(m_scene->GetHandle(), mesh->GetHandle()), "Failed to detach mesh from scene")) {
                    m_dirtyFlags |= ChangeTracker::DirtyScene;

                    mesh->DetachOnReleaseAction(RprApiObjectActionTokens->attach);
                }
            }
        } else {
            RecursiveLockGuard rprLock(g_rprAccessMutex);
            if (!RPR_ERROR_CHECK(rprShapeSetVisibility(mesh->GetHandle(), isVisible), "Fail to set mesh visibility")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
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
        if (RPR_ERROR_CHECK(rprContextCreateCurve(
            m_rprContext->GetHandle(), &curve
            , newPoints.size(), (float*)newPoints.data(), sizeof(GfVec3f)
            , newIndexes.size(), 1, (const rpr_uint*)newIndexes.data()
            , &width, nullptr, segmentsPerCurve.data(), 0), "Fail to create curve")) {
            return nullptr;
        }
        auto curveObject = RprApiObject::Wrap(curve);

        if (RPR_ERROR_CHECK(rprSceneAttachCurve(m_scene->GetHandle(), curve), "Fail to attach curve")) {
            return nullptr;
        }
        m_dirtyFlags |= ChangeTracker::DirtyScene;

        curveObject->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* curve) {
            if (!RPR_ERROR_CHECK(rprSceneDetachCurve(m_scene->GetHandle(), curve), "Failed to detach curve from scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        });

        return curveObject;
    }

    RprApiObjectPtr CreateDirectionalLight() {
        if (!m_rprContext) {
            return nullptr;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        rpr_light light;
        if (RPR_ERROR_CHECK(rprContextCreateDirectionalLight(m_rprContext->GetHandle(), &light), "Failed to create directional light")) {
            return nullptr;
        }
        auto lightObject = RprApiObject::Wrap(light);

        if (RPR_ERROR_CHECK(rprSceneAttachLight(m_scene->GetHandle(), light), "Failed to attach directional light to scene")) {
            return nullptr;
        }
        m_dirtyFlags |= ChangeTracker::DirtyScene;
        lightObject->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* light) {
            if (!RPR_ERROR_CHECK(rprSceneDetachLight(m_scene->GetHandle(), light), "Failed to detach directional light from scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        });

        m_isLightPresent = true;

        return lightObject;
    }

    void SetDirectionalLightAttributes(rpr_light light, GfVec3f const& color, float shadowSoftness) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        RPR_ERROR_CHECK(rprDirectionalLightSetRadiantPower3f(light, color[0], color[1], color[2]), "Failed to set directional light color");
        RPR_ERROR_CHECK(rprDirectionalLightSetShadowSoftness(light, GfClamp(shadowSoftness, 0.0f, 1.0f)), "Failed to set directional light color");
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
                if (!RPR_ERROR_CHECK(rprSceneDetachLight(m_scene->GetHandle(), light), "Fail to detach environment light")) {
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
            auto image = pxr::make_unique<rpr::Image>(m_rprContext->GetHandle(), path.c_str());
            return CreateEnvironmentLight(std::move(image), intensity);
        } catch (rpr::Error const& error) {
            TF_RUNTIME_ERROR("Failed to create environment light: %s", error.what());
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
            auto image = pxr::make_unique<rpr::Image>(m_rprContext->GetHandle(), imageSize, imageSize, format, imageData[0].data());
            return CreateEnvironmentLight(std::move(image), intensity);
        } catch (rpr::Error const& error) {
            TF_RUNTIME_ERROR("Failed to create environment light: %s", error.what());
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

    RprApiObjectPtr CreateDiskLightMesh(float radius) {
        constexpr uint32_t k_diskVertexCount = 32;

        VtVec3fArray points;
        VtVec3fArray normals;
        VtIntArray pointIndices;
        VtIntArray normalIndices(k_diskVertexCount * 3, 0);
        VtIntArray vpf(k_diskVertexCount, 3);

        points.reserve(k_diskVertexCount + 1);
        pointIndices.reserve(k_diskVertexCount * 3);

        const double step = M_PI * 2.0 / k_diskVertexCount;
        for (int i = 0; i < k_diskVertexCount; ++i) {
            double angle = step * i;
            points.push_back(GfVec3f(radius * cos(angle), radius * sin(angle), 0.0f));
        }
        const int centerPointIndex = points.size();
        points.push_back(GfVec3f(0.0f));

        normals.push_back(GfVec3f(0.0f, 0.0f, -1.0f));

        for (int i = 0; i < k_diskVertexCount; ++i) {
            pointIndices.push_back(i);
            pointIndices.push_back((i + 1) % k_diskVertexCount);
            pointIndices.push_back(centerPointIndex);
        }

        m_isLightPresent = true;

        return CreateMesh(points, pointIndices, normals, normalIndices, VtVec2fArray(), VtIntArray(), vpf);
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

    RprApiObjectPtr CreateCylinderLightMesh(float radius, float length) {
        constexpr int numPointsAtCap = 36;

        VtVec3fArray points;
        VtVec3fArray normals;
        VtIntArray pointIndices;
        VtIntArray normalIndices;
        VtIntArray vpf;

        points.reserve(numPointsAtCap * 2 + 2);
        normals.reserve(numPointsAtCap + 2);
        vpf.reserve(numPointsAtCap * 3);
        pointIndices.reserve(numPointsAtCap * 4 + 2 * (numPointsAtCap * 3));
        normalIndices.reserve(numPointsAtCap * 4 + 2 * (numPointsAtCap * 3));

        const float halfLength = 0.5f * length;

        for (int i = 0; i < numPointsAtCap * 2; ++i) {
            float angle = 2.0f * M_PI * float(i % numPointsAtCap) / numPointsAtCap;
            bool top = i < numPointsAtCap;
            auto point = GfVec3f(top ? halfLength : -halfLength, radius * cos(angle), radius * sin(angle));
            points.push_back(point);
        }

        {
            // Top cap faces
            points.push_back(GfVec3f(halfLength, 0.0f, 0.0f));
            normals.push_back(GfVec3f(1.0f, 0.0f, 0.0f));
            const int topCapCenterPointIndex = points.size() - 1;
            const int topCapNormalIndex = normals.size() - 1;
            for (int i = 0; i < numPointsAtCap; ++i) {
                pointIndices.push_back(i);
                pointIndices.push_back((i + 1) % numPointsAtCap);
                pointIndices.push_back(topCapCenterPointIndex);
                normalIndices.push_back(topCapNormalIndex);
                normalIndices.push_back(topCapNormalIndex);
                normalIndices.push_back(topCapNormalIndex);
                vpf.push_back(3);
            }
        }

        const int botPointIndexOffset = numPointsAtCap;
        {
            // Bottom cap faces
            points.push_back(GfVec3f(-halfLength, 0.0f, 0.0f));
            normals.push_back(GfVec3f(-1.0f, 0.0f, 0.0f));
            const int botCapCenterPointIndex = points.size() - 1;
            const int botCapNormalIndex = normals.size() - 1;
            for (int i = 0; i < numPointsAtCap; ++i) {
                pointIndices.push_back(botCapCenterPointIndex);
                pointIndices.push_back((i + 1) % numPointsAtCap + botPointIndexOffset);
                pointIndices.push_back(i + botPointIndexOffset);
                normalIndices.push_back(botCapNormalIndex);
                normalIndices.push_back(botCapNormalIndex);
                normalIndices.push_back(botCapNormalIndex);
                vpf.push_back(3);
            }
        }

        for (int i = 0; i < numPointsAtCap; ++i) {
            float angle = 2.0f * M_PI * float(i % numPointsAtCap) / numPointsAtCap;
            normals.push_back(GfVec3f(0.0f, cos(angle), sin(angle)));
        }

        const int normalIndexOffset = 2;
        for (int i = 0; i < numPointsAtCap; ++i) {
            pointIndices.push_back(i);
            pointIndices.push_back(i + botPointIndexOffset);
            pointIndices.push_back((i + 1) % numPointsAtCap + botPointIndexOffset);
            pointIndices.push_back((i + 1) % numPointsAtCap);
            normalIndices.push_back(i + normalIndexOffset);
            normalIndices.push_back(i + normalIndexOffset);
            normalIndices.push_back((i + 1) % numPointsAtCap + normalIndexOffset);
            normalIndices.push_back((i + 1) % numPointsAtCap + normalIndexOffset);
            vpf.push_back(4);
        }

        m_isLightPresent = true;

        return CreateMesh(points, pointIndices, normals, normalIndices, VtVec2fArray(), VtIntArray(), vpf);
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
        if (!material) {
            return nullptr;
        }

        return pxr::make_unique<RprApiObject>(material, [this](void* material) {
            m_rprMaterialFactory->DeleteMaterial(static_cast<RprApiMaterial*>(material));
        });
    }

    RprApiObjectPtr CreateHeteroVolume(const std::vector<uint32_t>& densityGridOnIndices, const std::vector<float>& densityGridOnValueIndices, const std::vector<float>& densityGridValues,
                                       const std::vector<uint32_t>& colorGridOnIndices, const std::vector<float>& colorGridOnValueIndices, const std::vector<float>& colorGridValues,
                                       const std::vector<uint32_t>& emissiveGridOnIndices, const std::vector<float>& emissiveGridOnValueIndices, const std::vector<float>& emissiveGridValues,
                                       const GfVec3i& gridSize) {
        rpr_hetero_volume heteroVolume = nullptr;
        if (RPR_ERROR_CHECK(rprContextCreateHeteroVolume(m_rprContext->GetHandle(), &heteroVolume), "Fail create hetero density volume")) return nullptr;
        auto heteroVolumeObject = RprApiObject::Wrap(heteroVolume);

        rpr_grid rprGridDensity;
        if (RPR_ERROR_CHECK(rprContextCreateGrid(
            m_rprContext->GetHandle(), &rprGridDensity
            , gridSize[0], gridSize[1], gridSize[2], &densityGridOnIndices[0]
            , densityGridOnIndices.size() / 3, RPR_GRID_INDICES_TOPOLOGY_XYZ_U32
            , &densityGridOnValueIndices[0], densityGridOnValueIndices.size() * sizeof(densityGridOnValueIndices[0])
            , 0)
            , "Fail create density grid")) return nullptr;
        heteroVolumeObject->AttachDependency(RprApiObject::Wrap(rprGridDensity));

        rpr_grid rprGridAlbedo;
        if (RPR_ERROR_CHECK(rprContextCreateGrid(
            m_rprContext->GetHandle(), &rprGridAlbedo
            , gridSize[0], gridSize[1], gridSize[2], &colorGridOnIndices[0]
            , colorGridOnIndices.size() / 3, RPR_GRID_INDICES_TOPOLOGY_XYZ_U32
            , &colorGridOnValueIndices[0], colorGridOnValueIndices.size() * sizeof(colorGridOnValueIndices[0])
            , 0)
            , "Fail create albedo grid")) return nullptr;
        heteroVolumeObject->AttachDependency(RprApiObject::Wrap(rprGridAlbedo));

        rpr_grid rprGridEmission;
        if (RPR_ERROR_CHECK(rprContextCreateGrid(
            m_rprContext->GetHandle(), &rprGridEmission
            , gridSize[0], gridSize[1], gridSize[2], &emissiveGridOnIndices[0]
            , emissiveGridOnIndices.size() / 3, RPR_GRID_INDICES_TOPOLOGY_XYZ_U32
            , &emissiveGridOnValueIndices[0], emissiveGridOnValueIndices.size() * sizeof(emissiveGridOnValueIndices[0])
            , 0)
            , "Fail create emission grid")) return nullptr;
        heteroVolumeObject->AttachDependency(RprApiObject::Wrap(rprGridEmission));

        if (RPR_ERROR_CHECK(rprHeteroVolumeSetDensityGrid(heteroVolume, rprGridDensity), "Fail to set density hetero volume")) return nullptr;
        if (RPR_ERROR_CHECK(rprHeteroVolumeSetDensityLookup(heteroVolume, &densityGridValues[0], densityGridValues.size() / 3), "Fail to set density volume lookup")) return nullptr;
        if (RPR_ERROR_CHECK(rprHeteroVolumeSetAlbedoGrid(heteroVolume, rprGridAlbedo), "Fail to set albedo hetero volume")) return nullptr;
        if (RPR_ERROR_CHECK(rprHeteroVolumeSetAlbedoLookup(heteroVolume, &colorGridValues[0], colorGridValues.size() / 3), "Fail to set albedo volume lookup")) return nullptr;
        if (RPR_ERROR_CHECK(rprHeteroVolumeSetEmissionGrid(heteroVolume, rprGridEmission), "Fail to set emission hetero volume")) return nullptr;
        if (RPR_ERROR_CHECK(rprHeteroVolumeSetEmissionLookup(heteroVolume, &emissiveGridValues[0], emissiveGridValues.size() / 3), "Fail to set emission volume lookup")) return nullptr;

        if (RPR_ERROR_CHECK(rprSceneAttachHeteroVolume(m_scene->GetHandle(), heteroVolume), "Fail attach hetero volume to scene")) return nullptr;
        m_dirtyFlags |= ChangeTracker::DirtyScene;

        heteroVolumeObject->AttachOnReleaseAction(RprApiObjectActionTokens->attach, [this](void* volume) {
            if (!RPR_ERROR_CHECK(rprSceneDetachHeteroVolume(m_scene->GetHandle(), volume), "Failed to detach hetero volume from scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        });

        return heteroVolumeObject;
    }

    void SetHeteroVolumeTransform(rpr_hetero_volume heteroVolume, const GfMatrix4f& m) {
        RPR_ERROR_CHECK(rprHeteroVolumeSetTransform(heteroVolume, false, m.GetArray()), "Fail to set hetero volume transform");
    }

    RprApiObjectPtr CreateVolume(const std::vector<uint32_t>& densityGridOnIndices, const std::vector<float>& densityGridOnValueIndices, const std::vector<float>& densityGridValues,
                                 const std::vector<uint32_t>& colorGridOnIndices, const std::vector<float>& colorGridOnValueIndices, const std::vector<float>& colorGridValues,
                                 const std::vector<uint32_t>& emissiveGridOnIndices, const std::vector<float>& emissiveGridOnValueIndices, const std::vector<float>& emissiveGridValues,
                                 const GfVec3i& gridSize, const GfVec3f& voxelSize, const GfVec3f& gridBBLow) {
        if (!m_rprContext) {
            return nullptr;
        }

        RecursiveLockGuard rprLock(g_rprAccessMutex);

        auto heteroVolume = CreateHeteroVolume(densityGridOnIndices, densityGridOnValueIndices, densityGridValues, colorGridOnIndices, colorGridOnValueIndices, colorGridValues, emissiveGridOnIndices, emissiveGridOnValueIndices, emissiveGridValues, gridSize);
        if (!heteroVolume) {
            return nullptr;
        }

        auto cubeMesh = CreateCubeMesh(1.0f, 1.0f, 1.0f);
        if (!cubeMesh) {
            return nullptr;
        }

        MaterialAdapter matAdapter(EMaterialType::TRANSPERENT,
                                   MaterialParams{{HdPrimvarRoleTokens->color, VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f))}});

        auto transparentMaterial = CreateMaterial(matAdapter);
        if (!transparentMaterial) {
            return nullptr;
        }

        GfMatrix4f meshTransform(1.0f);
        GfVec3f volumeSize = GfVec3f(voxelSize[0] * gridSize[0], voxelSize[1] * gridSize[1], voxelSize[2] * gridSize[2]);
        meshTransform.SetScale(volumeSize);
        meshTransform.SetTranslateOnly(GfCompMult(voxelSize, GfVec3f(gridSize)) / 2.0f + gridBBLow);

        SetMeshMaterial(cubeMesh->GetHandle(), static_cast<RprApiMaterial*>(transparentMaterial->GetHandle()));
        SetMeshHeteroVolume(cubeMesh->GetHandle(), heteroVolume->GetHandle());
        SetMeshTransform(cubeMesh->GetHandle(), meshTransform);
        SetHeteroVolumeTransform(heteroVolume->GetHandle(), meshTransform);

        heteroVolume->AttachDependency(std::move(cubeMesh));
        heteroVolume->AttachDependency(std::move(transparentMaterial));

        return heteroVolume;
    }

    void SetCameraViewMatrix(const GfMatrix4d& m) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        m_cameraViewMatrix = m;
        m_dirtyFlags |= ChangeTracker::DirtyCamera;
    }

    void SetCameraProjectionMatrix(const GfMatrix4d& proj) {
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

    GfVec2i GetViewportSize() const {
        return m_viewportSize;
    }

    void SetViewportSize(GfVec2i const& size) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        m_viewportSize = size;
        m_dirtyFlags |= ChangeTracker::DirtyViewport;
    }

    void SetAovBindings(HdRenderPassAovBindingVector const& aovBindings) {
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        m_aovBindings = aovBindings;
        m_dirtyFlags |= ChangeTracker::DirtyAOVBindings;
    }

    HdRenderPassAovBindingVector const& GetAovBindings() const {
        return m_aovBindings;
    }

    void ResolveFramebuffers(std::vector<std::pair<void*, size_t>> const& outputRenderBuffers) {
        for (auto& aovEntry : m_aovRegistry) {
            auto aov = aovEntry.second.lock();
            if (TF_VERIFY(aov)) {
                aov->Resolve();
            }
        }

        if (m_rifContext) {
            m_rifContext->ExecuteCommandQueue();            
        }

        for (int i = 0; i < m_aovBindings.size(); ++i) {
            if (outputRenderBuffers[i].first) {
                auto aovIter = m_boundAovs.find(m_aovBindings[i].aovName);
                if (aovIter != m_boundAovs.end()) {
                    aovIter->second->GetData(outputRenderBuffers[i].first, outputRenderBuffers[i].second);
                }
            }
        }
    }

    void Update() {
        RecursiveLockGuard rprLock(g_rprAccessMutex);

        m_imageCache->GarbageCollectIfNeeded();

        // In case there is no Lights in scene - create default
        if (!m_isLightPresent) {
            const GfVec3f k_defaultLightColor(0.5f, 0.5f, 0.5f);
            m_defaultLightObject = CreateEnvironmentLight(k_defaultLightColor, 1.f);
        }

        UpdateCamera();

        RenderSetting<bool> enableDenoise;
        {
            HdRprConfig* config;
            auto configInstanceLock = HdRprConfig::GetInstance(&config);

            enableDenoise.isDirty = config->IsDirty(HdRprConfig::DirtyDenoise);
            if (enableDenoise.isDirty) {
                enableDenoise.value = config->GetEnableDenoising();
            }

            UpdateSettings(*config);
            config->ResetDirty();
        }
        UpdateAovs(enableDenoise);

        m_dirtyFlags = ChangeTracker::Clean;
    }

    void UpdateTahoeSettings(HdRprConfig const& preferences, bool force) {
        if (preferences.IsDirty(HdRprConfig::DirtyAdaptiveSampling) || force) {
            m_varianceThreshold = preferences.GetVarianceThreshold();
            RPR_ERROR_CHECK(rprContextSetParameterByKey1f(m_rprContext->GetHandle(), RPR_CONTEXT_ADAPTIVE_SAMPLING_THRESHOLD, m_varianceThreshold), "Failed to set as.threshold");
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_ADAPTIVE_SAMPLING_MIN_SPP, preferences.GetMinAdaptiveSamples()), "Failed to set as.minspp");

            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }

        if (preferences.IsDirty(HdRprConfig::DirtyQuality) || force) {
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_MAX_RECURSION, preferences.GetMaxRayDepth()), "Failed to set max recursion");
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_MAX_DEPTH_DIFFUSE, preferences.GetMaxRayDepthDiffuse()), "Failed to set max depth diffuse");
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_MAX_DEPTH_GLOSSY, preferences.GetMaxRayDepthGlossy()), "Failed to set max depth glossy");
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_MAX_DEPTH_REFRACTION, preferences.GetMaxRayDepthRefraction()), "Failed to set max depth refraction");
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_MAX_DEPTH_GLOSSY_REFRACTION, preferences.GetMaxRayDepthGlossyRefraction()), "Failed to set max depth glossy refraction");
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_MAX_DEPTH_SHADOW, preferences.GetMaxRayDepthShadow()), "Failed to set max depth shadow");

            RPR_ERROR_CHECK(rprContextSetParameterByKey1f(m_rprContext->GetHandle(), RPR_CONTEXT_RAY_CAST_EPISLON, preferences.GetRaycastEpsilon()), "Failed to set ray cast epsilon");
            auto radianceClamp = preferences.GetEnableRadianceClamping() ? preferences.GetRadianceClamping() : std::numeric_limits<float>::max();
            RPR_ERROR_CHECK(rprContextSetParameterByKey1f(m_rprContext->GetHandle(), RPR_CONTEXT_RADIANCE_CLAMP, radianceClamp), "Failed to set radiance clamp");

            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }

        if (preferences.IsDirty(HdRprConfig::DirtyInteractiveMode)) {
            bool is_interactive = preferences.GetInteractiveMode();
            auto maxRayDepth = is_interactive ? preferences.GetInteractiveMaxRayDepth() : preferences.GetMaxRayDepth();
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_MAX_RECURSION, maxRayDepth), "Failed to set max recursion");
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_PREVIEW, int(is_interactive)), "Failed to set preview mode");

            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void UpdateHybridSettings(HdRprConfig const& preferences, bool force) {
        if (preferences.IsDirty(HdRprConfig::DirtyRenderQuality) || force) {
            auto quality = preferences.GetRenderQuality();
            if (quality == kRenderQualityMedium) {
                // XXX (Hybrid): temporarily disable until issues on hybrid side is not solved
                // otherwise driver crashes guaranteed (Radeon VII)
                quality = kRenderQualityHigh;
            }
            RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_RENDER_QUALITY, quality), "Fail to set context hybrid render quality");
        }
    }

    void UpdateSettings(HdRprConfig const& preferences, bool force = false) {
        if (preferences.IsDirty(HdRprConfig::DirtySampling) || force) {
            m_maxSamples = preferences.GetMaxSamples();
            if (m_maxSamples < m_iter) {
                // Force framebuffers clear to render required number of samples
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        }

        m_currentRenderQuality = preferences.GetRenderQuality();

        if (m_rprContext->GetActivePluginType() == rpr::PluginType::TAHOE) {
            UpdateTahoeSettings(preferences, force);
        } else if (m_rprContext->GetActivePluginType() == rpr::PluginType::HYBRID) {
            UpdateHybridSettings(preferences, force);
        }

        if (preferences.IsDirty(HdRprConfig::DirtyDevice) ||
            preferences.IsDirty(HdRprConfig::DirtyRenderQuality)) {
            bool restartRequired = false;
            if (preferences.IsDirty(HdRprConfig::DirtyDevice)) {
                if (int(m_rprContext->GetActiveRenderDeviceType()) != preferences.GetRenderDevice()) {
                    restartRequired = true;
                }
            }

            if (preferences.IsDirty(HdRprConfig::DirtyRenderQuality)) {
                auto quality = preferences.GetRenderQuality();

                auto activePlugin = m_rprContext->GetActivePluginType();
                if ((activePlugin == rpr::PluginType::TAHOE && quality < kRenderQualityFull) ||
                    (activePlugin == rpr::PluginType::HYBRID && quality == kRenderQualityFull)) {
                    restartRequired = true;
                }
            }

            m_state = restartRequired ? kStateRestartRequired : kStateRender;
        }
    }

    void UpdateCamera() {
        if ((m_dirtyFlags & ChangeTracker::DirtyCamera) == 0 &&
            (m_dirtyFlags & ChangeTracker::DirtyViewport) == 0) {
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
            auto ratio = double(m_viewportSize[0]) / m_viewportSize[1];
            auto focalLength = m_cameraProjectionMatrix[1][1] / (2.0 * ratio);
            auto sensorWidth = 1.0f;
            auto sensorHeight = 1.0f / ratio;

            RPR_ERROR_CHECK(rprCameraSetMode(camera, RPR_CAMERA_MODE_PERSPECTIVE), "Failed to set camera mode");
            RPR_ERROR_CHECK(rprCameraSetFocalLength(camera, focalLength), "Fail to set camera focal length");
            RPR_ERROR_CHECK(rprCameraSetSensorSize(camera, sensorWidth, sensorHeight), "Failed to set camera sensor size");
        }
    }

    void UpdateAovs(RenderSetting<bool> enableDenoise) {
        if (m_dirtyFlags & ChangeTracker::DirtyAOVBindings) {
            auto retainedBoundAovs = std::move(m_boundAovs);
            for (auto& aovBinding : m_aovBindings) {
                if (auto rb = aovBinding.renderBuffer) {
                    auto boundAovIter = retainedBoundAovs.find(aovBinding.aovName);
                    if (boundAovIter == retainedBoundAovs.end()) {
                        if (auto aov = CreateAov(aovBinding.aovName, rb->GetWidth(), rb->GetHeight(), rb->GetFormat())) {
                            m_boundAovs[aovBinding.aovName] = aov;
                        }
                    } else {
                        m_boundAovs[aovBinding.aovName] = boundAovIter->second;
                    }
                }
            }
        }

        if (m_dirtyFlags & ChangeTracker::DirtyViewport) {
            for (auto& aovEntry : m_internalAovs) {
                aovEntry.second->Resize(m_viewportSize[0], m_viewportSize[1], aovEntry.second->GetFormat());
            }
            for (auto& aovBinding : m_aovBindings) {
                if (auto rb = aovBinding.renderBuffer) {
                    auto boundAovIter = m_boundAovs.find(aovBinding.aovName);
                    if (boundAovIter != m_boundAovs.end()) {
                        boundAovIter->second->Resize(rb->GetWidth(), rb->GetHeight(), rb->GetFormat());
                    }
                }
            }
            // Size of bound AOVs controled by aovBinding's renderBuffer
        }

        bool clearAovs = false;
        if (m_dirtyFlags & ChangeTracker::DirtyScene ||
            m_dirtyFlags & ChangeTracker::DirtyAOVRegistry ||
            m_dirtyFlags & ChangeTracker::DirtyViewport ||
            m_dirtyFlags & ChangeTracker::DirtyCamera) {
            m_iter = 0;
            m_activePixels = -1;
            clearAovs = true;
        }

        UpdateDenoising(enableDenoise);

        auto rprApi = static_cast<HdRprRenderParam*>(m_delegate->GetRenderParam())->GetRprApi();
        for (auto it = m_aovRegistry.begin(); it != m_aovRegistry.end();) {
            if (auto aov = it->second.lock()) {
                aov->Update(rprApi, m_rifContext.get());
                if (clearAovs) {
                    aov->Clear();
                }
                ++it;
            } else {
                it = m_aovRegistry.erase(it);
            }
        }
    }

    void UpdateDenoising(RenderSetting<bool> enableDenoise) {
        // Disable denoiser to prevent possible crashes due to incorrect AI models
        if (!m_rifContext || m_rifContext->GetModelPath().empty()) {
            return;
        }

        if (!enableDenoise.isDirty) {
            return;
        }

        HdRprApiAov* aov = nullptr;
        auto colorAovIter = m_aovRegistry.find(HdRprAovTokens->color);
        if (colorAovIter == m_aovRegistry.end() ||
            !(aov = colorAovIter->second.lock().get())) {
            return;
        }

        assert(dynamic_cast<HdRprApiColorAov*>(aov));
        auto colorAov = static_cast<HdRprApiColorAov*>(aov);

        if (!enableDenoise.value) {
            colorAov->DisableDenoise(m_rifContext.get());
            return;
        }

        rif::FilterType filterType = rif::FilterType::EawDenoise;
#ifndef __APPLE__
        if (m_rprContext->GetActiveRenderDeviceType() == rpr::RenderDeviceType::GPU) {
            filterType = rif::FilterType::AIDenoise;
        }
#endif // __APPLE__

        if (filterType == rif::FilterType::EawDenoise) {
            colorAov->EnableEAWDenoise(m_internalAovs.at(HdRprAovTokens->albedo),
                                       m_internalAovs.at(HdRprAovTokens->normal),
                                       m_internalAovs.at(HdRprAovTokens->linearDepth),
                                       m_internalAovs.at(HdRprAovTokens->primId),
                                       m_internalAovs.at(HdRprAovTokens->worldCoordinate));
        } else {
            colorAov->EnableAIDenoise(m_internalAovs.at(HdRprAovTokens->albedo),
                                      m_internalAovs.at(HdRprAovTokens->normal),
                                      m_internalAovs.at(HdRprAovTokens->linearDepth));
        }
    }

    void RenderImpl(HdRprRenderThread* renderThread, std::vector<std::pair<void*, size_t>> const& outputRenderBuffers) {
        bool stopRequested = false;
        while (!IsConverged() || stopRequested) {
            renderThread->WaitUntilPaused();
            stopRequested = renderThread->IsStopRequested();
            if (stopRequested) {
                break;
            }

            m_rendering.store(true);
            auto status = rprContextRender(m_rprContext->GetHandle());
            m_rendering.store(false);

            if (status == RPR_ERROR_ABORTED ||
                RPR_ERROR_CHECK(status, "Fail contex render framebuffer")) {
                stopRequested = true;
                break;
            }

            m_iter++;
            if (m_varianceThreshold > 0.0f) {
                if (RPR_ERROR_CHECK(rprContextGetInfo(m_rprContext->GetHandle(), RPR_CONTEXT_ACTIVE_PIXEL_COUNT, sizeof(m_activePixels), &m_activePixels, NULL), "Failed to query active pixels")) {
                    m_activePixels = -1;
                }
            }

            if (!IsConverged()) {
                // Last framebuffer resolve will be called after "while" in case framebuffer is converged.
                // We do not resolve framebuffers in case user requested render stop
                ResolveFramebuffers(outputRenderBuffers);
            }

            stopRequested = renderThread->IsStopRequested();
        }

        if (!stopRequested) {
            ResolveFramebuffers(outputRenderBuffers);
        }
    }

    void Render(HdRprRenderThread* renderThread) {
        if (!m_rprContext) {
            return;
        }

        try {
            Update();
        } catch (std::runtime_error const& e) {
            TF_RUNTIME_ERROR("Failed to update: %s", e.what());
            return;
        }

        if (m_aovRegistry.empty()) {
            // Nothing to render
            return;
        }

        std::vector<std::pair<void*, size_t>> outputRenderBuffers;
        for (auto& aovBinding : m_aovBindings) {
            if (auto rb = static_cast<HdRprRenderBuffer*>(aovBinding.renderBuffer)) {
                if (rb->GetWidth() != m_viewportSize[0] || rb->GetHeight() != m_viewportSize[1]) {
                    TF_RUNTIME_ERROR("%s renderBuffer has inconsistent render buffer size: %ux%u. Expected: %dx%d",
                                     aovBinding.aovName.GetText(), rb->GetWidth(), rb->GetHeight(), m_viewportSize[0], m_viewportSize[1]);
                    outputRenderBuffers.emplace_back(nullptr, 0u);
                } else {
                    size_t size = rb->GetWidth() * rb->GetHeight() * HdDataSizeOfFormat(rb->GetFormat());
                    outputRenderBuffers.emplace_back(rb->Map(), size);
                }
            }
        }

        if (m_state == kStateRender) {
            try {
                RenderImpl(renderThread, outputRenderBuffers);
            } catch (std::runtime_error const& e) {
                TF_RUNTIME_ERROR("Failed to render frame: %s", e.what());
            }
        } else if (m_state == kStateRestartRequired) {
            PlugPluginPtr plugin = PLUG_THIS_PLUGIN;
            auto imagesPath = PlugFindPluginResource(plugin, "images", false);
            auto path = imagesPath + "/restartRequired.png";
            if (!RenderImage(path)) {
                fprintf(stderr, "Please restart render\n");
            }
        }

        for (auto& aovBinding : m_aovBindings) {
            if (auto rb = static_cast<HdRprRenderBuffer*>(aovBinding.renderBuffer)) {
                rb->Unmap();
                rb->SetConverged(true);
            }
        }
    }

    void AbortRender() {
        // XXX: disable until FIR-1588 resolved
        /*if (m_rprContext && m_rendering) {
            RPR_ERROR_CHECK(rprContextAbortRender(m_rprContext->GetHandle()), "Failed to abort render");
        }*/
    }

    int GetNumCompletedSamples() const {
        return m_iter;
    }

    int GetNumActivePixels() const {
        return m_activePixels;
    }

    bool IsChanged() const {
        if (m_dirtyFlags != ChangeTracker::Clean) {
            return true;
        }

        HdRprConfig* config;
        auto configInstanceLock = HdRprConfig::GetInstance(&config);
        return config->IsDirty(HdRprConfig::DirtyAll);
    }

    bool IsConverged() const {
        if (m_currentRenderQuality == kRenderQualityLow) {
            return m_iter == 1;
        }

        return m_iter >= m_maxSamples || m_activePixels == 0;
    }

    bool IsGlInteropEnabled() const {
        return m_rprContext && m_rprContext->IsGlInteropEnabled();
    }

    bool IsAovFormatConversionAvailable() const {
        return m_rifContext != nullptr;
    }

    int GetCurrentRenderQuality() const {
        return m_currentRenderQuality;
    }

private:
    void InitRpr() {
        RenderQualityType renderQuality;
        rpr::RenderDeviceType renderDevice;
        {
            HdRprConfig* config;
            auto configInstanceLock = HdRprConfig::GetInstance(&config);
            // Force sync to catch up the latest render quality and render device
            config->Sync(m_delegate);

            renderQuality = config->GetRenderQuality();
            renderDevice = static_cast<rpr::RenderDeviceType>(config->GetRenderDevice());
        }

        auto plugin = renderQuality == kRenderQualityFull ? rpr::PluginType::TAHOE : rpr::PluginType::HYBRID;
        auto cachePath = HdRprApi::GetCachePath();
        m_rprContext = rpr::Context::Create(plugin, renderDevice, false, cachePath.c_str());
        if (!m_rprContext) {
            return;
        }

        RPR_ERROR_CHECK(rprContextSetParameterByKey1u(m_rprContext->GetHandle(), RPR_CONTEXT_Y_FLIP, 0), "Fail to set context YFLIP parameter");

        {
            HdRprConfig* config;
            auto configInstanceLock = HdRprConfig::GetInstance(&config);
            UpdateSettings(*config, true);
        }

        m_imageCache.reset(new ImageCache(m_rprContext.get()));
        m_rendering.store(false);
    }

    bool ValidateRifModels(std::string const& modelsPath) {
        // To ensure that current RIF implementation will use correct models we check for the file that points to models version
        std::ifstream versionFile(modelsPath + "/rif_models.version");
        if (versionFile.is_open()) {
            std::stringstream buffer;
            buffer << versionFile.rdbuf();
            auto rifVersionString = std::to_string(RIF_VERSION_MAJOR) + "." + std::to_string(RIF_VERSION_MINOR) + "." + std::to_string(RIF_VERSION_REVISION);
            return rifVersionString == buffer.str();
        }

        return false;
    }

    void InitRif() {
        if (!m_rprContext) {
            return;
        }

        PlugPluginPtr plugin = PLUG_THIS_PLUGIN;
        auto modelsPath = PlugFindPluginResource(plugin, "rif_models", false);
        if (modelsPath.empty()) {
            TF_RUNTIME_ERROR("Failed to find RIF models in plugin package");
        } else if (!ValidateRifModels(modelsPath)) {
            modelsPath = "";
            TF_RUNTIME_ERROR("RIF version and AI models version mismatch");
        }

        m_rifContext = rif::Context::Create(m_rprContext.get(), modelsPath);
        if (!m_rifContext) {
            return;
        }

        // We create separate AOVs needed for denoising ASAP
        // In such a way, when user enables denoising it will not require to rerender
        // but it requires more memory, obviously, it should be taken into an account
        rif::FilterType filterType = rif::FilterType::EawDenoise;
#ifndef __APPLE__
        if (m_rprContext->GetActiveRenderDeviceType() == rpr::RenderDeviceType::GPU) {
            filterType = rif::FilterType::AIDenoise;
        }
#endif // __APPLE__

        m_internalAovs[HdRprAovTokens->albedo] = CreateAov(HdRprAovTokens->albedo, 0, 0);
        m_internalAovs[HdRprAovTokens->linearDepth] = CreateAov(HdRprAovTokens->linearDepth, 0, 0);
        m_internalAovs[HdRprAovTokens->normal] = CreateAov(HdRprAovTokens->normal, 0, 0);
        if (filterType == rif::FilterType::EawDenoise) {
            m_internalAovs[HdRprAovTokens->primId] = CreateAov(HdRprAovTokens->primId, 0, 0);
            m_internalAovs[HdRprAovTokens->worldCoordinate] = CreateAov(HdRprAovTokens->worldCoordinate, 0, 0);
        }
    }

    void InitMaterialSystem() {
        if (!m_rprContext) {
            return;
        }

        rpr_material_system matsys;
        if (RPR_ERROR_CHECK(rprContextCreateMaterialSystem(m_rprContext->GetHandle(), 0, &matsys), "Fail create Material System resolve")) return;
        m_matsys = RprApiObject::Wrap(matsys);
        m_rprMaterialFactory.reset(new RprMaterialFactory(matsys, m_imageCache.get()));
    }

    void SplitPolygons(const VtIntArray& indexes, const VtIntArray& vpf, VtIntArray& out_newIndexes, VtIntArray& out_newVpf) {
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

    void SplitPolygons(const VtIntArray& indexes, const VtIntArray& vpf, VtIntArray& out_newIndexes) {
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

    void ConvertIndices(VtIntArray* indices, VtIntArray const& vpf, TfToken const& windingOrder) {
        if (windingOrder == HdTokens->rightHanded) {
            return;
        }

        // XXX: RPR does not allow to select which winding order we want to use and it's by default right handed
        size_t indicesOffset = 0;
        for (int iFace = 0; iFace < vpf.size(); ++iFace) {
            auto faceIndices = indices->data() + indicesOffset;
            std::swap(faceIndices[0], faceIndices[2]);
            indicesOffset += vpf[iFace];
        }
    }

    RprApiObjectPtr CreateCubeMesh(float width, float height, float depth) {
        constexpr const size_t cubeVertexCount = 24;
        constexpr const size_t cubeNormalCount = 24;
        constexpr const size_t cubeVpfCount = 12;

        const float halfWidth = width * 0.5f;
        const float halfHeight = height * 0.5f;
        const float halfDepth = depth * 0.5f;

        VtVec3fArray position(cubeVertexCount);
        position[0] = GfVec3f(-halfWidth, halfHeight, -halfDepth);
        position[1] = GfVec3f(halfWidth, halfHeight, -halfDepth);
        position[2] = GfVec3f(halfWidth, halfHeight, halfDepth);
        position[3] = GfVec3f(-halfWidth, halfHeight, halfDepth);

        position[4] = GfVec3f(-halfWidth, -halfHeight, -halfDepth);
        position[5] = GfVec3f(halfWidth, -halfHeight, -halfDepth);
        position[6] = GfVec3f(halfWidth, -halfHeight, halfDepth);
        position[7] = GfVec3f(-halfWidth, -halfHeight, halfDepth);

        position[8] = GfVec3f(-halfWidth, -halfHeight, halfDepth);
        position[9] = GfVec3f(-halfWidth, -halfHeight, -halfDepth);
        position[10] = GfVec3f(-halfWidth, halfHeight, -halfDepth);
        position[11] = GfVec3f(-halfWidth, halfHeight, halfDepth);

        position[12] = GfVec3f(halfWidth, -halfHeight, halfDepth);
        position[13] = GfVec3f(halfWidth, -halfHeight, -halfDepth);
        position[14] = GfVec3f(halfWidth, halfHeight, -halfDepth);
        position[15] = GfVec3f(halfWidth, halfHeight, halfDepth);

        position[16] = GfVec3f(-halfWidth, -halfHeight, -halfDepth);
        position[17] = GfVec3f(halfWidth, -halfHeight, -halfDepth);
        position[18] = GfVec3f(halfWidth, halfHeight, -halfDepth);
        position[19] = GfVec3f(-halfWidth, halfHeight, -halfDepth);

        position[20] = GfVec3f(-halfWidth, -halfHeight, halfDepth);
        position[21] = GfVec3f(halfWidth, -halfHeight, halfDepth);
        position[22] = GfVec3f(halfWidth, halfHeight, halfDepth);
        position[23] = GfVec3f(-halfWidth, halfHeight, halfDepth);

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

        return CreateMesh(position, indexes, normals, VtIntArray(), VtVec2fArray(), VtIntArray(), vpf);
    }

    std::shared_ptr<HdRprApiAov> CreateAov(TfToken const& aovName, int width, int height, HdFormat format = HdFormatFloat32Vec4) {
        if (!m_rprContext ||
            width < 0 || height < 0 ||
            format == HdFormatInvalid || HdDataSizeOfFormat(format) == 0) {
            return nullptr;
        }

        auto rprAovIt = kAovTokenToRprAov.find(aovName);
        if (rprAovIt == kAovTokenToRprAov.end()) {
            TF_WARN("Unsupported aov type: %s", aovName.GetText());
            return nullptr;
        }

        std::shared_ptr<HdRprApiAov> aov;

        auto iter = m_aovRegistry.find(aovName);
        if (iter != m_aovRegistry.end()) {
            aov = iter->second.lock();
        }

        try {
            if (!aov) {
                if (aovName == HdRprAovTokens->color) {
                    aov = std::make_shared<HdRprApiColorAov>(width, height, format, m_rprContext.get());
                } else if (aovName == HdRprAovTokens->normal) {
                    aov = std::make_shared<HdRprApiNormalAov>(width, height, format, m_rprContext.get(), m_rifContext.get());
                } else if (aovName == HdRprAovTokens->depth) {
                    auto worldCoordinateAovIter = m_internalAovs.find(HdRprAovTokens->worldCoordinate);
                    if (worldCoordinateAovIter == m_internalAovs.end()) {
                        if (auto worldCoordinateAov = CreateAov(HdRprAovTokens->worldCoordinate, width, height, HdFormatFloat32Vec4)) {
                            worldCoordinateAovIter = m_internalAovs.emplace(HdRprAovTokens->worldCoordinate, worldCoordinateAov).first;
                        } else {
                            TF_CODING_ERROR("Failed to create depth AOV: can't create worldCoordinate AOV");
                            return nullptr;
                        }
                    }
                    aov = std::make_shared<HdRprApiDepthAov>(format, worldCoordinateAovIter->second, m_rprContext.get(), m_rifContext.get());
                } else {
                    aov = std::make_shared<HdRprApiAov>(rprAovIt->second, width, height, format, m_rprContext.get(), m_rifContext.get());
                }
            }

            m_aovRegistry[aovName] = aov;
            m_dirtyFlags |= ChangeTracker::DirtyAOVRegistry;
        } catch (std::runtime_error const& e) {
            TF_CODING_ERROR("Failed to create %s AOV: %s", aovName.GetText(), e.what());
        }

        return aov;
    }

    bool RenderImage(std::string const& path) {
        if (!m_rifContext) {
            return false;
        }

        auto colorAovBinding = std::find_if(m_aovBindings.begin(), m_aovBindings.end(), [](HdRenderPassAovBinding const& binding) {
            return binding.aovName == HdRprAovTokens->color;
        });
        if (colorAovBinding == m_aovBindings.end() ||
            !colorAovBinding->renderBuffer) {
            return false;
        }

        auto textureData = GlfUVTextureData::New(path, INT_MAX, 0, 0, 0, 0);
        if (textureData && textureData->Read(0, false)) {
            rif_image_desc imageDesc = {};
            imageDesc.image_width = textureData->ResizedWidth();
            imageDesc.image_height = textureData->ResizedHeight();
            imageDesc.image_depth = 1;

            int bytesPerChannel;
            if (textureData->GLType() == GL_UNSIGNED_BYTE) {
                imageDesc.type = RIF_COMPONENT_TYPE_UINT8;
                bytesPerChannel = 1;
            } else if (textureData->GLType() == GL_HALF_FLOAT) {
                imageDesc.type = RIF_COMPONENT_TYPE_FLOAT16;
                bytesPerChannel = 2;
            } else if (textureData->GLType() == GL_FLOAT) {
                imageDesc.type = RIF_COMPONENT_TYPE_FLOAT32;
                bytesPerChannel = 4;
            } else {
                TF_RUNTIME_ERROR("\"%s\" image has unsupported pixel channel type: %#x", path.c_str(), textureData->GLType());
                return false;
            }

            if (textureData->GLFormat() == GL_RGBA) {
                imageDesc.num_components = 4;
            } else if (textureData->GLFormat() == GL_RGB) {
                imageDesc.num_components = 3;
            } else if (textureData->GLFormat() == GL_RED) {
                imageDesc.num_components = 1;
            } else {
                TF_RUNTIME_ERROR("\"%s\" image has unsupported pixel format: %#x", path.c_str(), textureData->GLFormat());
                return false;
            }

            imageDesc.image_row_pitch = bytesPerChannel * imageDesc.num_components * imageDesc.image_width;
            imageDesc.image_slice_pitch = imageDesc.image_row_pitch * imageDesc.image_height;

            auto rifImage = m_rifContext->CreateImage(imageDesc);

            void* mappedData;
            if (RIF_ERROR_CHECK(rifImageMap(rifImage->GetHandle(), RIF_IMAGE_MAP_WRITE, &mappedData), "Failed to map rif image") || !mappedData) {
                return false;
            }
            std::memcpy(mappedData, textureData->GetRawBuffer(), imageDesc.image_slice_pitch);
            RIF_ERROR_CHECK(rifImageUnmap(rifImage->GetHandle(), mappedData), "Failed to unmap rif image");

            auto colorRb = colorAovBinding->renderBuffer;

            try {
                auto blitFilter = rif::Filter::Create(rif::FilterType::Resample, m_rifContext.get(), colorRb->GetWidth(), colorRb->GetHeight());
                blitFilter->SetParam("interpOperator", RIF_IMAGE_INTERPOLATION_BICUBIC);
                blitFilter->SetInput(rif::Color, rifImage->GetHandle());
                blitFilter->SetOutput(rif::Image::GetDesc(colorRb->GetWidth(), colorRb->GetHeight(), colorRb->GetFormat()));
                blitFilter->Update();

                m_rifContext->ExecuteCommandQueue();

                if (RIF_ERROR_CHECK(rifImageMap(blitFilter->GetOutput(), RIF_IMAGE_MAP_READ, &mappedData), "Failed to map rif image") || !mappedData) {
                    return false;
                }
                size_t size = HdDataSizeOfFormat(colorRb->GetFormat()) * colorRb->GetWidth() * colorRb->GetHeight();
                std::memcpy(colorRb->Map(), mappedData, size);
                colorRb->Unmap();

                RIF_ERROR_CHECK(rifImageUnmap(blitFilter->GetOutput(), mappedData), "Failed to unmap rif image");
                return true;
            } catch (rif::Error const& e) {
                TF_RUNTIME_ERROR("Failed to blit image: %s", e.what());
                return false;
            }
        } else {
            TF_RUNTIME_ERROR("Failed to load \"%s\" image", path.c_str());
            return false;
        }
    }

private:
    HdRenderDelegate* m_delegate;

    enum ChangeTracker : uint32_t {
        Clean = 0,
        AllDirty = ~0u,
        DirtyScene = 1 << 0,
        DirtyAOVRegistry = 1 << 1,
        DirtyCamera = 1 << 2,
        DirtyViewport = 1 << 3,
        DirtyAOVBindings = 1 << 4,
    };
    uint32_t m_dirtyFlags = ChangeTracker::AllDirty;

    std::unique_ptr<rpr::Context> m_rprContext;
    std::unique_ptr<rif::Context> m_rifContext;
    std::unique_ptr<ImageCache> m_imageCache;
    RprApiObjectPtr m_scene;
    RprApiObjectPtr m_camera;
    RprApiObjectPtr m_matsys;
    std::unique_ptr<RprMaterialFactory> m_rprMaterialFactory;

    std::map<TfToken, std::weak_ptr<HdRprApiAov>> m_aovRegistry;
    std::map<TfToken, std::shared_ptr<HdRprApiAov>> m_boundAovs;
    std::map<TfToken, std::shared_ptr<HdRprApiAov>> m_internalAovs;
    HdRenderPassAovBindingVector m_aovBindings;

    GfVec2i m_viewportSize;

    GfMatrix4d m_cameraViewMatrix = GfMatrix4d(1.f);
    GfMatrix4d m_cameraProjectionMatrix = GfMatrix4d(1.f);

    bool m_isLightPresent = false;
    RprApiObjectPtr m_defaultLightObject;

    int m_iter = 0;
    int m_activePixels = -1;
    int m_maxSamples = 0;
    float m_varianceThreshold = 0.0f;
    RenderQualityType m_currentRenderQuality = kRenderQualityFull;

    std::atomic<bool> m_rendering;

    enum State {
        kStateUninitialized,
        kStateRender,
        kStateRestartRequired
    };
    State m_state = kStateUninitialized;
};

std::unique_ptr<RprApiObject> RprApiObject::Wrap(void* handle) {
    return pxr::make_unique<RprApiObject>(handle);
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

void RprApiObject::DetachOnReleaseAction(TfToken const& actionName) {
    m_onReleaseActions.erase(actionName);
}

bool RprApiObject::HasOnReleaseAction(TfToken const& actionName) {
    return m_onReleaseActions.count(actionName) != 0;
}

void* RprApiObject::GetHandle() const {
    return m_handle;
}

HdRprApi::HdRprApi(HdRenderDelegate* delegate) : m_impl(new HdRprApiImpl(delegate)) {

}

HdRprApi::~HdRprApi() {
    delete m_impl;
}

RprApiObjectPtr HdRprApi::CreateMesh(const VtVec3fArray& points, const VtIntArray& pointIndexes, const VtVec3fArray& normals, const VtIntArray& normalIndexes, const VtVec2fArray& uv, const VtIntArray& uvIndexes, const VtIntArray& vpf, TfToken const& polygonWinding) {
    m_impl->InitIfNeeded();
    return m_impl->CreateMesh(points, pointIndexes, normals, normalIndexes, uv, uvIndexes, vpf, polygonWinding);
}

RprApiObjectPtr HdRprApi::CreateCurve(const VtVec3fArray& points, const VtIntArray& indexes, float width) {
    m_impl->InitIfNeeded();
    return m_impl->CreateCurve(points, indexes, width);
}

RprApiObjectPtr HdRprApi::CreateMeshInstance(RprApiObject* prototypeMesh) {
    m_impl->InitIfNeeded();
    return m_impl->CreateMeshInstance(prototypeMesh->GetHandle());
}

RprApiObjectPtr HdRprApi::CreateEnvironmentLight(GfVec3f color, float intensity) {
    m_impl->InitIfNeeded();
    return m_impl->CreateEnvironmentLight(color, intensity);
}

RprApiObjectPtr HdRprApi::CreateEnvironmentLight(const std::string& prthTotexture, float intensity) {
    m_impl->InitIfNeeded();
    return m_impl->CreateEnvironmentLight(prthTotexture, intensity);
}

RprApiObjectPtr HdRprApi::CreateRectLightMesh(float width, float height) {
    m_impl->InitIfNeeded();
    return m_impl->CreateRectLightMesh(width, height);
}

RprApiObjectPtr HdRprApi::CreateSphereLightMesh(float radius) {
    m_impl->InitIfNeeded();
    return m_impl->CreateSphereLightMesh(radius);
}

RprApiObjectPtr HdRprApi::CreateCylinderLightMesh(float radius, float length) {
    m_impl->InitIfNeeded();
    return m_impl->CreateCylinderLightMesh(radius, length);
}

RprApiObjectPtr HdRprApi::CreateDiskLightMesh(float radius) {
    m_impl->InitIfNeeded();
    return m_impl->CreateDiskLightMesh(radius);
}

void HdRprApi::SetLightTransform(RprApiObject* light, GfMatrix4f const& transform) {
    m_impl->SetLightTransform(light->GetHandle(), transform);
}

RprApiObjectPtr HdRprApi::CreateDirectionalLight() {
    m_impl->InitIfNeeded();
    return m_impl->CreateDirectionalLight();
}

void HdRprApi::SetDirectionalLightAttributes(RprApiObject* directionalLight, GfVec3f const& color, float shadowSoftness) {
    m_impl->SetDirectionalLightAttributes(directionalLight->GetHandle(), color, shadowSoftness);
}

RprApiObjectPtr HdRprApi::CreateVolume(
    const std::vector<uint32_t>& densityGridOnIndices, const std::vector<float>& densityGridOnValueIndices, const std::vector<float>& densityGridValues,
    const std::vector<uint32_t>& colorGridOnIndices, const std::vector<float>& colorGridOnValueIndices, const std::vector<float>& colorGridValues,
    const std::vector<uint32_t>& emissiveGridOnIndices, const std::vector<float>& emissiveGridOnValueIndices, const std::vector<float>& emissiveGridValues,
    const GfVec3i& gridSize, const GfVec3f& voxelSize, const GfVec3f& gridBBLow) {
    m_impl->InitIfNeeded();
    return m_impl->CreateVolume(
        densityGridOnIndices, densityGridOnValueIndices, densityGridValues,
        colorGridOnIndices, colorGridOnValueIndices, colorGridValues,
        emissiveGridOnIndices, emissiveGridOnValueIndices, emissiveGridValues,
        gridSize, voxelSize, gridBBLow);
}

RprApiObjectPtr HdRprApi::CreateMaterial(MaterialAdapter& materialAdapter) {
    m_impl->InitIfNeeded();
    return m_impl->CreateMaterial(materialAdapter);
}

void HdRprApi::SetMeshTransform(RprApiObject* mesh, const GfMatrix4f& transform) {
    m_impl->SetMeshTransform(mesh->GetHandle(), transform);
}

void HdRprApi::SetMeshRefineLevel(RprApiObject* mesh, int level) {
    m_impl->SetMeshRefineLevel(mesh->GetHandle(), level);
}

void HdRprApi::SetMeshVertexInterpolationRule(RprApiObject* mesh, TfToken boundaryInterpolation) {
    m_impl->SetMeshVertexInterpolationRule(mesh->GetHandle(), boundaryInterpolation);
}

void HdRprApi::SetMeshMaterial(RprApiObject* mesh, RprApiObject const* material) {
    auto materialHandle = material ? static_cast<RprApiMaterial*>(material->GetHandle()) : nullptr;
    m_impl->SetMeshMaterial(mesh->GetHandle(), materialHandle);
}

void HdRprApi::SetMeshVisibility(RprApiObject* mesh, bool isVisible) {
    m_impl->SetMeshVisibility(mesh, isVisible);
}

void HdRprApi::SetCurveMaterial(RprApiObject* curve, RprApiObject const* material) {
    auto materialHandle = material ? static_cast<RprApiMaterial*>(material->GetHandle()) : nullptr;
    m_impl->SetCurveMaterial(curve->GetHandle(), materialHandle);
}

void HdRprApi::SetCurveTransform(RprApiObject* curve, GfMatrix4f const& transform) {
    m_impl->SetCurveTransform(curve->GetHandle(), transform);
}

void HdRprApi::SetCurveVisibility(RprApiObject* curve, bool isVisible) {
    // XXX: RPR API does not have function to manage curve visibility
#if 0
    m_impl->SetCurveVisibility(curve->GetHandle(), isVisible);
#else
    if (isVisible) {
        m_impl->AttachCurveToScene(curve);
    } else {
        m_impl->DetachCurveFromScene(curve);
    }
#endif
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

GfVec2i HdRprApi::GetViewportSize() const {
    return m_impl->GetViewportSize();
}

void HdRprApi::SetViewportSize(GfVec2i const& size) {
    m_impl->SetViewportSize(size);
}

void HdRprApi::SetAovBindings(HdRenderPassAovBindingVector const& aovBindings) {
    m_impl->InitIfNeeded();
    m_impl->SetAovBindings(aovBindings);
}

HdRenderPassAovBindingVector HdRprApi::GetAovBindings() const {
    return m_impl->GetAovBindings();
}

void HdRprApi::Render(HdRprRenderThread* renderThread) {
    m_impl->Render(renderThread);
}

void HdRprApi::AbortRender() {
    m_impl->AbortRender();
}

bool HdRprApi::IsChanged() const {
    return m_impl->IsChanged();
}

int HdRprApi::GetNumCompletedSamples() const {
    return m_impl->GetNumCompletedSamples();
}

int HdRprApi::GetNumActivePixels() const {
    return m_impl->GetNumActivePixels();
}

bool HdRprApi::IsGlInteropEnabled() const {
    return m_impl->IsGlInteropEnabled();
}

bool HdRprApi::IsAovFormatConversionAvailable() const {
    return m_impl->IsAovFormatConversionAvailable();
}

int HdRprApi::GetCurrentRenderQuality() const {
    return m_impl->GetCurrentRenderQuality();
}

std::string HdRprApi::GetAppDataPath() {
    auto appDataPath = []() -> std::string {
#ifdef WIN32
        char appDataPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, 0, appDataPath))) {
            return appDataPath + std::string("\\hdRpr");
        }
#elif defined(__linux__)
        if (auto homeEnv = getenv("XDG_DATA_HOME")) {
            if (homeEnv[0] == '/') {
                return homeEnv + std::string("/hdRpr");
            }
        }

        int uid = getuid();
        auto homeEnv = std::getenv("HOME");
        if (uid != 0 && homeEnv) {
            return homeEnv + std::string("/.config/hdRpr");
        }

#elif defined(__APPLE__)
        if (auto homeEnv = getenv("HOME")) {
            if (homeEnv[0] == '/') {
                return homeEnv + std::string("/Library/Application Support/hdRPR");
            }
        }
#else
#warning "Unknown platform"
#endif
        return "";
    }();
    if (!appDataPath.empty()) {
        ArchCreateDirectory(appDataPath.c_str());
    }
    return appDataPath;
}

std::string HdRprApi::GetCachePath() {
    auto path = GetAppDataPath();
    if (!path.empty()) {
        path = (path + ARCH_PATH_SEP) + "cache";
        ArchCreateDirectory(path.c_str());
    }
    return path;
}

PXR_NAMESPACE_CLOSE_SCOPE
