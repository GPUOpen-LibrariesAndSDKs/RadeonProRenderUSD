#ifndef HDRPR_LIGHT_BASE_H
#define HDRPR_LIGHT_BASE_H

#include "pxr/imaging/hd/sprim.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/usd/sdf/path.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprLightBase : public HdLight {
public:
    HdRprLightBase(SdfPath const& id, HdRprApiSharedPtr rprApi)
        : HdLight(id)
        , m_rprApiWeakPtr(rprApi) {
    }

    ~HdRprLightBase() override = default;

    void Sync(HdSceneDelegate* sceneDelegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    virtual bool IsDirtyGeomParam(std::map<TfToken, float>& params) = 0;

    // Fetch required params for geometry
    virtual const TfTokenVector& FetchLightGeometryParamNames() const = 0;

    virtual RprApiObjectPtr CreateLightMesh() = 0;

    // Normalize Light Color with surface area
    virtual GfVec3f NormalizeLightColor(const GfMatrix4d& transform, const GfVec3f& emmisionColor) = 0;

protected:
    HdRprApiWeakPtr m_rprApiWeakPtr;

private:
    bool IsDirtyMaterial(const GfVec3f& emmisionColor);
    RprApiObjectPtr CreateLightMaterial(const GfVec3f& illumColor);

private:
    RprApiObjectPtr m_lightMesh;
    RprApiObjectPtr m_lightMaterial;
    GfVec3f m_emmisionColor = GfVec3f(0.0f);
    GfMatrix4d m_transform;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_LIGHT_BASE_H
