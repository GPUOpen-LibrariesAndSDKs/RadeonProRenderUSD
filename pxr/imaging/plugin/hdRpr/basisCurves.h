#ifndef HDRPR_BASIS_CURVES_H
#define HDRPR_BASIS_CURVES_H

#include "pxr/imaging/hd/basisCurves.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprMaterial;
class RprApiObject;
using RprApiObjectPtr = std::unique_ptr<RprApiObject>;

class HdRprBasisCurves : public HdBasisCurves {

public:
    HdRprBasisCurves(SdfPath const& id,
                     SdfPath const& instancerId);

    ~HdRprBasisCurves() override = default;

    void Sync(HdSceneDelegate* delegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits,
              TfToken const& reprSelector) override;

protected:

    HdDirtyBits GetInitialDirtyBitsMask() const override;

    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

    void _InitRepr(TfToken const& reprName,
                   HdDirtyBits* dirtyBits) override;

private:
    RprApiObjectPtr m_rprCurve;
    RprApiObjectPtr m_fallbackMaterial;
    HdRprMaterial const* m_cachedMaterial;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_BASIS_CURVES_H
