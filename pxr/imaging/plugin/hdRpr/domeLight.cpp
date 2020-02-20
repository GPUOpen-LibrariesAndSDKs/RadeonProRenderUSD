#include "domeLight.h"
#include "renderParam.h"
#include "rprApi.h"

#include "pxr/usd/ar/resolver.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/usdLux/blackbody.h"
#include "pxr/base/tf/envSetting.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(HDRPR_INVERT_DOME_LIGHT_Z_AXIS, true,
    "In Houdini 18.0.287 we needed to invert X-axis of dome light to match with Karma,"
    "but in Houdini 18.0.311 this behavior is changed so now we need to invert Z-axis");

static void removeFirstSlash(std::string& string) {
    // Don't need this for *nix/Mac
#ifdef _WIN32
    if (string[0] == '/' || string[0] == '\\') {
        string.erase(0, 1);
    }
#endif
}

static float computeLightIntensity(float intensity, float exposure) {
    return intensity * exp2(exposure);
}

void HdRprDomeLight::Sync(HdSceneDelegate* sceneDelegate,
                          HdRenderParam* renderParam,
                          HdDirtyBits* dirtyBits) {

    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    SdfPath const& id = GetId();
    HdDirtyBits bits = *dirtyBits;

    if (bits & HdLight::DirtyTransform) {
        m_transform = GfMatrix4f(sceneDelegate->GetLightParamValue(id, HdLightTokens->transform).Get<GfMatrix4d>());
        // XXX: Required to match orientation with Houdini's Karma
        if (TfGetEnvSetting(HDRPR_INVERT_DOME_LIGHT_Z_AXIS)) {
            m_transform *= GfMatrix4f(1.0).SetScale(GfVec3f(1.0f, 1.0f, -1.0f));
        } else {
            m_transform *= GfMatrix4f(1.0).SetScale(GfVec3f(-1.0f, 1.0f, 1.0f));
        }
    }

    bool newLight = false;
    if (bits & HdLight::DirtyParams) {
        m_rprLight = nullptr;

        float intensity = sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity).Get<float>();
        float exposure = sceneDelegate->GetLightParamValue(id, HdLightTokens->exposure).Get<float>();
        float computedIntensity = computeLightIntensity(intensity, exposure);

        std::string texturePath;
        VtValue texturePathValue = sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFile);
        if (texturePathValue.IsHolding<SdfAssetPath>()) {
            auto& assetPath = texturePathValue.UncheckedGet<SdfAssetPath>();
            if (assetPath.GetResolvedPath().empty()) {
                texturePath = ArGetResolver().Resolve(assetPath.GetAssetPath());
            } else {
                texturePath = assetPath.GetResolvedPath();
            }
            // XXX: Why?
            removeFirstSlash(texturePath);
        } else if (texturePathValue.IsHolding<std::string>()) {
            // XXX: Is it even possible?
            texturePath = texturePathValue.UncheckedGet<std::string>();
        }

        if (texturePath.empty()) {
            GfVec3f color = sceneDelegate->GetLightParamValue(id, HdPrimvarRoleTokens->color).Get<GfVec3f>();
            if (sceneDelegate->GetLightParamValue(id, HdLightTokens->enableColorTemperature).Get<bool>()) {
                GfVec3f temperatureColor = UsdLuxBlackbodyTemperatureAsRgb(sceneDelegate->GetLightParamValue(id, HdLightTokens->colorTemperature).Get<float>());
                color[0] *= temperatureColor[0];
                color[1] *= temperatureColor[1];
                color[2] *= temperatureColor[2];
            }

            m_rprLight = rprApi->CreateEnvironmentLight(color, computedIntensity);
        } else {
            m_rprLight = rprApi->CreateEnvironmentLight(texturePath, computedIntensity);
        }

        if (m_rprLight) {
            newLight = true;
        }
    }

    if (newLight || ((bits & HdLight::DirtyTransform) && m_rprLight)) {
        rprApi->SetTransform(m_rprLight, m_transform);
    }

    if (newLight && !m_created) {
        m_created = true;
        rprRenderParam->AddLight();
    }

    *dirtyBits = HdLight::Clean;
}

HdDirtyBits HdRprDomeLight::GetInitialDirtyBitsMask() const {
    return HdLight::AllDirty;
}

void HdRprDomeLight::Finalize(HdRenderParam* renderParam) {
    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    
    if (m_rprLight) {
        rprRenderParam->AcquireRprApiForEdit()->Release(m_rprLight);
        m_rprLight = nullptr;
    }

    if (m_created) {
        rprRenderParam->RemoveLight();
        m_created = false;
    }

    HdSprim::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
