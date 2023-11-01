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

#include "light.h"
#include "renderParam.h"
#include "primvarUtil.h"
#include "rprApi.h"

#include "pxr/imaging/rprUsd/debugCodes.h"
#include "pxr/imaging/rprUsd/tokens.h"

#include "pxr/imaging/rprUsd/lightRegistry.h"

#include "pxr/base/tf/envSetting.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/usd/usdLux/blackbody.h"
#include "pxr/usd/usdLux/tokens.h"
#include "pxr/usd/sdf/assetPath.h"

#include "pxr/usdImaging/usdImaging/implicitSurfaceMeshUtils.h"
#include "pxr/imaging/pxOsd/meshTopology.h"

PXR_NAMESPACE_OPEN_SCOPE

#if PXR_VERSION >= 2105
#define USD_LUX_TOKEN_SHAPING_IES_FILE UsdLuxTokens->inputsShapingIesFile
#define USD_LUX_TOKEN_SHAPING_CONE_ANGLE UsdLuxTokens->inputsShapingConeAngle
#define USD_LUX_TOKEN_SHAPING_CONE_SOFTNESS UsdLuxTokens->inputsShapingConeSoftness
#else
#define USD_LUX_TOKEN_SHAPING_IES_FILE UsdLuxTokens->shapingIesFile
#define USD_LUX_TOKEN_SHAPING_CONE_ANGLE UsdLuxTokens->shapingConeAngle
#define USD_LUX_TOKEN_SHAPING_CONE_SOFTNESS UsdLuxTokens->shapingConeSoftness
#endif

