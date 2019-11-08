#ifndef HDRPR_CYLINDER_LIGHT_H
#define HDRPR_CYLINDER_LIGHT_H

#include "lightBase.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprCylinderLight : public HdRprLightBase {

public:
    HdRprCylinderLight(SdfPath const& id, HdRprApiSharedPtr rprApi)
        : HdRprLightBase(id, rprApi) {
    }

protected:

    bool IsDirtyGeomParam(std::map<TfToken, float>& params) override;

    // Fetch required params for geometry
    const TfTokenVector& FetchLightGeometryParamNames() const override;

    // Create mesh with emmisive material
    RprApiObjectPtr CreateLightMesh() override;

    // Normalize Light Color with surface area
    GfVec3f NormalizeLightColor(const GfMatrix4d& transform, const GfVec3f& emmisionColor) override;

private:
    float m_radius = std::numeric_limits<float>::quiet_NaN();
    float m_length = std::numeric_limits<float>::quiet_NaN();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_CYLINDER_LIGHT_H
