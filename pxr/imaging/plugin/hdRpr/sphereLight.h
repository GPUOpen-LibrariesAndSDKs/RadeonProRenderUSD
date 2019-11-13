#ifndef HDRPR_SPHERE_LIGHT_H
#define HDRPR_SPHERE_LIGHT_H

#include "lightBase.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprSphereLight : public HdRprLightBase {

public:
    HdRprSphereLight(SdfPath const& id)
        : HdRprLightBase(id) {
    }

protected:
    bool SyncGeomParams(HdSceneDelegate* sceneDelegate, SdfPath const& id) override;

    // Create mesh with emmisive material
    RprApiObjectPtr CreateLightMesh(HdRprApi* rprApi) override;

    // Normalize Light Color with surface area
    GfVec3f NormalizeLightColor(const GfMatrix4f& transform, const GfVec3f& emmisionColor) override;

private:
    float m_radius = std::numeric_limits<float>::quiet_NaN();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_SPHERE_LIGHT_H
