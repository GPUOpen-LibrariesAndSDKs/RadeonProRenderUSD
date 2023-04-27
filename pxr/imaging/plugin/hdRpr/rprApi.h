/************************************************************************
Copyright 2020 Advanced Micro Devices, Inc
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
************************************************************************/

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
#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hd/renderDelegate.h"

#include <RadeonProRender.hpp>

#include <memory>
#include <vector>
#include <string>

#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
#include <RadeonProRender_VK.h>
#include <RadeonProRender_Baikal.h>
#include <vulkan/vulkan.h>
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT

PXR_NAMESPACE_OPEN_SCOPE

class HdRprDelegate;
class HdRprRenderThread;
class MaterialAdapter;

class HdRprApiImpl;
class RprUsdMaterial;

struct HdRprApiVolume;
struct HdRprApiEnvironmentLight;

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

/*
* kVisibilityLight was intentionally removed because RPR_SHAPE_VISIBILITY_LIGHT is the sum of
* RPR_SHAPE_VISIBILITY_PRIMARY_ONLY_FLAG and RPR_SHAPE_VISIBILITY_GLOSSY_REFRACTION
*/ 
enum HdRprVisibilityFlag {
    kVisiblePrimary = 1 << 0,
    kVisibleShadow = 1 << 1,
    kVisibleReflection = 1 << 2,
    kVisibleRefraction = 1 << 3,
    kVisibleTransparent = 1 << 4,
    kVisibleDiffuse = 1 << 5,
    kVisibleGlossyReflection = 1 << 6,
    kVisibleGlossyRefraction = 1 << 7,
    kVisibleAll = (kVisibleGlossyRefraction << 1) - 1
};
const uint32_t kInvisible = 0u;

class HdRprApi final {
public:
    HdRprApi(HdRprDelegate* delegate);
    ~HdRprApi();

    struct BackgroundOverride {
        bool enable;
        GfVec3f color;
    };

    HdRprApiEnvironmentLight* CreateEnvironmentLight(const std::string& pathTotexture, float intensity, BackgroundOverride const& backgroundOverride);
    HdRprApiEnvironmentLight* CreateEnvironmentLight(GfVec3f color, float intensity, BackgroundOverride const& backgroundOverride);
    void SetTransform(HdRprApiEnvironmentLight* envLight, GfMatrix4f const& transform);
    void Release(HdRprApiEnvironmentLight* envLight);

    rpr::DirectionalLight* CreateDirectionalLight();
    rpr::SpotLight* CreateSpotLight(float angle, float softness);
    rpr::IESLight* CreateIESLight(std::string const& iesFilepath);
    rpr::PointLight* CreatePointLight();
    rpr::DiskLight* CreateDiskLight();
    rpr::SphereLight* CreateSphereLight();

    void SetDirectionalLightAttributes(rpr::DirectionalLight* light, GfVec3f const& color, float shadowSoftnessAngle);
    void SetLightRadius(rpr::SphereLight* light, float radius);
    void SetLightRadius(rpr::DiskLight* light, float radius);
    void SetLightAngle(rpr::DiskLight* light, float angle);
    void SetLightColor(rpr::RadiantLight* light, GfVec3f const& color);

    void Release(rpr::Light* light);

    RprUsdMaterial* CreateGeometryLightMaterial(GfVec3f const& emissionColor);
    void ReleaseGeometryLightMaterial(RprUsdMaterial* material);

    HdRprApiVolume* CreateVolume(VtUIntArray const& densityCoords, VtFloatArray const& densityValues, VtVec3fArray const& densityLUT, float densityScale,
                                 VtUIntArray const& albedoCoords, VtFloatArray const& albedoValues, VtVec3fArray const& albedoLUT, float albedoScale,
                                 VtUIntArray const& emissionCoords, VtFloatArray const& emissionValues, VtVec3fArray const& emissionLUT, float emissionScale,
                                 const GfVec3i& gridSize, const GfVec3f& voxelSize, const GfVec3f& gridBBLow);
    void SetTransform(HdRprApiVolume* volume, GfMatrix4f const& transform);
    void SetVolumeVisibility(HdRprApiVolume* volume, uint32_t visibilityMask);
    void Release(HdRprApiVolume* volume);

    RprUsdMaterial* CreateMaterial(SdfPath const& materialId, HdSceneDelegate* sceneDelegate, HdMaterialNetworkMap const& materialNetwork);
    RprUsdMaterial* CreatePointsMaterial(VtVec3fArray const& colors);
    RprUsdMaterial* CreateDiffuseMaterial(GfVec3f const& color);
    RprUsdMaterial* CreatePrimvarColorLookupMaterial();
    void Release(RprUsdMaterial* material);

