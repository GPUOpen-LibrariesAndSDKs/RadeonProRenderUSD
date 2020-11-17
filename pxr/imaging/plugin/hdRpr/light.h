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

#ifndef HDRPR_LIGHT_H
#define HDRPR_LIGHT_H

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/imaging/hd/light.h"

#include "pxr/imaging/rprUsd/boostIncludePath.h"
#include BOOST_INCLUDE_PATH(variant.hpp)

namespace rpr { class Shape; class PointLight; class SpotLight; class IESLight; class DiskLight; class SphereLight; }

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;
class RprUsdMaterial;

class HdRprLight : public HdLight {
public:
    HdRprLight(SdfPath const& id, TfToken const& lightType)
        : HdLight(id), m_lightType(lightType) {

    }

    ~HdRprLight() override = default;

    void Sync(HdSceneDelegate* sceneDelegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Finalize(HdRenderParam* renderParam) override;

private:
    void CreateIESLight(HdRprApi* rprApi, std::string const& path);

    void CreateAreaLightMesh(HdRprApi* rprApi, HdSceneDelegate* sceneDelegate);
    rpr::Shape* CreateDiskLightMesh(HdRprApi* rprApi);
    rpr::Shape* CreateRectLightMesh(HdRprApi* rprApi, bool applyTransform = false, GfMatrix4f const& transform = GfMatrix4f(1.0f));
    rpr::Shape* CreateSphereLightMesh(HdRprApi* rprApi);
    rpr::Shape* CreateCylinderLightMesh(HdRprApi* rprApi);

    struct AreaLight;
    void SyncAreaLightGeomParams(HdSceneDelegate* sceneDelegate, float* intensity);

    void ReleaseLight(HdRprApi* rprApi);

private:
    const TfToken m_lightType;

    struct AreaLight {
        RprUsdMaterial* material = nullptr;
        std::vector<rpr::Shape*> meshes;
    };

    struct LightVariantEmpty {};
    using Light = BOOST_NS::variant<LightVariantEmpty, rpr::PointLight*, rpr::SpotLight*, rpr::IESLight*, rpr::DiskLight*, rpr::SphereLight*, AreaLight*>;
    Light m_light;

    struct LightParameterSetter;
    struct LightTransformSetter;
    struct LightNameSetter;
    struct LightReleaser;

    GfVec3f m_emisionColor = GfVec3f(0.0f);
    GfMatrix4f m_transform;
    GfMatrix4f m_localTransform;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_LIGHT_H
