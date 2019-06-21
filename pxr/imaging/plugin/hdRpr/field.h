
#ifndef HDRPR_FIELD_H
#define HDRPR_FIELD_H

#include "pxr/pxr.h"

#include "pxr/imaging/hd/field.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprField : public HdField {
public:
	HdRprField(SdfPath const& id, HdRprApiSharedPtr rprApi);
    HD_API
    virtual ~HdRprField();

	virtual void Sync(HdSceneDelegate *sceneDelegate,
		HdRenderParam   *renderParam,
		HdDirtyBits     *dirtyBits) override;

protected:
	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

	HdRprApiWeakPtr m_rprApiWeakPrt;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HDRPR_FIELD_H
