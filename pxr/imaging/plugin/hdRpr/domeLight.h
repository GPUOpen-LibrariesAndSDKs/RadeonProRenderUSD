#ifndef HDRPR_DOME_LIGHT_H
#define HDRPR_DOME_LIGHT_H

#include "pxr/base/gf/matrix4f.h"
#include "pxr/imaging/hd/sprim.h"
#include "pxr/usd/sdf/path.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;
class RprApiObject;
using RprApiObjectPtr = std::unique_ptr<RprApiObject>;

class HdRprDomeLight : public HdSprim {

public:
    HdRprDomeLight(SdfPath const& id)
        : HdSprim(id) {

    }

    void Sync(HdSceneDelegate* sceneDelegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    RprApiObjectPtr m_rprLight;
    GfMatrix4f m_transform;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_DOME_LIGHT_H
