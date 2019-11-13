#ifndef HDRPR_FIELD_H
#define HDRPR_FIELD_H

#include "pxr/imaging/hd/field.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprField : public HdField {
public:
    HdRprField(SdfPath const& id);
    ~HdRprField() override = default;

    void Sync(HdSceneDelegate* sceneDelegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

protected:
    HdDirtyBits GetInitialDirtyBitsMask() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_FIELD_H
