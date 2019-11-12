#include "rectLight.h"
#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

bool HdRprRectLight::SyncGeomParams(HdSceneDelegate* sceneDelegate, SdfPath const& id) {
    float width = std::abs(sceneDelegate->GetLightParamValue(id, HdLightTokens->width).Get<float>());
    float height = std::abs(sceneDelegate->GetLightParamValue(id, HdLightTokens->height).Get<float>());

    bool isDirty = (width != m_width || m_height != height);

    m_width = width;
    m_height = height;

    return isDirty;
}

RprApiObjectPtr HdRprRectLight::CreateLightMesh() {
    auto rprApi = m_rprApiWeakPtr.lock();
    if (!rprApi) {
        TF_CODING_ERROR("RprApi is expired");
        return nullptr;
    }
    return rprApi->CreateRectLightMesh(m_width, m_height);
}

GfVec3f HdRprRectLight::NormalizeLightColor(const GfMatrix4d& transform, const GfVec3f& inColor) {
    const GfVec4d ox(m_width, 0., 0., 0.);
    const GfVec4d oy(0., m_height, 0., 0.);

    const GfVec4d oxTrans = ox * transform;
    const GfVec4d oyTrans = oy * transform;

    return static_cast<float>(1. / (oxTrans.GetLength() * oyTrans.GetLength())) * inColor;
}

PXR_NAMESPACE_CLOSE_SCOPE
