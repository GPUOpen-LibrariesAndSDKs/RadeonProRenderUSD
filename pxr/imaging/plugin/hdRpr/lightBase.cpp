#include "lightBase.h"
#include "material.h"
#include "materialFactory.h"

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

RprApiObjectPtr HdRprLightBase::CreateLightMaterial(const GfVec3f& illumColor) {
    auto rprApi = m_rprApiWeakPtr.lock();
    if (!rprApi) {
        TF_CODING_ERROR("RprApi is expired");
        return nullptr;
    }

    MaterialAdapter matAdapter(EMaterialType::EMISSIVE, MaterialParams{{HdLightTokens->color, VtValue(illumColor)}});
    return rprApi->CreateMaterial(matAdapter);
}

void HdRprLightBase::Sync(HdSceneDelegate* sceneDelegate,
                          HdRenderParam* renderParam,
                          HdDirtyBits* dirtyBits) {

    auto rprApi = m_rprApiWeakPtr.lock();
    if (!rprApi) {
        TF_CODING_ERROR("RprApi is expired");
        return;
    }

    SdfPath const& id = GetId();
    HdDirtyBits bits = *dirtyBits;

    if (bits & DirtyBits::DirtyTransform) {
        m_transform = sceneDelegate->GetLightParamValue(id, HdLightTokens->transform).Get<GfMatrix4d>();
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

        const TfTokenVector& paramNames = FetchLightGeometryParamNames();

        std::map<TfToken, float> params;
        for (const TfToken& paramName : paramNames) {
            params[paramName] = sceneDelegate->GetLightParamValue(id, paramName).Get<float>();
        }

        if (!m_lightMesh || this->IsDirtyGeomParam(params)) {
            m_lightMesh = CreateLightMesh();
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
            m_lightMaterial = CreateLightMaterial(emissionColor);
        }

        if (!m_lightMaterial) {
            TF_CODING_ERROR("Light material was not created");
            *dirtyBits = DirtyBits::Clean;
            return;
        }

        rprApi->SetMeshMaterial(m_lightMesh.get(), m_lightMaterial.get());
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

PXR_NAMESPACE_CLOSE_SCOPE
