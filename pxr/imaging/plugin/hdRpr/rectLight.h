#ifndef HDRPR_RECT_LIGHT_H
#define HDRPR_RECT_LIGHT_H

#include "lightBase.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprRectLight : public HdRprLightBase {

public:
    HdRprRectLight(SdfPath const& id, HdRprApiSharedPtr rprApi)
        : HdRprLightBase(id, rprApi) {
    }

protected:

    virtual bool IsDirtyGeomParam(std::map<TfToken, float>& params) override;

    // Fetch required params for geometry
    virtual const TfTokenVector& FetchLightGeometryParamNames() const override;

    // Create light mesh which is required to be set emmisive material 
    RprApiObjectPtr CreateLightMesh() override;

    // Normalize Light Color with surface area
    virtual GfVec3f NormalizeLightColor(const GfMatrix4d& transform, const GfVec3f& emmisionColor) override;

private:
    float m_width = std::numeric_limits<float>::quiet_NaN();
    float m_height = std::numeric_limits<float>::quiet_NaN();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RECT_LIGHT_H
