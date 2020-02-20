#ifndef HDRPR_VOLUME_H
#define HDRPR_VOLUME_H

#include "pxr/imaging/hd/volume.h"
#include "pxr/base/gf/matrix4f.h"

PXR_NAMESPACE_OPEN_SCOPE

struct HdRprApiVolume;

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

    void Finalize(HdRenderParam* renderParam) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

    void _InitRepr(TfToken const& reprName,
                   HdDirtyBits* dirtyBits) override;

private:
    HdRprApiVolume* m_rprVolume = nullptr;
    GfMatrix4f m_transform;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HDRPR_VOLUME_H
