#include "cylinderLight.h"
#include "rprApi.h"
#include "pxr/imaging/hd/sceneDelegate.h"

#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

bool HdRprCylinderLight::SyncGeomParams(HdSceneDelegate* sceneDelegate, SdfPath const& id) {
    float radius = std::abs(sceneDelegate->GetLightParamValue(id, HdLightTokens->radius).Get<float>());
    float length = std::abs(sceneDelegate->GetLightParamValue(id, HdLightTokens->length).Get<float>());

    bool isDirty = radius != m_radius || length != m_length;

    m_radius = radius;
    m_length = length;

    return isDirty;
}

RprApiObjectPtr HdRprCylinderLight::CreateLightMesh(HdRprApi* rprApi) {
    return rprApi->CreateCylinderLightMesh(m_radius, m_length);
}

GfVec3f HdRprCylinderLight::NormalizeLightColor(const GfMatrix4f& transform, const GfVec3f& inColor) {
    const double sx = GfVec3d(transform[0][0], transform[1][0], transform[2][0]).GetLength() * m_radius;
    const double sy = GfVec3d(transform[0][1], transform[1][1], transform[2][1]).GetLength() * m_radius;
    const double sz = GfVec3d(transform[0][2], transform[1][2], transform[2][2]).GetLength() * m_length;

    if (sx == 0. && sy == 0. && sz == 0.) {
        return inColor;
    }

    constexpr float unitCylinderArea = /* 2 * capArea */ 2.0f * M_PI + /* sideArea */ 2.0f * M_PI;

    float cylinderArea = 1.0f;
    if (std::abs(sx - sy) < 1e4f) {
        float capArea = M_PI * sx * sx;
        float sideArea = 2.0f * M_PI * sx * sz;
        cylinderArea = 2.0f * capArea + sideArea;
    } else {
        float capArea = M_PI * sx * sy;
        // Use Ramanujan approximation to calculate ellipse circumference
        float h = (sx - sy) / (sx + sy); // might be unstable due to finite precision, consider formula transformation
        float circumference = M_PI * (sx + sy) * (1.0f + (3.0f * h) / (10.0f + std::sqrt(4.0f - 3.0f * h)));
        float sideArea = circumference * sz;
        cylinderArea = 2.0f * capArea + sideArea;
    }

    float scaleFactor = cylinderArea / unitCylinderArea;
    return inColor / scaleFactor;
}

PXR_NAMESPACE_CLOSE_SCOPE