namespace {

float GetDiskLightNormalization(GfMatrix4f const& transform, float radius) {
    const double sx = GfVec3d(transform[0][0], transform[1][0], transform[2][0]).GetLength() * radius;
    const double sy = GfVec3d(transform[0][1], transform[1][1], transform[2][1]).GetLength() * radius;

    float scaleFactor = 1.0f;
    if (sx != 0.0 && sy != 0.0) {
        constexpr float unitDiskArea = M_PI;
        float diskArea = M_PI * sx * sy;
        scaleFactor = diskArea / unitDiskArea;
    }
    return scaleFactor;
}

float GetSphereLightNormalization(GfMatrix4f const& transform, float radius) {
    const double sx = GfVec3d(transform[0][0], transform[1][0], transform[2][0]).GetLength() * radius;
    const double sy = GfVec3d(transform[0][1], transform[1][1], transform[2][1]).GetLength() * radius;
    const double sz = GfVec3d(transform[0][2], transform[1][2], transform[2][2]).GetLength() * radius;

    float scaleFactor = 1.0f;
    if (sx != 0.0 && sy != 0.0 && sz != 0.0) {
        if (sx == sy && sy == sz) {
            // Can use the simple formula for surface area of a sphere
            scaleFactor = static_cast<float>(sx * sx);
        } else {
            // Approximating the area of a stretched ellipsoid using the Knud Thomsen formula:
            // http://www.numericana.com/answer/ellipsoid.htm
            constexpr const double p = 1.6075;
            constexpr const double pinv = 1. / 1.6075;
            double sx_p = pow(sx, p);
            double sy_p = pow(sy, p);
            double sz_p = pow(sz, p);

            scaleFactor = 1.0f / pow(3. / (sx_p * sy_p + sx_p * sz_p + sy_p * sz_p), pinv);
        }
    }
    return scaleFactor;
}

float GetRectLightNormalization(GfMatrix4f const& transform, float width, float height) {
    const GfVec4f ox(width, 0., 0., 0.);
    const GfVec4f oy(0., height, 0., 0.);

    const GfVec4f oxTrans = ox * transform;
    const GfVec4f oyTrans = oy * transform;

    float scaleFactor = oxTrans.GetLength() * oyTrans.GetLength();
    if (scaleFactor == 0.0f) {
        scaleFactor = 1.0f;
    }
    return scaleFactor;
}

float GetCylinderLightNormalization(GfMatrix4f const& transform, float length, float radius) {
    const auto scaledLength = GfVec3d(transform[0][0], transform[1][0], transform[2][0]).GetLength() * length;
    const auto scaledRadiusX = GfVec3d(transform[0][1], transform[1][1], transform[2][1]).GetLength() * radius;
    const auto scaledRadiusY = GfVec3d(transform[0][2], transform[1][2], transform[2][2]).GetLength() * radius;

    float scaleFactor = 1.0f;
    if (scaledRadiusX != 0.0 && scaledRadiusY != 0.0 && scaledLength != 0.0) {
        constexpr float unitCylinderArea = /* 2 * capArea */ 2.0f * M_PI + /* sideArea */ 2.0f * M_PI;

        float cylinderArea = 1.0f;
        if (std::abs(scaledRadiusX - scaledRadiusY) < 1e4) {
            float capArea = M_PI * scaledRadiusX * scaledRadiusX;
            float sideArea = 2.0f * M_PI * scaledRadiusX * scaledLength;
            cylinderArea = 2.0f * capArea + sideArea;
        } else {
            float capArea = M_PI * scaledRadiusX * scaledRadiusY;
            // Use Ramanujan approximation to calculate ellipse circumference
            float h = (scaledRadiusX - scaledRadiusY) / (scaledRadiusX + scaledRadiusY); // might be unstable due to finite precision, consider formula transformation
            float circumference = M_PI * (scaledRadiusX + scaledRadiusY) * (1.0f + (3.0f * h) / (10.0f + std::sqrt(4.0f - 3.0f * h)));
            float sideArea = circumference * scaledLength;
            cylinderArea = 2.0f * capArea + sideArea;
        }

        scaleFactor = cylinderArea / unitCylinderArea;
    }
    return scaleFactor;
}

float ComputeLightIntensity(float intensity, float exposure, bool sameWithKarma, bool normalizeIntensity, TfToken lightType) {
    intensity *= exp2(exposure);
    int multiplier = 1;
    if (sameWithKarma) {
        if (!normalizeIntensity) {
            multiplier = 3; // 3 is a manually fitted coefficient to get same visual results with Karma
        }
        else if (lightType != HdPrimTypeTokens->diskLight && lightType != HdPrimTypeTokens->sphereLight && lightType != HdPrimTypeTokens->cylinderLight) {
            multiplier = 3; // 3 is a manually fitted coefficient to get same visual results with Karma
        }
    }
    return intensity * multiplier;
}

} // namespace anonymous

rpr::Shape* HdRprLight::CreateDiskLightMesh(HdRprApi* rprApi) {
    constexpr uint32_t kDiskVertexCount = 32;
    constexpr float kRadius = 0.5f;

    VtVec3fArray points;
    VtIntArray pointIndices;
    VtVec3fArray normals(1, GfVec3f(0.0f, 0.0f, -1.0f));
    VtIntArray normalIndices(kDiskVertexCount * 3, 0);
    VtIntArray vpf(kDiskVertexCount, 3);

    points.reserve(kDiskVertexCount + 1);
    pointIndices.reserve(kDiskVertexCount * 3);

    const double step = M_PI * 2.0 / kDiskVertexCount;
    for (int i = 0; i < kDiskVertexCount; ++i) {
        double angle = step * i;
        points.push_back(GfVec3f(kRadius * cos(angle), kRadius * sin(angle), 0.0f));
    }
    const int centerPointIndex = points.size();
    points.push_back(GfVec3f(0.0f));

    for (int i = 0; i < kDiskVertexCount; ++i) {
        pointIndices.push_back(i);
        pointIndices.push_back((i + 1) % kDiskVertexCount);
        pointIndices.push_back(centerPointIndex);
    }

    return rprApi->CreateMesh(points, pointIndices, normals, normalIndices, VtVec2fArray(), VtIntArray(), vpf, HdTokens->rightHanded);
}

