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
#include "primvarUtil.h"
#include "rprApi.h"

#include "pxr/imaging/rprUsd/debugCodes.h"
#include "pxr/imaging/rprUsd/lightRegistry.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/usd/usdLux/blackbody.h"
#include "pxr/base/gf/matrix4d.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (angle)
);

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
#if PXR_VERSION >= 2011
        m_transform = GfMatrix4f(sceneDelegate->GetTransform(id));
#else
        m_transform = GfMatrix4f(HdRpr_GetParam(sceneDelegate, id, HdTokens->transform).Get<GfMatrix4d>());
#endif
    }

    bool newLight = false;
    if (bits & HdLight::DirtyParams) {
        bool isVisible = sceneDelegate->GetVisible(id);
        if (!isVisible) {
            if (m_rprLight) {
                rprApi->Release(m_rprLight);
                m_rprLight = nullptr;
            }

            *dirtyBits = HdLight::Clean;
            return;
        }

        float intensity = HdRpr_GetParam(sceneDelegate, id, HdLightTokens->intensity, 1.0f);
        float exposure = HdRpr_GetParam(sceneDelegate, id, HdLightTokens->exposure, 1.0f);
        float computedIntensity = computeLightIntensity(intensity, exposure);

        GfVec3f color = HdRpr_GetParam(sceneDelegate, id, HdPrimvarRoleTokens->color, GfVec3f(1.0f));
        if (HdRpr_GetParam(sceneDelegate, id, HdLightTokens->enableColorTemperature, false)) {
            GfVec3f temperatureColor = UsdLuxBlackbodyTemperatureAsRgb(HdRpr_GetParam(sceneDelegate, id, HdLightTokens->colorTemperature, 5000.0f));
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

            if (RprUsdIsLeakCheckEnabled()) {
                rprApi->SetName(m_rprLight, id.GetText());
            }
        }

        float angle = HdRpr_GetParam(sceneDelegate, id, _tokens->angle, 3.0f);

        rprApi->SetDirectionalLightAttributes(m_rprLight, color * computedIntensity, angle * (M_PI / 180.0));

        newLight = true;
        RprUsdLightRegistry::Register(id, m_rprLight);
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
        RprUsdLightRegistry::Release(GetId());
        auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
        rprRenderParam->AcquireRprApiForEdit()->Release(m_rprLight);
        m_rprLight = nullptr;
    }

    HdSprim::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
