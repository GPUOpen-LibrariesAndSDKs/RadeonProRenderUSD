#include "distantLight.h"
#include "renderParam.h"

#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/usd/usdLux/blackbody.h"
#include "pxr/usd/usdLux/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

static float computeLightIntensity(float intensity, float exposure) {
    return intensity * exp2(exposure);
}

void HdRprDistantLight::Sync(HdSceneDelegate* sceneDelegate,
                             HdRenderParam* renderParam,
                             HdDirtyBits* dirtyBits) {

    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    HdDirtyBits bits = *dirtyBits;
    auto& id = GetId();

    if (bits & HdLight::DirtyTransform) {
        m_transform = GfMatrix4f(sceneDelegate->GetLightParamValue(id, HdLightTokens->transform).Get<GfMatrix4d>());
    }

    bool newLight = false;
    if (bits & HdLight::DirtyParams) {
        float intensity = sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity).Get<float>();
        float exposure = sceneDelegate->GetLightParamValue(id, HdLightTokens->exposure).Get<float>();
        float computedIntensity = computeLightIntensity(intensity, exposure);

        GfVec3f color = sceneDelegate->GetLightParamValue(id, HdPrimvarRoleTokens->color).Get<GfVec3f>();
        if (sceneDelegate->GetLightParamValue(id, HdLightTokens->enableColorTemperature).Get<bool>()) {
            GfVec3f temperatureColor = UsdLuxBlackbodyTemperatureAsRgb(sceneDelegate->GetLightParamValue(id, HdLightTokens->colorTemperature).Get<float>());
            color[0] *= temperatureColor[0];
            color[1] *= temperatureColor[1];
            color[2] *= temperatureColor[2];
        }

        if (!m_rprLight) {
            m_rprLight = rprApi->CreateDirectionalLight();
            if (!m_rprLight) {
                TF_CODING_ERROR("Directional light was not created");
                *dirtyBits = HdLight::Clean;
                return;
            }
            rprRenderParam->AddLight();
        }

        float angle = sceneDelegate->GetLightParamValue(id, UsdLuxTokens->angle).Get<float>();
        // TODO: implement physically correct conversion
        float shadowSoftness = std::min(angle * (M_PI / 180.0) * M_PI, 1.0);

        rprApi->SetDirectionalLightAttributes(m_rprLight.get(), color * computedIntensity, shadowSoftness);

        newLight = true;
    }

    if (newLight || ((bits & HdLight::DirtyTransform) && m_rprLight)) {
        rprApi->SetLightTransform(m_rprLight.get(), m_transform);
    }

    *dirtyBits = HdLight::Clean;
}


HdDirtyBits HdRprDistantLight::GetInitialDirtyBitsMask() const {
    return HdLight::AllDirty;
}

void HdRprDistantLight::Finalize(HdRenderParam* renderParam) {
    // Stop render thread to safely release resources
    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    rprRenderParam->GetRenderThread()->StopRender();
    rprRenderParam->RemoveLight();

    HdSprim::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
