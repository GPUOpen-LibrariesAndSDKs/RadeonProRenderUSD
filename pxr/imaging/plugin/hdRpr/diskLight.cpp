#include "diskLight.h"
#include "pxr/imaging/hd/sceneDelegate.h"

#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

const TfTokenVector k_requiredGeometryParam = {
    HdLightTokens->radius,
};

bool HdRprDiskLight::SyncGeomParams(HdSceneDelegate* sceneDelegate, SdfPath const& id) {
    float radius = std::abs(sceneDelegate->GetLightParamValue(id, HdLightTokens->radius).Get<float>());

    bool isDirty = radius != m_radius;

    m_radius = radius;

    return isDirty;
}

RprApiObjectPtr HdRprDiskLight::CreateLightMesh() {
    HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
    if (!rprApi) {
        TF_CODING_ERROR("RprApi is expired");
        return nullptr;
    }

    return rprApi->CreateDiskLightMesh(m_radius);
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