#include "lightBase.h"
#include "renderParam.h"
#include "material.h"
#include "materialFactory.h"
#include "rprApi.h"

#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/usd/usdLux/blackbody.h"

PXR_NAMESPACE_OPEN_SCOPE

static float computeLightIntensity(float intensity, float exposure) {
    return intensity * exp2(exposure);
}

bool HdRprLightBase::IsDirtyMaterial(const GfVec3f& emissionColor) {
    bool isDirty = (m_emmisionColor != emissionColor);
    m_emmisionColor = emissionColor;
    return isDirty;
}

void HdRprLightBase::Sync(HdSceneDelegate* sceneDelegate,
                          HdRenderParam* renderParam,
                          HdDirtyBits* dirtyBits) {

    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    SdfPath const& id = GetId();
    HdDirtyBits bits = *dirtyBits;

    if (bits & DirtyBits::DirtyTransform) {
        m_transform = GfMatrix4f(sceneDelegate->GetLightParamValue(id, HdLightTokens->transform).Get<GfMatrix4d>());
    }

    bool newLight = false;
    if (bits & DirtyBits::DirtyParams) {
        // Get the color of the light
        GfVec3f color = sceneDelegate->GetLightParamValue(id, HdPrimvarRoleTokens->color).Get<GfVec3f>();

        // Extract intensity
        const float intensity = sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity).Get<float>();

        // Extract the exposure of the light
        const float exposure = sceneDelegate->GetLightParamValue(id, HdLightTokens->exposure).Get<float>();

        // Get the colorTemerature of the light
        if (sceneDelegate->GetLightParamValue(id, HdLightTokens->enableColorTemperature).Get<bool>()) {
            GfVec3f temperatureColor = UsdLuxBlackbodyTemperatureAsRgb(sceneDelegate->GetLightParamValue(id, HdLightTokens->colorTemperature).Get<float>());
            color[0] *= temperatureColor[0];
            color[1] *= temperatureColor[1];
            color[2] *= temperatureColor[2];
        }

        // Compute
        const float illuminationIntensity = computeLightIntensity(intensity, exposure);

        if (SyncGeomParams(sceneDelegate, id) || !m_lightMesh) {
            m_lightMesh = CreateLightMesh(rprApi);
        }

        if (!m_lightMesh) {
            TF_CODING_ERROR("Light Mesh was not created");
            *dirtyBits = DirtyBits::Clean;
            return;
        }

        const bool isNormalize = sceneDelegate->GetLightParamValue(id, HdLightTokens->normalize).Get<bool>();
        const GfVec3f illumColor = color * illuminationIntensity;
        const GfVec3f emissionColor = (isNormalize) ? NormalizeLightColor(m_transform, illumColor) : illumColor;

        if (!m_lightMaterial || IsDirtyMaterial(emissionColor)) {
            MaterialAdapter matAdapter(EMaterialType::EMISSIVE, MaterialParams{{HdLightTokens->color, VtValue(emissionColor)}});
            auto lightMaterial = rprApi->CreateMaterial(matAdapter);
            if (!m_lightMaterial && lightMaterial) {
                rprRenderParam->AddLight();
            }
            m_lightMaterial = std::move(lightMaterial);
        }

        if (!m_lightMaterial) {
            TF_CODING_ERROR("Light material was not created");
        }

        rprApi->SetMeshMaterial(m_lightMesh.get(), m_lightMaterial.get(), false, false);
        newLight = true;
    }

    if (newLight || ((bits & DirtyTransform) && m_lightMesh)) {
        rprApi->SetMeshTransform(m_lightMesh.get(), m_transform);
    }

    *dirtyBits = DirtyBits::Clean;
}


HdDirtyBits HdRprLightBase::GetInitialDirtyBitsMask() const {
    return DirtyBits::DirtyTransform
        | DirtyBits::DirtyParams;
}

void HdRprLightBase::Finalize(HdRenderParam* renderParam) {
    // Stop render thread to safely release resources
    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    rprRenderParam->GetRenderThread()->StopRender();
    rprRenderParam->RemoveLight();

    HdLight::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
