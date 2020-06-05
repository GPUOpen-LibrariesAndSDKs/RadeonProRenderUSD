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

#include "distantLight.h"
#include "renderParam.h"
#include "rprApi.h"

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
        }

        float angle = sceneDelegate->GetLightParamValue(id, UsdLuxTokens->angle).Get<float>();

        rprApi->SetDirectionalLightAttributes(m_rprLight, color * computedIntensity, angle * (M_PI / 180.0));

        newLight = true;
    }

    if (newLight || ((bits & HdLight::DirtyTransform) && m_rprLight)) {
        rprApi->SetTransform(m_rprLight, m_transform);
    }

    *dirtyBits = HdLight::Clean;
}


HdDirtyBits HdRprDistantLight::GetInitialDirtyBitsMask() const {
    return HdLight::AllDirty;
}

void HdRprDistantLight::Finalize(HdRenderParam* renderParam) {
    if (m_rprLight) {
        auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
        rprRenderParam->AcquireRprApiForEdit()->Release(m_rprLight);
        m_rprLight = nullptr;
    }

    HdSprim::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
