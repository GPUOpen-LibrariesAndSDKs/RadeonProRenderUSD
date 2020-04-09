/************************************************************************
Copyright 2020 Advanced Micro Devices, Inc
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
************************************************************************/

#include "basisCurves.h"
#include "materialAdapter.h"
#include "material.h"
#include "renderParam.h"
#include "primvarUtil.h"
#include "rprApi.h"

#include "pxr/usd/usdUtils/pipeline.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprBasisCurves::HdRprBasisCurves(SdfPath const& id,
                                   SdfPath const& instancerId)
    : HdBasisCurves(id, instancerId)
    , m_visibilityMask(kVisibleAll) {

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
        | HdChangeTracker::DirtyPrimvar
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
    std::map<HdInterpolation, HdPrimvarDescriptorVector> primvarDescsPerInterpolation;

    bool newCurve = false;

    if (*dirtyBits & HdChangeTracker::DirtyPoints) {
        HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, id, &primvarDescsPerInterpolation);
        if (HdRprIsPrimvarExists(HdTokens->points, primvarDescsPerInterpolation)) {
            m_points = sceneDelegate->Get(id, HdTokens->points).Get<VtVec3fArray>();
        } else {
            m_points = VtVec3fArray();
        }
        newCurve = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyTopology) {
        m_topology = sceneDelegate->GetBasisCurvesTopology(id);
        m_indices = VtIntArray();
        if (m_topology.HasIndices()) {
            if (m_topology.GetCurveWrap() == HdTokens->nonperiodic || // GL_LINE_STRIP
                m_topology.GetCurveWrap() == HdTokens->periodic) { // GL_LINE_LOOP
                bool isPeriodic = m_topology.GetCurveWrap() == HdTokens->periodic;

                auto indices = m_topology.GetCurveIndices();
                m_indices.reserve(indices.size() * 2 + isPeriodic ? 2 : 0);
                for (size_t i = 0; i < indices.size() - 1; ++i) {
                    m_indices.push_back(indices[i]);
                    m_indices.push_back(indices[i + 1]);
                }
                if (isPeriodic) {
                    m_indices.push_back(indices.back());
                    m_indices.push_back(indices.front());
                }
            } else if (m_topology.GetCurveWrap() == HdTokens->segmented) {
                m_indices = m_topology.GetCurveIndices();
            } else {
                TF_RUNTIME_ERROR("[%s] Curve could not be created: unsupported curve wrap type - %s", id.GetText(), m_topology.GetCurveWrap().GetText());
            }
        } else {
            m_indices.reserve(m_points.size());
            for (int i = 0; i < m_points.size(); ++i) {
                m_indices.push_back(i);
            }
        }
        newCurve = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyWidths) {
        HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, id, &primvarDescsPerInterpolation);
        if (HdRprIsPrimvarExists(HdTokens->widths, primvarDescsPerInterpolation, &m_widthsInterpolation)) {
            m_widths = sceneDelegate->Get(id, HdTokens->widths).Get<VtFloatArray>();
        } else {
            m_widths = VtFloatArray(1, 1.0f);
            m_widthsInterpolation = HdInterpolationConstant;
            TF_WARN("[%s] Curve do not have widths. Fallback value is 1.0f with a constant interpolation", id.GetText());
        }
        newCurve = true;
    }

    bool isVisibilityMaskDirty = false;
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, id, &primvarDescsPerInterpolation);
        auto stToken = UsdUtilsGetPrimaryUVSetName();
        if (HdRprIsPrimvarExists(stToken, primvarDescsPerInterpolation, &m_uvsInterpolation)) {
            m_uvs = sceneDelegate->Get(id, stToken).Get<VtVec2fArray>();
        } else {
            m_uvs = VtVec2fArray();
        }
        newCurve = true;

        HdRprGeometrySettings geomSettings = {};
        geomSettings.visibilityMask = kVisibleAll;
        HdRprParseGeometrySettings(sceneDelegate, id, primvarDescsPerInterpolation.at(HdInterpolationConstant), &geomSettings);

        if (m_visibilityMask != geomSettings.visibilityMask) {
            m_visibilityMask = geomSettings.visibilityMask;
            isVisibilityMaskDirty = true;
        }
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
        m_rprCurve = nullptr;

        if (m_points.empty()) {
            TF_RUNTIME_ERROR("[%s] Curve could not be created: missing points", id.GetText());
        } else if (m_indices.empty()) {
            TF_RUNTIME_ERROR("[%s] Curve could not be created: missing indices", id.GetText());
        } else if (m_widths.empty()) {
            TF_RUNTIME_ERROR("[%s] Curve could not be created: missing width", id.GetText());
        } else if (m_topology.GetCurveType() != HdTokens->linear) {
            TF_RUNTIME_ERROR("[%s] Curve could not be created: unsupported basis curve type - %s", id.GetText(), m_topology.GetCurveType().GetText());
        } else if (!HdRprIsValidPrimvarSize(m_widths.size(), m_widthsInterpolation, m_topology.GetCurveVertexCounts().size(), m_points.size())) {
            TF_RUNTIME_ERROR("[%s] Curve could not be created: mismatch in number of widths and requested interpolation type", id.GetText());
        } else if (!m_uvs.empty() && !HdRprIsValidPrimvarSize(m_uvs.size(), m_uvsInterpolation, m_topology.GetCurveVertexCounts().size(), m_points.size())) {
            TF_RUNTIME_ERROR("[%s] Curve could not be created: mismatch in number of uvs and requested interpolation type", id.GetText());
        } else {
            HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, id, &primvarDescsPerInterpolation);
            if (HdRprIsPrimvarExists(HdTokens->normals, primvarDescsPerInterpolation)) {
                TF_WARN("[%s] Ribbon curves are not supported. Curve of tube type will be created", id.GetText());
            }

            if (m_uvsInterpolation != HdInterpolationConstant || m_uvsInterpolation != HdInterpolationUniform) {
                TF_WARN("[%s] Unsupported uv interpolation type", id.GetText());
            }

            m_rprCurve = CreateRprCurve(rprApi);
        }
    }

    if (m_rprCurve) {
        if (newCurve || (*dirtyBits & HdChangeTracker::DirtyMaterialId)) {
            if (m_cachedMaterial && m_cachedMaterial->GetRprMaterialObject()) {
                rprApi->SetCurveMaterial(m_rprCurve, m_cachedMaterial->GetRprMaterialObject());
            } else {
                GfVec3f color(0.18f);

                if (HdRprIsPrimvarExists(HdTokens->displayColor, primvarDescsPerInterpolation)) {
                    VtValue val = sceneDelegate->Get(id, HdTokens->displayColor);
                    if (!val.IsEmpty() && val.IsHolding<VtVec3fArray>()) {
                        auto colors = val.UncheckedGet<VtVec3fArray>();
                        if (!colors.empty()) {
                            color = colors[0];
                        }
                    }
                }

                MaterialAdapter matAdapter(EMaterialType::COLOR, MaterialParams{{HdRprMaterialTokens->color, VtValue(color)}});
                m_fallbackMaterial = rprApi->CreateMaterial(matAdapter);

                rprApi->SetCurveMaterial(m_rprCurve, m_fallbackMaterial);
            }
        }

        if (newCurve || ((*dirtyBits & HdChangeTracker::DirtyVisibility) || isVisibilityMaskDirty)) {
            auto visibilityMask = m_visibilityMask;
            if (!_sharedData.visible) {
                // Override m_visibilityMask
                visibilityMask = 0;
            }
            rprApi->SetCurveVisibility(m_rprCurve, visibilityMask);
        }

        if (newCurve || (*dirtyBits & HdChangeTracker::DirtyTransform)) {
            rprApi->SetTransform(m_rprCurve, m_transform);
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

rpr::Curve* HdRprBasisCurves::CreateRprCurve(HdRprApi* rprApi) {
    bool isCurveTapered = m_widthsInterpolation != HdInterpolationConstant && m_widthsInterpolation != HdInterpolationUniform;
    // Each segment of USD linear curves defined by two vertices
    // For tapered curve we need to convert it to RPR representation:
    //   4 vertices and 2 radiuses per segment
    // For cylindrical curve we can leave indices data in the same format as in USD,
    //   but we have to ensure that number of indices in each curve multiple of kNumPointsPerSegment
    const int kNumPointsPerSegment = 4;

    VtIntArray rprIndices;
    VtIntArray rprSegmentPerCurve;
    VtFloatArray rprRadiuses;
    VtVec2fArray rprUvs;

    auto& curveCounts = m_topology.GetCurveVertexCounts();
    rprSegmentPerCurve.reserve(curveCounts.size());

    size_t indicesOffset = 0;
    for (size_t iCurve = 0; iCurve < curveCounts.size(); ++iCurve) {
        auto numVertices = curveCounts[iCurve];

        if (numVertices > 1) {
            auto curveIndices = &m_indices[indicesOffset];
            for (int i = 0; i < numVertices - 1; ++i) {
                auto i0 = curveIndices[i + 0];
                auto i1 = curveIndices[i + 1];

                if (isCurveTapered) {
                    // Each 2 vertices of USD curve corresponds to 1 tapered RPR curve segment
                    rprIndices.push_back(i0);
                    rprIndices.push_back(i0);
                    rprIndices.push_back(i1);
                    rprIndices.push_back(i1);

                    // Each segment of tapered curve have 2 radiuses
                    rprRadiuses.push_back(m_widths[i0] * 0.5f);
                    rprRadiuses.push_back(m_widths[i1] * 0.5f);
                } else {
                    rprIndices.push_back(i0);
                    rprIndices.push_back(i1);
                }
            }

            if (!isCurveTapered) {
                // RPR requires curves to consist only of segments of kNumPointsPerSegment length
                auto numPointsInCurve = (numVertices - 1) * 2;
                auto extraPoints = numPointsInCurve % kNumPointsPerSegment;
                if (extraPoints) {
                    extraPoints = kNumPointsPerSegment - extraPoints;

                    auto lastPointIndex = curveIndices[numVertices - 1];
                    for (int i = 0; i < extraPoints; ++i) {
                        rprIndices.push_back(lastPointIndex);
                    }
                }

                // Each cylindrical curve must have 1 radius
                if (m_widthsInterpolation == HdInterpolationUniform) {
                    rprRadiuses.push_back(m_widths[iCurve] * 0.5f);
                } else if (m_widthsInterpolation == HdInterpolationConstant) {
                    rprRadiuses.push_back(m_widths[0] * 0.5f);
                }

                rprSegmentPerCurve.push_back((numPointsInCurve + extraPoints) / kNumPointsPerSegment);
            } else {
                rprSegmentPerCurve.push_back(numVertices - 1);
            }
        }

        indicesOffset += numVertices;
    }

    if (!m_uvs.empty()) {
        if (m_uvsInterpolation == HdInterpolationUniform) {
            rprUvs = m_uvs;
        } else if (m_uvsInterpolation == HdInterpolationConstant) {
            rprUvs = VtVec2fArray(rprSegmentPerCurve.size(), m_uvs[0]);
        }
    }

    return rprApi->CreateCurve(m_points, rprIndices, rprRadiuses, rprUvs, rprSegmentPerCurve);
}

void HdRprBasisCurves::Finalize(HdRenderParam* renderParam) {
    auto rprApi = static_cast<HdRprRenderParam*>(renderParam)->AcquireRprApiForEdit();

    rprApi->Release(m_rprCurve);
    m_rprCurve = nullptr;

    rprApi->Release(m_fallbackMaterial);
    m_fallbackMaterial = nullptr;
 
    HdBasisCurves::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
