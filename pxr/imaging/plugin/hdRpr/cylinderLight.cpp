#include "cylinderLight.h"
#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

const TfTokenVector k_requiredGeometryParam =
{
    HdLightTokens->radius,
    HdLightTokens->length,
};


bool HdRprCylinderLight::IsDirtyGeomParam(std::map<TfToken, float>& params) {
    auto radiusParamIter = params.find(HdLightTokens->radius);
    if (radiusParamIter == params.end()) {
        return false;
    }
    auto lengthParamIter = params.find(HdLightTokens->length);
    if (lengthParamIter == params.end()) {
        return false;
    }

    float radius = radiusParamIter->second;
    float length = lengthParamIter->second;

    bool isDirty = radius != m_radius || length != m_length;

    m_radius = radius;
    m_length = length;

    return isDirty;
}

const TfTokenVector& HdRprCylinderLight::FetchLightGeometryParamNames() const {
    return k_requiredGeometryParam;
}

RprApiObjectPtr HdRprCylinderLight::CreateLightMesh(std::map<TfToken, float>& params) {

    HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
    if (!rprApi) {
        TF_CODING_ERROR("RprApi is expired");
        return nullptr;
    }

    return rprApi->CreateCylinderLightMesh(params[HdLightTokens->radius], params[HdLightTokens->length]);
}

GfVec3f HdRprCylinderLight::NormalizeLightColor(const GfMatrix4d& transform, std::map<TfToken, float>& params, const GfVec3f& inColor) {
    float radius = params[HdLightTokens->radius];
    if (radius <= 0.0f) {
        radius = 1.0f;
    }

    float length = params[HdLightTokens->length];
    if (length <= 0.0f) {
        length = 1.0f;
    }

    const double sx = GfVec3d(transform[0][0], transform[1][0], transform[2][0]).GetLength() * radius;
    const double sy = GfVec3d(transform[0][1], transform[1][1], transform[2][1]).GetLength() * radius;
    const double sz = GfVec3d(transform[0][2], transform[1][2], transform[2][2]).GetLength() * length;

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
