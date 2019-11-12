#ifndef HDRPR_DISK_LIGHT_H
#define HDRPR_DISK_LIGHT_H

#include "lightBase.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprDiskLight : public HdRprLightBase {
    
public:
    HdRprDiskLight(SdfPath const & id, HdRprApiSharedPtr rprApi)
        : HdRprLightBase(id, rprApi) {}

protected:

    bool SyncGeomParams(HdSceneDelegate* sceneDelegate, SdfPath const& id) override;

    // Create mesh with emmisive material
    RprApiObjectPtr CreateLightMesh() override;

    // Normalize Light Color with surface area
    GfVec3f NormalizeLightColor(const GfMatrix4d & transform, const GfVec3f & emmisionColor) override;

private:
    float m_radius = std::numeric_limits<float>::quiet_NaN();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_DISK_LIGHT_H
