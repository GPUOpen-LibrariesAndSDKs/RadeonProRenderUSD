#include "diskLight.h"
#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

const TfTokenVector k_requiredGeometryParam = {
    HdLightTokens->radius,
};

bool HdRprDiskLight::IsDirtyGeomParam(std::map<TfToken, float>& params) {
    auto radiusParamIter = params.find(HdLightTokens->radius);
    if (radiusParamIter == params.end()) {
        return false;
    }

    float radius = std::abs(radiusParamIter->second);

    bool isDirty = radius != m_radius;

    m_radius = radius;

    return isDirty;
}

const TfTokenVector& HdRprDiskLight::FetchLightGeometryParamNames() const {
    return k_requiredGeometryParam;
}

RprApiObjectPtr HdRprDiskLight::CreateLightMesh(std::map<TfToken, float>& params) {
    HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
    if (!rprApi) {
        TF_CODING_ERROR("RprApi is expired");
        return nullptr;
    }

    return rprApi->CreateDiskLightMesh(params[HdLightTokens->radius]);
}

GfVec3f HdRprDiskLight::NormalizeLightColor(const GfMatrix4d& transform, const GfVec3f& inColor) {
    const double sx = GfVec3d(transform[0][0], transform[1][0], transform[2][0]).GetLength() * m_radius;
    const double sy = GfVec3d(transform[0][1], transform[1][1], transform[2][1]).GetLength() * m_radius;

    if (sx == 0. && sy == 0.) {
        return inColor;
    }

    constexpr float unitDiskArea = M_PI;
    float diskArea = M_PI * sx * sy;
    float scaleFactor = diskArea / unitDiskArea;
    return inColor / scaleFactor;
}

PXR_NAMESPACE_CLOSE_SCOPE
