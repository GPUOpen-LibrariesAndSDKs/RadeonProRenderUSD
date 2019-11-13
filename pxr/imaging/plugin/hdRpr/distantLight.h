#ifndef HDRPR_DISTANT_LIGHT_H
#define HDRPR_DISTANT_LIGHT_H

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4f.h"
#include "pxr/imaging/hd/sprim.h"
#include "pxr/usd/sdf/path.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprDistantLight : public HdSprim {

public:
    HdRprDistantLight(SdfPath const& id)
        : HdSprim(id) {

    }

    void Sync(HdSceneDelegate* sceneDelegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Finalize(HdRenderParam* renderParam) override;

protected:
    RprApiObjectPtr m_rprLight;
    GfMatrix4f m_transform;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_DISTANT_LIGHT_H
