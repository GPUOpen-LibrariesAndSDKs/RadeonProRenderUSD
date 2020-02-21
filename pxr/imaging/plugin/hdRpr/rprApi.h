#ifndef HDRPR_RPR_API_H
#define HDRPR_RPR_API_H

#include "api.h"

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

#include <RadeonProRender.hpp>

#include <memory>
#include <vector>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class HdRprRenderThread;
class MaterialAdapter;

class HdRprApiImpl;

struct HdRprApiVolume;
struct HdRprApiMaterial;
struct HdRprApiEnvironmentLight;

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

class HdRprApi final {
public:
    HdRprApi(HdRenderDelegate* delegate);
    ~HdRprApi();

    HdRprApiEnvironmentLight* CreateEnvironmentLight(const std::string& pathTotexture, float intensity);
    HdRprApiEnvironmentLight* CreateEnvironmentLight(GfVec3f color, float intensity);
    void SetTransform(HdRprApiEnvironmentLight* envLight, GfMatrix4f const& transform);
    void Release(HdRprApiEnvironmentLight* envLight);

    rpr::DirectionalLight* CreateDirectionalLight();
    rpr::SpotLight* CreateSpotLight(float angle, float softness);
    rpr::IESLight* CreateIESLight(std::string const& iesFilepath);
    rpr::PointLight* CreatePointLight();

    void SetDirectionalLightAttributes(rpr::DirectionalLight* light, GfVec3f const& color, float shadowSoftnessAngle);
    void SetLightColor(rpr::SpotLight* light, GfVec3f const& color);
    void SetLightColor(rpr::PointLight* light, GfVec3f const& color);
    void SetLightColor(rpr::IESLight* light, GfVec3f const& color);

    void Release(rpr::Light* light);

    HdRprApiVolume* CreateVolume(const std::vector<uint32_t>& densityGridOnIndices, const std::vector<float>& densityGridOnValueIndices, const std::vector<float>& densityGridValues,
                                const std::vector<uint32_t>& colorGridOnIndices, const std::vector<float>& colorGridOnValueIndices, const std::vector<float>& colorGridValues,
                                const std::vector<uint32_t>& emissiveGridOnIndices, const std::vector<float>& emissiveGridOnValueIndices, const std::vector<float>& emissiveGridValues,
                                const GfVec3i& gridSize, const GfVec3f& voxelSize, const GfVec3f& gridBBLow);
    void SetTransform(HdRprApiVolume* volume, GfMatrix4f const& transform);
    void Release(HdRprApiVolume* volume);

    HdRprApiMaterial* CreateMaterial(MaterialAdapter& materialAdapter);
    void Release(HdRprApiMaterial* material);

    rpr::Shape* CreateMesh(const VtVec3fArray& points, const VtIntArray& pointIndexes, const VtVec3fArray& normals, const VtIntArray& normalIndexes, const VtVec2fArray& uv, const VtIntArray& uvIndexes, const VtIntArray& vpf, TfToken const& polygonWinding);
    rpr::Shape* CreateMeshInstance(rpr::Shape* prototypeMesh);
    void SetMeshRefineLevel(rpr::Shape* mesh, int level);
    void SetMeshVertexInterpolationRule(rpr::Shape* mesh, TfToken boundaryInterpolation);
    void SetMeshMaterial(rpr::Shape* mesh, HdRprApiMaterial const* material, bool doublesided, bool displacementEnabled);
    void SetMeshVisibility(rpr::Shape* mesh, bool isVisible);
    void SetMeshLightVisibility(rpr::Shape* lightMesh, bool isVisible);
    void Release(rpr::Shape* shape);

    rpr::Curve* CreateCurve(VtVec3fArray const& points, VtIntArray const& indices, VtFloatArray const& radiuses, VtVec2fArray const& uvs, VtIntArray const& segmentPerCurve);
    void SetCurveMaterial(rpr::Curve* curve, HdRprApiMaterial const* material);
    void SetCurveVisibility(rpr::Curve* curve, bool isVisible);
    void Release(rpr::Curve* curve);

    void SetTransform(rpr::SceneObject* object, GfMatrix4f const& transform);

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
    bool IsArbitraryShapedLightSupported() const;
    int GetCurrentRenderQuality() const;

    static std::string GetAppDataPath();
    static std::string GetCachePath();

private:
    HdRprApiImpl* m_impl = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_H