rpr::Shape* HdRprLight::CreateRectLightMesh(HdRprApi* rprApi, bool applyTransform, GfMatrix4f const& transform) {
    constexpr float kHalfSize = 0.5f;
    VtVec3fArray points = {
        GfVec3f(kHalfSize, kHalfSize, 0.0f),
        GfVec3f(kHalfSize, -kHalfSize, 0.0f),
        GfVec3f(-kHalfSize, -kHalfSize, 0.0f),
        GfVec3f(-kHalfSize, kHalfSize, 0.0f),
    };
    VtIntArray pointIndices = {
        0, 1, 2,
        0, 2, 3
    };
    VtIntArray vpf(pointIndices.size() / 3, 3);

    if (applyTransform) {
        for (auto& position : points) {
            position = transform.Transform(position);
        }
    }

    return rprApi->CreateMesh(points, pointIndices, VtVec3fArray(), VtIntArray(), VtVec2fArray(), VtIntArray(), vpf, HdTokens->rightHanded);
}

rpr::Shape* HdRprLight::CreateSphereLightMesh(HdRprApi* rprApi) {
    auto& topology = UsdImagingGetUnitSphereMeshTopology();
    auto& points = UsdImagingGetUnitSphereMeshPoints();

    return rprApi->CreateMesh(points, topology.GetFaceVertexIndices(), VtVec3fArray(), VtIntArray(), VtVec2fArray(), VtIntArray(), topology.GetFaceVertexCounts(), topology.GetOrientation());
}

rpr::Shape* HdRprLight::CreateCylinderLightMesh(HdRprApi* rprApi) {
    auto& topology = UsdImagingGetUnitCylinderMeshTopology();
    auto& points = UsdImagingGetUnitCylinderMeshPoints();

    return rprApi->CreateMesh(points, topology.GetFaceVertexIndices(), VtVec3fArray(), VtIntArray(), VtVec2fArray(), VtIntArray(), topology.GetFaceVertexCounts(), topology.GetOrientation());
}

void HdRprLight::SyncAreaLightGeomParams(HdSceneDelegate* sceneDelegate, float* intensity) {
    bool normalizeIntensity = HdRpr_GetParam(sceneDelegate, GetId(), HdLightTokens->normalize, false);

    if (m_lightType == HdPrimTypeTokens->diskLight ||
        m_lightType == HdPrimTypeTokens->sphereLight) {
        float radius = std::abs(HdRpr_GetParam(sceneDelegate, GetId(), HdLightTokens->radius, 1.0f));

        m_localTransform = GfMatrix4f(1.0f).SetScale(GfVec3f(radius * 2.0f));

        if (normalizeIntensity) {
            if (m_lightType == HdPrimTypeTokens->diskLight) {
                (*intensity) /= GetDiskLightNormalization(m_transform, radius);
            } else {
                (*intensity) /= GetSphereLightNormalization(m_transform, radius);
            }
        }
    } else if (m_lightType == HdPrimTypeTokens->rectLight) {
        float width = std::abs(HdRpr_GetParam(sceneDelegate, GetId(), HdLightTokens->width, 1.0f));
        float height = std::abs(HdRpr_GetParam(sceneDelegate, GetId(), HdLightTokens->height, 1.0f));

        m_localTransform = GfMatrix4f(1.0f).SetScale(GfVec3f(width, height, 1.0f));

        if (normalizeIntensity) {
            (*intensity) /= GetRectLightNormalization(m_transform, width, height);
        }
    } else if (m_lightType == HdPrimTypeTokens->cylinderLight) {
        float radius = std::abs(HdRpr_GetParam(sceneDelegate, GetId(), HdLightTokens->radius, 1.0f));
        float length = std::abs(HdRpr_GetParam(sceneDelegate, GetId(), HdLightTokens->length, 1.0f));

        m_localTransform = GfMatrix4f(1.0f).SetRotate(GfRotation(GfVec3d(0.0, 1.0, 0.0), 90.0)) * GfMatrix4f(1.0f).SetScale(GfVec3f(length, radius * 2.0f, radius * 2.0f));

        if (normalizeIntensity) {
            (*intensity) /= GetCylinderLightNormalization(m_transform, length, radius);
        }
    }
}

