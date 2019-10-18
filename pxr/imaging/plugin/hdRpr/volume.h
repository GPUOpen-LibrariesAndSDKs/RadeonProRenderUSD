#ifndef HDRPR_VOLUME_H
#define HDRPR_VOLUME_H

#include "rprApi.h"

#include "pxr/imaging/hd/volume.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprVolume : public HdVolume {
public:
    HD_API
	HdRprVolume(SdfPath const& id, HdRprApiSharedPtr rprApi);

    HD_API
    virtual ~HdRprVolume();

	virtual void Sync(
		HdSceneDelegate* sceneDelegate,
		HdRenderParam*   renderParam,
		HdDirtyBits*     dirtyBits,
		TfToken const&   reprName
	) override;

protected:

	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

	virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

	virtual void _InitRepr(TfToken const &reprName,
		HdDirtyBits *dirtyBits) override;

	HdRprApiWeakPtr m_rprApiWeakPtr;
	RprApiObjectPtr m_rprHeteroVolume;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HDRPR_VOLUME_H
