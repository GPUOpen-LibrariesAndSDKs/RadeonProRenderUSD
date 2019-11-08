#ifndef HDRPR_VOLUME_H
#define HDRPR_VOLUME_H

#include "pxr/imaging/hd/volume.h"
#include "pxr/base/gf/matrix4f.h"

PXR_NAMESPACE_OPEN_SCOPE

class RprApiObject;
using RprApiObjectPtr = std::unique_ptr<RprApiObject>;

class HdRprVolume : public HdVolume {
public:
    HdRprVolume(SdfPath const& id);
    ~HdRprVolume() override = default;

    void Sync(
        HdSceneDelegate* sceneDelegate,
        HdRenderParam* renderParam,
        HdDirtyBits* dirtyBits,
        TfToken const& reprName
    ) override;

protected:
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

    void _InitRepr(TfToken const& reprName,
                   HdDirtyBits* dirtyBits) override;

private:
    RprApiObjectPtr m_rprHeteroVolume;
    GfMatrix4f m_transform;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HDRPR_VOLUME_H
