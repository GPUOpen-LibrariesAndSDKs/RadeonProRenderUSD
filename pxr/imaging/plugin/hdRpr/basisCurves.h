#ifndef HDRPR_BASIS_CURVES_H
#define HDRPR_BASIS_CURVES_H

#include "pxr/imaging/hd/basisCurves.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprBasisCurves : public HdBasisCurves {

public:
	HdRprBasisCurves(SdfPath const& id, HdRprApiSharedPtr rprApi,
		SdfPath const& instancerId = SdfPath());

    virtual ~HdRprBasisCurves();

	virtual void Sync(HdSceneDelegate      *delegate,
		HdRenderParam        *renderParam,
		HdDirtyBits          *dirtyBits,
		TfToken const &reprSelector);

protected:

	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

	virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

	virtual void _InitRepr(TfToken const &reprName,
		HdDirtyBits *dirtyBits) override;

private:
	HdRprApiWeakPtr m_rprApiWeakPrt;
	RprApiObject m_rprCurve = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_BASIS_CURVES_H
