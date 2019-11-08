#ifndef HDRPR_MATERIAL_H
#define HDRPR_MATERIAL_H

#include "pxr/imaging/hd/material.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprMaterial final : public HdMaterial {
public:
    HdRprMaterial(SdfPath const& id, HdRprApiSharedPtr rprApi);

    ~HdRprMaterial() override = default;

    void Sync(HdSceneDelegate* sceneDelegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Reload() override;

    /// Get pointer to RPR material
    /// In case material сreation failure return nullptr
    const RprApiObject* GetRprMaterialObject() const;

private:
    HdRprApiWeakPtr m_rprApiWeakPtr;
    RprApiObjectPtr m_rprMaterial;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_H