void HdRprLight::CreateAreaLightMesh(HdRprApi* rprApi, HdSceneDelegate* sceneDelegate) {
    auto light = new AreaLight;

    if (rprApi->IsArbitraryShapedLightSupported()) {
        rpr::Shape* mesh = nullptr;
        if (m_lightType == HdPrimTypeTokens->diskLight) {
            mesh = CreateDiskLightMesh(rprApi);
        } else if (m_lightType == HdPrimTypeTokens->rectLight) {
            mesh = CreateRectLightMesh(rprApi);
        } else if (m_lightType == HdPrimTypeTokens->cylinderLight) {
            mesh = CreateCylinderLightMesh(rprApi);
        } else if (m_lightType == HdPrimTypeTokens->sphereLight) {
            mesh = CreateSphereLightMesh(rprApi);
        }

        if (mesh) {
            light->meshes.push_back(mesh);
        }
    } else {
        if (m_lightType == HdPrimTypeTokens->rectLight) {
            if (auto mesh = CreateRectLightMesh(rprApi)) {
                light->meshes.push_back(mesh);
            }
        } else if (m_lightType == HdPrimTypeTokens->diskLight) {
            // Rescale rect so that total emission power equals to emission power of approximated shape (area equality)
            // pi*(R/2)^2 = a^2 -> a = R * sqrt(pi) / 2
            if (auto mesh = CreateRectLightMesh(rprApi, true, GfMatrix4f(1.0f).SetScale(GfVec3f(sqrt(M_PI) / 2.0)))) {
                light->meshes.push_back(mesh);
            }
        } else if (m_lightType == HdPrimTypeTokens->sphereLight ||
                   m_lightType == HdPrimTypeTokens->cylinderLight) {
            // Approximate sphere and cylinder lights via cube
            constexpr float kHalfSize = 0.5f;

            GfMatrix4f sideTransforms[6] = {
                GfMatrix4f(1.0).SetRotate(GfRotation(GfVec3d(1.0, 0.0, 0.0), 90.0)).SetTranslateOnly(GfVec3f(0.0f, kHalfSize, 0.0f)), // top (XZ plane)
                GfMatrix4f(1.0).SetRotate(GfRotation(GfVec3d(1.0, 0.0, 0.0), -90.0)).SetTranslateOnly(GfVec3f(0.0f, -kHalfSize, 0.0f)), // bottom (XZ plane)
                GfMatrix4f(1.0).SetRotate(GfRotation(GfVec3d(0.0, 1.0, 0.0), -90.0)).SetTranslateOnly(GfVec3f(kHalfSize, 0.0, 0.0)), // side_0 (ZY plane, front)
                GfMatrix4f(1.0).SetRotate(GfRotation(GfVec3d(0.0, 1.0, 0.0), 90.0)).SetTranslateOnly(GfVec3f(-kHalfSize, 0.0, 0.0)), // side_1 (ZY plane, back)
                GfMatrix4f(1.0).SetRotate(GfRotation(GfVec3d(0.0, 1.0, 0.0), 180.0)).SetTranslateOnly(GfVec3f(0.0f, 0.0, kHalfSize)), // side_2 (XY plane, front)
                GfMatrix4f(1.0).SetTranslateOnly(GfVec3f(0.0f, 0.0, -kHalfSize)), // side_3 (XY plane, back)
            };

            // Rescale cube so that total emission power equals to emission power of approximated shape (area equality)
            GfMatrix4f scale(1.0f);
            if (m_lightType == HdPrimTypeTokens->sphereLight) {
                // 4*pi*(R/2)^2 = 6*a^2 -> a = R * sqrt(pi/6)
                scale.SetScale(GfVec3f(sqrt(M_PI / 6.0)));
            } else {
                // 2*pi*(R/2)^2+2*pi*(R/2)*L = 6*a^2 -> a = sqrt(pi/6 * (R^2/2+R*L)) = sqrt(pi/6 * 3/2)
                scale.SetScale(GfVec3f(sqrt(M_PI / 4.0)));
            }

            for (auto& transform : sideTransforms) {
                if (auto mesh = CreateRectLightMesh(rprApi, true, transform * scale)) {
                    light->meshes.push_back(mesh);
                }
            }
        }
    }

    HdRprGeometrySettings geomSettings = {};

    geomSettings.visibilityMask = kVisibleAll;
    auto setVisibilityFlag = [&](TfToken const& name, HdRprVisibilityFlag flag) {
        VtValue value = HdRpr_GetParam(sceneDelegate, GetId(), name);
        if (value.IsHolding<bool>()) {
            if (value.UncheckedGet<bool>()) {
                geomSettings.visibilityMask |= flag;
            } else {
                geomSettings.visibilityMask &= ~flag;
            }
        }
    };
    setVisibilityFlag(RprUsdTokens->rprObjectVisibilityCamera, kVisiblePrimary);
    setVisibilityFlag(RprUsdTokens->rprObjectVisibilityShadow, kVisibleShadow);
    setVisibilityFlag(RprUsdTokens->rprObjectVisibilityReflection, kVisibleReflection);
    setVisibilityFlag(RprUsdTokens->rprObjectVisibilityGlossyReflection, kVisibleGlossyReflection);
    setVisibilityFlag(RprUsdTokens->rprObjectVisibilityRefraction, kVisibleRefraction);
    setVisibilityFlag(RprUsdTokens->rprObjectVisibilityGlossyRefraction, kVisibleGlossyRefraction);
    setVisibilityFlag(RprUsdTokens->rprObjectVisibilityDiffuse, kVisibleDiffuse);
    setVisibilityFlag(RprUsdTokens->rprObjectVisibilityTransparent, kVisibleTransparent);

    for (auto& mesh : light->meshes) {
        rprApi->SetMeshVisibility(mesh, geomSettings.visibilityMask);
    }

    m_light = light;
}