    rpr::Shape* CreateMesh(VtVec3fArray const& points, VtIntArray const& pointIndexes, VtVec3fArray const& normals, VtIntArray const& normalIndexes, VtVec2fArray const& uvs, VtIntArray const& uvIndexes, VtIntArray const& vpf, TfToken const& polygonWinding);
    rpr::Shape* CreateMesh(VtArray<VtVec3fArray> const& pointSamples, VtIntArray const& pointIndexes, VtArray<VtVec3fArray> const& normalSamples, VtIntArray const& normalIndexes, VtArray<VtVec2fArray> const& uvSamples, VtIntArray const& uvIndexes, VtIntArray const& vpf, TfToken const& polygonWinding);
    rpr::Shape* CreateMeshInstance(rpr::Shape* prototypeMesh);
    void SetMeshRefineLevel(rpr::Shape* mesh, int level, const float creaseWeight);
    void SetMeshVertexInterpolationRule(rpr::Shape* mesh, TfToken boundaryInterpolation);
    void SetMeshMaterial(rpr::Shape* mesh, RprUsdMaterial const* material, bool displacementEnabled);
    void SetMeshVisibility(rpr::Shape* mesh, uint32_t visibilityMask);
    void SetMeshId(rpr::Shape* mesh, uint32_t id);
    void SetMeshIgnoreContour(rpr::Shape* mesh, bool ignoreContour);
    bool SetMeshVertexColor(rpr::Shape* mesh, VtArray<VtVec3fArray> const& primvarSamples, HdInterpolation interpolation);
    void Release(rpr::Shape* shape);

    rpr::Curve* CreateCurve(VtVec3fArray const& points, VtIntArray const& indices, VtFloatArray const& radiuses, VtVec2fArray const& uvs, VtIntArray const& segmentPerCurve);
    void SetCurveMaterial(rpr::Curve* curve, RprUsdMaterial const* material);
    void SetCurveVisibility(rpr::Curve* curve, uint32_t visibilityMask);
    void Release(rpr::Curve* curve);

    void SetTransform(rpr::SceneObject* object, GfMatrix4f const& transform);
    void SetTransform(rpr::Shape* shape, size_t numSamples, float* timeSamples, GfMatrix4d* transformSamples);

    void SetName(rpr::ContextObject* object, const char* name);
    void SetName(RprUsdMaterial* object, const char* name);
    void SetName(HdRprApiEnvironmentLight* object, const char* name);

    GfMatrix4d GetCameraViewMatrix() const;
    const GfMatrix4d& GetCameraProjectionMatrix() const;

    HdCamera const* GetCamera() const;
    void SetCamera(HdCamera const* camera);

    GfVec2i GetViewportSize() const;
    void SetViewportSize(GfVec2i const& size);

    void SetAovBindings(HdRenderPassAovBindingVector const& aovBindings);
    HdRenderPassAovBindingVector GetAovBindings() const;

    void SetInteropInfo(void* interopInfo);

#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
    bool GetInteropSemaphore(VkSemaphore& rInteropSemaphore, uint32_t& rInteropSemaphoreIndex);
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT

    void Restart();

    struct RenderStats {
        double percentDone;
        double averageRenderTimePerSample;
        double averageResolveTimePerSample;
        double totalRenderTime;
        double frameRenderTotalTime;
        double frameResolveTotalTime;
        double cacheCreationTime;
        double syncTime;
    };
    RenderStats GetRenderStats() const;

    std::vector<std::string> GetGpuUsedNames() const;
    int GetCpuThreadCountUsed() const;
    float GetFirstIterationRenerTime() const;

    void CommitResources();
    void Resolve(SdfPath const& aovId);
    void Render(HdRprRenderThread* renderThread);
    void AbortRender();

    bool IsChanged() const;
    bool IsGlInteropEnabled() const;
    bool IsVulkanInteropEnabled() const;
    bool IsArbitraryShapedLightSupported() const;
    bool IsSphereAndDiskLightSupported() const;
    TfToken const& GetCurrentRenderQuality() const;
    rpr::FrameBuffer* GetRawColorFramebuffer();
    rpr::FrameBuffer* GetPrimIdFramebuffer();

private:
    HdRprApiImpl* m_impl = nullptr;
};

rpr::EnvironmentLight* GetLightObject(HdRprApiEnvironmentLight* envLight);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_H
