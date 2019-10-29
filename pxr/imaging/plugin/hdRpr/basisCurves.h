#ifndef HDRPR_BASIS_CURVES_H
#define HDRPR_BASIS_CURVES_H

#include "pxr/imaging/hd/basisCurves.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprMaterial;

class HdRprBasisCurves : public HdBasisCurves {

public:
	HdRprBasisCurves(SdfPath const& id, HdRprApiSharedPtr rprApi,
		SdfPath const& instancerId = SdfPath());

    ~HdRprBasisCurves() override = default;

	virtual void Sync(HdSceneDelegate      *delegate,
		HdRenderParam        *renderParam,
		HdDirtyBits          *dirtyBits,
		TfToken const &reprSelector) override;

protected:

	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

	virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

	virtual void _InitRepr(TfToken const &reprName,
		HdDirtyBits *dirtyBits) override;

private:
    HdRprApiWeakPtr m_rprApiWeakPtr;
    RprApiObjectPtr m_rprCurve;
    RprApiObjectPtr m_fallbackMaterial;

    HdRprMaterial const* m_cachedMaterial;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_BASIS_CURVES_H
