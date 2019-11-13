#include "rectLight.h"
#include "rprApi.h"
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

RprApiObjectPtr HdRprRectLight::CreateLightMesh(HdRprApi* rprApi) {
    return rprApi->CreateRectLightMesh(m_width, m_height);
}

GfVec3f HdRprRectLight::NormalizeLightColor(const GfMatrix4f& transform, const GfVec3f& inColor) {
    const GfVec4f ox(m_width, 0., 0., 0.);
    const GfVec4f oy(0., m_height, 0., 0.);

    const GfVec4f oxTrans = ox * transform;
    const GfVec4f oyTrans = oy * transform;

    return static_cast<float>(1. / (oxTrans.GetLength() * oyTrans.GetLength())) * inColor;
}

PXR_NAMESPACE_CLOSE_SCOPE
