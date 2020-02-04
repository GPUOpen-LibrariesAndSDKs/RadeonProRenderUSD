#ifndef HDRPR_RPR_API_H
#define HDRPR_RPR_API_H

#include "api.h"
#include "renderThread.h"
#include "rprcpp/rprObject.h"

#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/quaternion.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hd/renderDelegate.h"

#include <memory>
#include <vector>
#include <string.h>

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

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

#define HD_RPR_AOV_TOKENS \
    (color) \
    (albedo) \
    (depth) \
    (linearDepth) \
    (primId) \
    (instanceId) \
    (elementId) \
    (normal) \
    (worldCoordinate) \
    (opacity) \
    ((primvarsSt, "primvars:st"))

TF_DECLARE_PUBLIC_TOKENS(HdRprAovTokens, HDRPR_API, HD_RPR_AOV_TOKENS);

class HdRprApi final {
public:
    HdRprApi(HdRenderDelegate* delegate);
    ~HdRprApi();

    RprApiObjectPtr CreateEnvironmentLight(const std::string& pathTotexture, float intensity);
    RprApiObjectPtr CreateEnvironmentLight(GfVec3f color, float intensity);
    RprApiObjectPtr CreateRectLightMesh(float width, float height);
    RprApiObjectPtr CreateSphereLightMesh(float radius);
    RprApiObjectPtr CreateCylinderLightMesh(float radius, float length);
    RprApiObjectPtr CreateDiskLightMesh(float radius);
    void SetLightTransform(RprApiObject* light, GfMatrix4f const& transform);

    RprApiObjectPtr CreateDirectionalLight();
    void SetDirectionalLightAttributes(RprApiObject* directionalLight, GfVec3f const& color, float shadowSoftnessAngle);

    RprApiObjectPtr CreateVolume(const std::vector<uint32_t>& densityGridOnIndices, const std::vector<float>& densityGridOnValueIndices, const std::vector<float>& densityGridValues,
                                 const std::vector<uint32_t>& colorGridOnIndices, const std::vector<float>& colorGridOnValueIndices, const std::vector<float>& colorGridValues,
                                 const std::vector<uint32_t>& emissiveGridOnIndices, const std::vector<float>& emissiveGridOnValueIndices, const std::vector<float>& emissiveGridValues,
                                 const GfVec3i& gridSize, const GfVec3f& voxelSize, const GfVec3f& gridBBLow);

    RprApiObjectPtr CreateMesh(const VtVec3fArray& points, const VtIntArray& pointIndexes, const VtVec3fArray& normals, const VtIntArray& normalIndexes, const VtVec2fArray& uv, const VtIntArray& uvIndexes, const VtIntArray& vpf, TfToken const& polygonWinding);
    RprApiObjectPtr CreateMeshInstance(RprApiObject* prototypeMesh);
    void SetMeshTransform(RprApiObject* mesh, const GfMatrix4f& transform);
    void SetMeshRefineLevel(RprApiObject* mesh, int level);
    void SetMeshVertexInterpolationRule(RprApiObject* mesh, TfToken boundaryInterpolation);
    void SetMeshMaterial(RprApiObject* mesh, RprApiObject const* material, bool doublesided, bool displacementEnabled);
    void SetMeshVisibility(RprApiObject* mesh, bool isVisible);

    RprApiObjectPtr CreateCurve(VtVec3fArray const& points, VtIntArray const& indices, VtFloatArray const& radiuses, VtVec2fArray const& uvs, VtIntArray const& segmentPerCurve);
    void SetCurveMaterial(RprApiObject* curve, RprApiObject const* material);
    void SetCurveVisibility(RprApiObject* curve, bool isVisible);
    void SetCurveTransform(RprApiObject* curve, GfMatrix4f const& transform);

    RprApiObjectPtr CreateMaterial(MaterialAdapter& materialAdapter);

    GfMatrix4d GetCameraViewMatrix() const;
    const GfMatrix4d& GetCameraProjectionMatrix() const;

    HdCamera const* GetCamera() const;
    void SetCamera(HdCamera const* camera);

    GfVec2i GetViewportSize() const;
    void SetViewportSize(GfVec2i const& size);

    void SetAovBindings(HdRenderPassAovBindingVector const& aovBindings);
    HdRenderPassAovBindingVector GetAovBindings() const;

    int GetNumCompletedSamples() const;
    // returns -1 if adaptive sampling is not used
    int GetNumActivePixels() const;

    void Render(HdRprRenderThread* renderThread);
    void AbortRender();

    bool IsChanged() const;
    bool IsGlInteropEnabled() const;
    bool IsAovFormatConversionAvailable() const;
    int GetCurrentRenderQuality() const;

    static std::string GetAppDataPath();
    static std::string GetCachePath();

private:
    HdRprApiImpl* m_impl = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_H