struct HdRprLight::LightParameterSetter : public BOOST_NS::static_visitor<void> {
    HdRprApi* rprApi;
    GfVec3f const& emissionColor;
    bool emissionColorIsDirty;

    LightParameterSetter(HdRprApi* rprApi, GfVec3f const& emissionColor, bool emissionColorIsDirty)
        : rprApi(rprApi), emissionColor(emissionColor), emissionColorIsDirty(emissionColorIsDirty) {

    }

    void operator()(LightVariantEmpty) const { /*no-op*/ }
    void operator()(AreaLight* light) const {
        if (emissionColorIsDirty || !light->material) {
            if (light->material) {
                rprApi->ReleaseGeometryLightMaterial(light->material);
            }
            light->material = rprApi->CreateGeometryLightMaterial(emissionColor);
        }

        if (light->material) {
            for (auto& mesh : light->meshes) {
                rprApi->SetMeshMaterial(mesh, light->material, false);
            }
        }
    }

    template <typename T>
    void operator()(T* light) const {
        if (emissionColorIsDirty) { rprApi->SetLightColor(light, emissionColor); }
    }
};

struct HdRprLight::LightNameSetter : public BOOST_NS::static_visitor<void> {
    HdRprApi* rprApi;
    const char* name;

    LightNameSetter(HdRprApi* rprApi, const char* name)
        : rprApi(rprApi), name(name) {

    }

    void operator()(LightVariantEmpty) const { /*no-op*/ }
    void operator()(AreaLight* light) const {
        rprApi->SetName(light->material, name);
        for (auto& mesh : light->meshes) {
            rprApi->SetName(mesh, name);
        }
    }

    template <typename T>
    void operator()(T* light) const {
        rprApi->SetName(light, name);
    }
};

