#ifndef HDRPR_DOME_LIGHT_H
#define HDRPR_DOME_LIGHT_H

#include "pxr/pxr.h"

#include "pxr/imaging/hd/sprim.h"
#include "pxr/usd/sdf/path.h"


#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprDomeLight : public HdSprim {
	
public:
	HdRprDomeLight(SdfPath const & id, HdRprApiSharedPtr rprApi)
		: HdSprim(id)
		, m_rprApiWeakPrt(rprApi) {}

	// change tracking for HdStLight
	enum DirtyBits : HdDirtyBits {
		Clean = 0,
		DirtyTransform = 1 << 0,
		DirtyParams = 1 << 1,
		AllDirty = (DirtyTransform
		| DirtyParams)
	};

	/// Synchronizes state from the delegate to this object.
	/// @param[in, out]  dirtyBits: On input specifies which state is
	///                             is dirty and can be pulled from the scene
	///                             delegate.
	///                             On output specifies which bits are still
	///                             dirty and were not cleaned by the sync. 
	///                             
	virtual void Sync(HdSceneDelegate *sceneDelegate,
		HdRenderParam   *renderParam,
		HdDirtyBits     *dirtyBits) override;

	/// Returns the minimal set of dirty bits to place in the
	/// change tracker for use in the first sync of this prim.
	/// Typically this would be all dirty bits.
	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
	HdRprApiWeakPtr m_rprApiWeakPrt;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_DOME_LIGHT_H
