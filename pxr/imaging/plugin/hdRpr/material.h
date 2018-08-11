#ifndef HDRPR_MATERIAL_H
#define HDRPR_MATERIAL_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/material.h"

#include "rprApi.h"

#include <vector>
#include <map>

PXR_NAMESPACE_OPEN_SCOPE

class RprApiMaterial;


class HdRprMaterial final : public HdMaterial {
public:
	HdRprMaterial(SdfPath const& id, HdRprApiSharedPtr rprApi);

	virtual ~HdRprMaterial();

	/// Synchronizes state from the delegate to this object.

	virtual void Sync(HdSceneDelegate *sceneDelegate,
		HdRenderParam   *renderParam,
		HdDirtyBits     *dirtyBits) override;

	/// Returns the minimal set of dirty bits to place in the
	/// change tracker for use in the first sync of this prim.
	/// Typically this would be all dirty bits.

	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

	/// Causes the shader to be reloaded.
	virtual void Reload() override;

	/// Get pointer to RPR material
	/// In case material сreation failure return nullptr
	const RprApiMaterial * GetRprMaterialObject() const;

private:
	HdRprApiWeakPtr m_rprApiWeakPrt;

	RprApiMaterial * m_rprMaterial = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_H