struct HdRprLight::LightTransformSetter : public BOOST_NS::static_visitor<> {
    HdRprApi* rprApi;
    GfMatrix4f const& transform;

    LightTransformSetter(HdRprApi* rprApi, GfMatrix4f const& transform)
        : rprApi(rprApi), transform(transform) {

    }

    void operator()(LightVariantEmpty) const {}
    void operator()(AreaLight* light) const {
        for (auto& mesh : light->meshes) {
            rprApi->SetTransform(mesh, transform);
        }
    }

    template <typename T>
    void operator()(T* light) const { rprApi->SetTransform(light, transform); }
};

void HdRprLight::Sync(HdSceneDelegate* sceneDelegate,
                          HdRenderParam* renderParam,
                          HdDirtyBits* dirtyBits) {
    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    SdfPath const& id = GetId();
    HdDirtyBits bits = *dirtyBits;

    if (bits & DirtyBits::DirtyTransform) {
#if PXR_VERSION >= 2011
        m_transform = GfMatrix4f(sceneDelegate->GetTransform(id));
#else
        m_transform = GfMatrix4f(HdRpr_GetParam(sceneDelegate, id, HdTokens->transform).Get<GfMatrix4d>());
#endif
    }

    if (bits & DirtyParams) {
        m_localTransform = GfMatrix4f(1.0f);
        ReleaseLight(rprApi);

        bool isVisible = sceneDelegate->GetVisible(id);
        if (!isVisible) {
            *dirtyBits = DirtyBits::Clean;
            return;
        }

        bool newLight = false;
        rpr::Light* lightPtr = nullptr;
        auto iesFile = HdRpr_GetParam(sceneDelegate, id, USD_LUX_TOKEN_SHAPING_IES_FILE);
        if (iesFile.IsHolding<SdfAssetPath>()) {
            auto& path = iesFile.UncheckedGet<SdfAssetPath>();
            if (!path.GetResolvedPath().empty()) {
                if (auto light = rprApi->CreateIESLight(path.GetResolvedPath())) {
                    m_localTransform *= GfMatrix4f(1.0).SetRotate(GfRotation(GfVec3d(1, 0, 0), 90));
                    m_light = light;
                    newLight = true;
                    lightPtr = light;
                }
            }
        } else {
            auto coneAngle = HdRpr_GetParam(sceneDelegate, id, USD_LUX_TOKEN_SHAPING_CONE_ANGLE);
            auto coneSoftness = HdRpr_GetParam(sceneDelegate, id, USD_LUX_TOKEN_SHAPING_CONE_SOFTNESS);
            if (coneAngle.IsHolding<float>() && coneSoftness.IsHolding<float>()) {
                if (auto light = rprApi->CreateSpotLight(coneAngle.UncheckedGet<float>(), coneSoftness.UncheckedGet<float>())) {
                    m_light = light;
                    newLight = true;
                    lightPtr = light;
                }
            } else if (HdRpr_GetParam(sceneDelegate, id, UsdLuxTokens->treatAsPoint).GetWithDefault(false)) {
                if (auto light = rprApi->CreatePointLight()) {
                    m_light = light;
                    newLight = true;
                    lightPtr = light;
                }
            } else {
                if (rprApi->IsSphereAndDiskLightSupported() &&
                    (m_lightType == HdPrimTypeTokens->sphereLight ||
                    m_lightType == HdPrimTypeTokens->diskLight)) {

                    if (m_lightType == HdPrimTypeTokens->sphereLight) {
                        if (auto light = rprApi->CreateSphereLight()) {
                            rprApi->SetLightRadius(light, 0.5f);

                            m_light = light;
                            newLight = true;
                            lightPtr = light;
                        }
                    } else {
                        if (auto light = rprApi->CreateDiskLight()) {
                            rprApi->SetLightRadius(light, 0.5f);
                            rprApi->SetLightAngle(light, float(M_PI_2));

                            m_light = light;
                            newLight = true;
                            lightPtr = light;
                        }
                    }

                } else {
                    CreateAreaLightMesh(rprApi, sceneDelegate);
                    newLight = true;
                }
            }
        }

        if (m_light.type() == typeid(LightVariantEmpty)) {
            *dirtyBits = DirtyBits::Clean;
            return;
        }

        float intensity = HdRpr_GetParam(sceneDelegate, id, HdLightTokens->intensity, 1.0f);
        float exposure = HdRpr_GetParam(sceneDelegate, id, HdLightTokens->exposure, 1.0f);
        bool sameWithKarma = HdRpr_GetParam(sceneDelegate, id, RprUsdTokens->rprLightIntensitySameWithKarma, false);
        bool normalizeIntensity = HdRpr_GetParam(sceneDelegate, id, HdLightTokens->normalize, false);
        intensity = ComputeLightIntensity(intensity, exposure, sameWithKarma, normalizeIntensity, m_lightType);

        GfVec3f color = HdRpr_GetParam(sceneDelegate, id, HdPrimvarRoleTokens->color, GfVec3f(1.0f));
        if (HdRpr_GetParam(sceneDelegate, id, HdLightTokens->enableColorTemperature, false)) {
            GfVec3f temperatureColor = UsdLuxBlackbodyTemperatureAsRgb(HdRpr_GetParam(sceneDelegate, id, HdLightTokens->colorTemperature, 5000.0f));
            color[0] *= temperatureColor[0];
            color[1] *= temperatureColor[1];
            color[2] *= temperatureColor[2];
        }

        if (m_light.type() == typeid(AreaLight*) ||
            m_light.type() == typeid(rpr::SphereLight*) ||
            m_light.type() == typeid(rpr::DiskLight*)) {
            SyncAreaLightGeomParams(sceneDelegate, &intensity);
        }

        auto emissionColor = color * intensity;
        bool isEmissionColorDirty = newLight || m_emisionColor != emissionColor;
        if (isEmissionColorDirty) { m_emisionColor = emissionColor; }

        BOOST_NS::apply_visitor(LightParameterSetter{rprApi, emissionColor, isEmissionColorDirty}, m_light);

        if (newLight && RprUsdIsLeakCheckEnabled()) {
            BOOST_NS::apply_visitor(LightNameSetter{rprApi, id.GetText()}, m_light);
        }

        if (newLight) {
            RprUsdLightRegistry::Register(id, lightPtr);
        }
    }

    if (bits & (DirtyTransform | DirtyParams)) {
        BOOST_NS::apply_visitor(LightTransformSetter{rprApi, m_localTransform * m_transform}, m_light);
    }

    *dirtyBits = DirtyBits::Clean;
}


HdDirtyBits HdRprLight::GetInitialDirtyBitsMask() const {
    return DirtyBits::DirtyTransform
         | DirtyBits::DirtyParams;
}

struct HdRprLight::LightReleaser : public BOOST_NS::static_visitor<> {
    HdRprApi* rprApi;

    LightReleaser(HdRprApi* rprApi) : rprApi(rprApi) {}

    void operator()(LightVariantEmpty) const { /*no-op*/ }
    void operator()(AreaLight* light) const {
        for (auto& mesh : light->meshes) {
            rprApi->Release(mesh);
        }
        rprApi->ReleaseGeometryLightMaterial(light->material);
        delete light;
    }

    template <typename T>
    void operator()(T* light) const { rprApi->Release(light); }
};

void HdRprLight::ReleaseLight(HdRprApi* rprApi) {
    RprUsdLightRegistry::Release(GetId());
    BOOST_NS::apply_visitor(LightReleaser{rprApi}, m_light);
    m_light = LightVariantEmpty{};
}

void HdRprLight::Finalize(HdRenderParam* renderParam) {
    auto rprApi = static_cast<HdRprRenderParam*>(renderParam)->AcquireRprApiForEdit();
    ReleaseLight(rprApi);

    HdLight::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
