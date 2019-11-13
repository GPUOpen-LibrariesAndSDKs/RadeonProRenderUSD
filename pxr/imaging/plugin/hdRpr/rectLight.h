#ifndef HDRPR_RECT_LIGHT_H
#define HDRPR_RECT_LIGHT_H

#include "lightBase.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprRectLight : public HdRprLightBase {

public:
    HdRprRectLight(SdfPath const& id)
        : HdRprLightBase(id) {
    }

protected:

    bool SyncGeomParams(HdSceneDelegate* sceneDelegate, SdfPath const& id) override;

    // Create light mesh which is required to be set emmisive material 
    RprApiObjectPtr CreateLightMesh(HdRprApi* rprApi) override;

    // Normalize Light Color with surface area
    GfVec3f NormalizeLightColor(const GfMatrix4f& transform, const GfVec3f& emmisionColor) override;

private:
    float m_width = std::numeric_limits<float>::quiet_NaN();
    float m_height = std::numeric_limits<float>::quiet_NaN();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RECT_LIGHT_H
