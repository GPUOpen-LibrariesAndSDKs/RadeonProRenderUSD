#include "basisCurves.h"
#include "materialFactory.h"
#include "material.h"
#include "renderParam.h"
#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprBasisCurves::HdRprBasisCurves(SdfPath const& id,
                                   SdfPath const& instancerId)
    : HdBasisCurves(id, instancerId) {

}

HdDirtyBits HdRprBasisCurves::_PropagateDirtyBits(HdDirtyBits bits) const {
    return bits;
}

void HdRprBasisCurves::_InitRepr(TfToken const& reprName,
                                 HdDirtyBits* dirtyBits) {
    TF_UNUSED(reprName);
    TF_UNUSED(dirtyBits);

    // No-op
}

void HdRprBasisCurves::Sync(HdSceneDelegate* sceneDelegate,
                            HdRenderParam* renderParam,
                            HdDirtyBits* dirtyBits,
                            TfToken const& reprSelector) {
    TF_UNUSED(renderParam);

    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    SdfPath const& id = GetId();

    bool newCurve = false;
    if (*dirtyBits & HdChangeTracker::DirtyTopology) {
        auto& points = sceneDelegate->Get(id, HdTokens->points).Get<VtVec3fArray>();
        auto curveTopology = sceneDelegate->GetBasisCurvesTopology(id);

        VtIntArray indexes;
        if (curveTopology.HasIndices()) {
            indexes = curveTopology.GetCurveIndices();
        } else {
            indexes.reserve(points.size());
            for (int i = 0; i < points.size(); ++i) {
                indexes.push_back(i);
            }
        }

        VtFloatArray curveWidths = sceneDelegate->Get(id, HdTokens->widths).Get<VtFloatArray>();
        float curveWidth = curveWidths[0];
        float currentWidth = curveWidth;
        const size_t curveWidthCount = points.size();
        for (int i = 1; i < curveWidthCount; ++i) {
            if (i < curveWidths.size()) {
                currentWidth = curveWidths[i];
            }

            curveWidth += currentWidth;
        }
        curveWidth /= curveWidthCount;

        GfMatrix4d transform = sceneDelegate->GetTransform(id);
        VtVec3fArray pointsTransformed;
        for (auto& point : points) {
            const GfVec4f& pointTransformed = GfVec4f(point[0], point[1], point[2], 1.) * transform;
            pointsTransformed.push_back(GfVec3f(pointTransformed[0], pointTransformed[1], pointTransformed[2]));
        }

        m_rprCurve = rprApi->CreateCurve(pointsTransformed, indexes, curveWidth);
        if (!m_rprCurve) {
            TF_RUNTIME_ERROR("Failed to create curve");
            *dirtyBits = HdChangeTracker::Clean;
            return;
        }

        newCurve = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        m_cachedMaterial = static_cast<const HdRprMaterial*>(sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, sceneDelegate->GetMaterialId(id)));
    }

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        _sharedData.visible = sceneDelegate->GetVisible(id);
    }

    if (newCurve || ((*dirtyBits & HdChangeTracker::DirtyMaterialId) && m_rprCurve)) {
        if (m_cachedMaterial && m_cachedMaterial->GetRprMaterialObject()) {
            rprApi->SetCurveMaterial(m_rprCurve.get(), m_cachedMaterial->GetRprMaterialObject());
        } else {
            if (!m_fallbackMaterial) {
                auto primvars = sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationConstant);
                auto colorPrimvarDescIter = std::find_if(primvars.begin(), primvars.end(), [](HdPrimvarDescriptor const& desc) { return desc.name == HdPrimvarRoleTokens->color; });
                if (colorPrimvarDescIter != primvars.end()) {
                    VtValue val = sceneDelegate->Get(id, HdPrimvarRoleTokens->color);
                    if (!val.IsEmpty()) {
                        VtArray<GfVec4f> color = val.Get<VtArray<GfVec4f>>();
                        MaterialAdapter matAdapter(EMaterialType::COLOR, MaterialParams{{HdPrimvarRoleTokens->color, VtValue(color[0])}});
                        m_fallbackMaterial = rprApi->CreateMaterial(matAdapter);
                    }
                }
            }

            rprApi->SetCurveMaterial(m_rprCurve.get(), m_fallbackMaterial.get());
        }
    }

    if (newCurve || ((*dirtyBits & HdChangeTracker::DirtyVisibility) && m_rprCurve)) {
        rprApi->SetCurveVisibility(m_rprCurve.get(), _sharedData.visible);
    }

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdRprBasisCurves::GetInitialDirtyBitsMask() const {
    return HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyMaterialId;
}

PXR_NAMESPACE_CLOSE_SCOPE
