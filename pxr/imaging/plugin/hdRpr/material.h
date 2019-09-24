#ifndef HDRPR_MATERIAL_H
#define HDRPR_MATERIAL_H

#include "rprApi.h"

#include "pxr/imaging/hd/material.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprMaterial final : public HdMaterial {
public:
    HdRprMaterial(SdfPath const& id, HdRprApiSharedPtr rprApi);

    ~HdRprMaterial() override = default;

    /// Synchronizes state from the delegate to this object.
    void Sync(HdSceneDelegate* sceneDelegate,
        HdRenderParam* renderParam,
        HdDirtyBits* dirtyBits) override;

    /// Returns the minimal set of dirty bits to place in the
    /// change tracker for use in the first sync of this prim.
    /// Typically this would be all dirty bits.
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Causes the shader to be reloaded.
    void Reload() override;

    /// Get pointer to RPR material
    /// In case material сreation failure return nullptr
    RprApiObject const* GetRprMaterialObject() const;

private:
    HdRprApiWeakPtr m_rprApiWeakPtr;
    RprApiObjectPtr m_rprMaterial;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_H
