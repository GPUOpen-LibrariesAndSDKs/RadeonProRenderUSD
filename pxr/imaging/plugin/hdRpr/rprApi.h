#ifndef HDRPR_RPR_API_H
#define HDRPR_RPR_API_H

#include "api.h"

#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quaternion.h"
#include "pxr/imaging/hd/types.h"
#include "pxr/base/tf/staticTokens.h"

#include "rprcpp/rprObject.h"

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApiImpl;
class MaterialAdapter;
struct RprApiMaterial;

class RprApiObject {
public:
    static std::unique_ptr<RprApiObject> Wrap(void* handle);

    RprApiObject() : m_handle(nullptr) {}
    RprApiObject(nullptr_t) : m_handle(nullptr) {}

    explicit RprApiObject(void* handle);
    RprApiObject(void* handle, std::function<void (void*)> deleter);
    ~RprApiObject();

    void AttachDependency(std::unique_ptr<RprApiObject>&& dependencyObject);
    void AttachDependency(std::unique_ptr<rpr::Object>&& dependencyObject);

    void AttachOnReleaseAction(TfToken const& actionName, std::function<void (void*)> action);
    void DetachOnReleaseAction(TfToken const& actionName);
    bool HasOnReleaseAction(TfToken const& actionName);

    void* GetHandle() const;

private:
    void* m_handle;
    std::function<void (void*)> m_deleter;
    std::vector<std::unique_ptr<RprApiObject>> m_dependencyObjects;
    std::vector<std::unique_ptr<rpr::Object>> m_dependencyRprObjects;
    std::map<TfToken, std::function<void (void*)>> m_onReleaseActions;
};
using RprApiObjectPtr = std::unique_ptr<RprApiObject>;

#define HD_RPR_AOV_TOKENS \
    (color)                                     \
    (albedo)                                    \
    (depth)                                     \
    (linearDepth)                               \
    (primId)                                    \
    (instanceId)                                \
    (elementId)                                 \
    (normal)                                    \
    (worldCoordinate)                           \
    ((primvarsSt, "primvars:st"))

TF_DECLARE_PUBLIC_TOKENS(HdRprAovTokens, HDRPR_API, HD_RPR_AOV_TOKENS);

class HdRprApi final
{
public:
    HdRprApi();
    ~HdRprApi();

    RprApiObjectPtr CreateEnvironmentLight(const std::string& pathTotexture, float intensity);
    RprApiObjectPtr CreateEnvironmentLight(GfVec3f color, float intensity);
    RprApiObjectPtr CreateRectLightMesh(float width, float height);
    RprApiObjectPtr CreateSphereLightMesh(float radius);
    RprApiObjectPtr CreateCylinderLightMesh(float radius, float length);
    RprApiObjectPtr CreateDiskLightMesh(float radius);
    void SetLightTransform(RprApiObject* light, GfMatrix4d const& transform);

    RprApiObjectPtr CreateVolume(const VtArray<float>& gridDencityData, const VtArray<size_t>& indexesDencity, const VtArray<float>& gridAlbedoData, const VtArray<unsigned int>& indexesAlbedo, const GfVec3i& grigSize, const GfVec3f& voxelSize);

    RprApiObjectPtr CreateMesh(const VtVec3fArray& points, const VtIntArray& pointIndexes, const VtVec3fArray& normals, const VtIntArray& normalIndexes, const VtVec2fArray& uv, const VtIntArray& uvIndexes, const VtIntArray& vpf, TfToken const& polygonWinding);
    RprApiObjectPtr CreateMeshInstance(RprApiObject* prototypeMesh);
    void SetMeshTransform(RprApiObject* mesh, const GfMatrix4d& transform);
    void SetMeshRefineLevel(RprApiObject* mesh, int level);
    void SetMeshVertexInterpolationRule(RprApiObject* mesh, TfToken boundaryInterpolation);
    void SetMeshMaterial(RprApiObject* mesh, RprApiObject const* material);
    void SetMeshVisibility(RprApiObject* mesh, bool isVisible);

    RprApiObjectPtr CreateCurve(const VtVec3fArray& points, const VtIntArray& indexes, float width);
    void SetCurveMaterial(RprApiObject* curve, RprApiObject const* material);
    void SetCurveVisibility(RprApiObject* curve, bool isVisible);

    RprApiObjectPtr CreateMaterial(MaterialAdapter& materialAdapter);

    const GfMatrix4d& GetCameraViewMatrix() const;
    const GfMatrix4d& GetCameraProjectionMatrix() const;
    void SetCameraViewMatrix(const GfMatrix4d& m );
    void SetCameraProjectionMatrix(const GfMatrix4d& m);

    bool EnableAov(TfToken const& aovName, int width, int height, HdFormat format = HdFormatCount);
    void DisableAov(TfToken const& aovName);
    bool IsAovEnabled(TfToken const& aovName);
    GfVec2i GetAovSize(TfToken const& aovName) const;
    std::shared_ptr<char> GetAovData(TfToken const& aovName, std::shared_ptr<char> buffer = nullptr, size_t* bufferSize = nullptr);

    // This function exist for only one particular reason:
    //   for explicit bliting to GL framebuffer when there are no aovBindings in renderPass::_Execute
    //   we need to know the latest enabled AOV so we can draw it
    TfToken const& GetActiveAov() const;

    void Render();
    bool IsConverged();

    bool IsGlInteropEnabled() const;

    static std::string GetAppDataPath();
    static std::string GetCachePath();

private:
    HdRprApiImpl* m_impl = nullptr;
};

typedef std::shared_ptr<HdRprApi> HdRprApiSharedPtr;
typedef std::weak_ptr<HdRprApi> HdRprApiWeakPtr;

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_H
