#include "basisCurves.h"
#include "materialAdapter.h"
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

HdDirtyBits HdRprBasisCurves::GetInitialDirtyBitsMask() const {
    return HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyPoints
        | HdChangeTracker::DirtyWidths
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyMaterialId;
}

void HdRprBasisCurves::Sync(HdSceneDelegate* sceneDelegate,
                            HdRenderParam* renderParam,
                            HdDirtyBits* dirtyBits,
                            TfToken const& reprSelector) {
    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    SdfPath const& id = GetId();

    bool newCurve = false;

    if (*dirtyBits & HdChangeTracker::DirtyPoints) {
        m_points = sceneDelegate->Get(id, HdTokens->points).Get<VtVec3fArray>();
        newCurve = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyTopology) {
        m_topology = sceneDelegate->GetBasisCurvesTopology(id);
        newCurve = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyWidths) {
        m_widths = sceneDelegate->Get(id, HdTokens->widths).Get<VtFloatArray>();
        newCurve = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        m_transform = GfMatrix4f(sceneDelegate->GetTransform(id));
        newCurve = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        m_cachedMaterial = static_cast<const HdRprMaterial*>(sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, sceneDelegate->GetMaterialId(id)));
    }

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        _sharedData.visible = sceneDelegate->GetVisible(id);
    }

    if (newCurve) {
        VtIntArray indices;
        if (m_topology.HasIndices()) {
            indices = m_topology.GetCurveIndices();
        } else {
            indices.reserve(m_points.size());
            for (int i = 0; i < m_points.size(); ++i) {
                indices.push_back(i);
            }
        }

        float curveWidth = 1.0f;
        if (!m_widths.empty()) {
            float curveWidth = m_widths[0];
            float currentWidth = curveWidth;
            const size_t curveWidthCount = m_points.size();
            for (int i = 1; i < curveWidthCount; ++i) {
                if (i < m_widths.size()) {
                    currentWidth = m_widths[i];
                }

                curveWidth += currentWidth;
            }
            curveWidth /= curveWidthCount;
        }

        m_rprCurve = rprApi->CreateCurve(m_points, indices, curveWidth);
        if (!m_rprCurve) {
            TF_RUNTIME_ERROR("Failed to create curve");
        }
    }

    if (m_rprCurve) {
        if (newCurve || (*dirtyBits & HdChangeTracker::DirtyMaterialId)) {
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
                            MaterialAdapter matAdapter(EMaterialType::COLOR, MaterialParams{{HdRprMaterialTokens->color, VtValue(color[0])}});
                            m_fallbackMaterial = rprApi->CreateMaterial(matAdapter);
                        }
                    }
                }

                rprApi->SetCurveMaterial(m_rprCurve.get(), m_fallbackMaterial.get());
            }
        }

        if (newCurve || (*dirtyBits & HdChangeTracker::DirtyVisibility)) {
            rprApi->SetCurveVisibility(m_rprCurve.get(), _sharedData.visible);
        }

        if (newCurve || (*dirtyBits & HdChangeTracker::DirtyTransform)) {
            rprApi->SetCurveTransform(m_rprCurve.get(), m_transform);
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void HdRprBasisCurves::Finalize(HdRenderParam* renderParam) {
    // Stop render thread to safely release resources
    static_cast<HdRprRenderParam*>(renderParam)->GetRenderThread()->StopRender();

    HdBasisCurves::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
