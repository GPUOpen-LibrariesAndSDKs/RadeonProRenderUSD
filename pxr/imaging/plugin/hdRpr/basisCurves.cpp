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
#include "material.h"
#include "renderParam.h"
#include "primvarUtil.h"
#include "rprApi.h"

#include "pxr/imaging/rprUsd/material.h"
#include "pxr/imaging/rprUsd/debugCodes.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprBasisCurves::HdRprBasisCurves(SdfPath const& id,
                                   SdfPath const& instancerId)
    : HdRprBaseRprim(id, instancerId)
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
            m_indices = m_topology.GetCurveIndices();
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

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        UpdateMaterialId(sceneDelegate, rprRenderParam);
    }

    auto material = static_cast<const HdRprMaterial*>(
        sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, m_materialId)
    );

    bool isVisibilityMaskDirty = false;
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, id, &primvarDescsPerInterpolation);

        static TfToken st("st", TfToken::Immortal);
        TfToken const* uvPrimvarName = &st;
        if (material) {
            if (auto rprMaterial = material->GetRprMaterialObject()) {
                uvPrimvarName = &rprMaterial->GetUvPrimvarName();
            }
        }

        if (HdRprIsPrimvarExists(*uvPrimvarName, primvarDescsPerInterpolation, &m_uvsInterpolation)) {
            m_uvs = sceneDelegate->Get(id, *uvPrimvarName).Get<VtVec2fArray>();
        } else {
            m_uvs = VtVec2fArray();
        }
        newCurve = true;

        HdRprGeometrySettings geomSettings = {};
        geomSettings.visibilityMask = kVisibleAll;
        HdRprParseGeometrySettings(sceneDelegate, id, primvarDescsPerInterpolation, &geomSettings);

        if (m_visibilityMask != geomSettings.visibilityMask) {
            m_visibilityMask = geomSettings.visibilityMask;
            isVisibilityMaskDirty = true;
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        m_transform = GfMatrix4f(sceneDelegate->GetTransform(id));
        newCurve = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        _sharedData.visible = sceneDelegate->GetVisible(id);
    }

    if (newCurve) {
        if (m_rprCurve) {
            rprApi->Release(m_rprCurve);
            m_rprCurve = nullptr;
        }

        if (m_points.empty()) {
            TF_RUNTIME_ERROR("[%s] Curve could not be created: missing points", id.GetText());
        } else if (m_widths.empty()) {
            TF_RUNTIME_ERROR("[%s] Curve could not be created: missing width", id.GetText());
        } else if (m_topology.GetCurveWrap() != HdTokens->segmented &&
                   m_topology.GetCurveWrap() != HdTokens->nonperiodic &&
                   m_topology.GetCurveWrap() != HdTokens->periodic) {
            TF_RUNTIME_ERROR("[%s] Curve could not be created: unsupported curve wrap type - %s", id.GetText(), m_topology.GetCurveWrap().GetText());
        } else if (m_topology.GetCurveType() != HdTokens->linear &&
                   m_topology.GetCurveType() != HdTokens->cubic) {
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

            bool isLinear = m_topology.GetCurveType() == HdTokens->linear;

            if (m_topology.GetCurveType() == HdTokens->cubic &&
                (m_topology.GetCurveBasis() == HdTokens->catmullRom||
                 m_topology.GetCurveBasis() == HdTokens->bSpline)) {
                // XXX: There is no way to natively support catmullrom and bspline bases in RPR, try to render them as linear ones
                isLinear = true;
            }

            if (isLinear) {
                m_rprCurve = CreateLinearRprCurve(rprApi);
            } else if (m_topology.GetCurveType() == HdTokens->cubic &&
                       m_topology.GetCurveBasis() == HdTokens->bezier) {
                m_rprCurve = CreateBezierRprCurve(rprApi);
            }

            if (m_rprCurve && RprUsdIsLeakCheckEnabled()) {
                rprApi->SetName(m_rprCurve, id.GetText());
            }
        }
    }

    if (m_rprCurve) {
        if (newCurve || (*dirtyBits & HdChangeTracker::DirtyMaterialId)) {
            if (material && material->GetRprMaterialObject()) {
                rprApi->SetCurveMaterial(m_rprCurve, material->GetRprMaterialObject());
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

                m_fallbackMaterial = rprApi->CreateDiffuseMaterial(color);
                rprApi->SetCurveMaterial(m_rprCurve, m_fallbackMaterial);

                if (RprUsdIsLeakCheckEnabled()) {
                    rprApi->SetName(m_fallbackMaterial, id.GetText());
                }
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

static const int kRprNumPointsPerSegment = 4;

rpr::Curve* HdRprBasisCurves::CreateLinearRprCurve(HdRprApi* rprApi) {
    // Each segment of USD linear curves defined by two vertices
    // For tapered curve we need to convert it to RPR representation:
    //   4 vertices and 2 radiuses per segment
    // For cylindrical curve we can leave indices data in the same format as in USD,
    //   but we have to ensure that number of indices in each curve multiple of kRprNumPointsPerSegment

    const bool periodic = m_topology.GetCurveWrap() == HdTokens->periodic;
    const bool strip = periodic || m_topology.GetCurveWrap() == HdTokens->nonperiodic;
    const bool isCurveTapered = m_widthsInterpolation != HdInterpolationConstant && m_widthsInterpolation != HdInterpolationUniform;

    const int kNumPointsPerSegment = 2;
    const int kVstep = strip ? 1 : 2;

    VtIntArray rprIndices;
    VtIntArray rprSegmentPerCurve;
    VtFloatArray rprRadiuses;
    VtVec2fArray rprUvs;

    std::function<float(int, bool)> sampleTaperRadius;
    if (isCurveTapered) {
        if (m_widthsInterpolation == HdInterpolationVarying) {
            sampleTaperRadius = [this](int iSegment, bool front) {
                return 0.5f * m_widths[iSegment + (front ? 0 : 1)];
            };
        } else if (m_widthsInterpolation == HdInterpolationVertex) {
            sampleTaperRadius = [=](int iSegment, bool front) {
                return 0.5f * m_widths[iSegment * kVstep + (front ? 0 : (kNumPointsPerSegment - 1))];
            };
        }
    }

    std::function<int(int)> indexSampler;
    if (m_indices.empty()) {
        indexSampler = [](int idx) { return idx; };
    } else {
        indexSampler = [this](int idx) { return m_indices.cdata()[idx]; };
    }

    auto& curveCounts = m_topology.GetCurveVertexCounts();
    rprSegmentPerCurve.reserve(curveCounts.size());

    // Validate Hydra curve data and calculate amount of required memory.
    //
    size_t numRadiuses = 0;
    size_t numIndices = 0;
    int curveSegmentOffset = 0;
    int curveIndicesOffset = 0;
    for (size_t iCurve = 0; iCurve < curveCounts.size(); ++iCurve) {
        auto numVertices = curveCounts[iCurve];
        if (numVertices < 2) {
            continue;
        }

        if (!strip && numVertices % 2 != 0) {
            TF_RUNTIME_ERROR("[%s] corrupted curve data: segmented linear curve should contain even number of vertices", GetId().GetText());
            return nullptr;
        }

        int numSegments = (numVertices - (kNumPointsPerSegment - kVstep)) / kVstep;
        if (periodic) numSegments++;

        if ((m_widthsInterpolation == HdInterpolationVarying && m_widths.size() < (curveSegmentOffset + numSegments)) ||
            (m_widthsInterpolation == HdInterpolationVertex && m_widths.size() < (curveIndicesOffset + numVertices))) {
            TF_RUNTIME_ERROR("[%s] corrupted curve data: insufficient amount of widths", GetId().GetText());
            return nullptr;
        }

        if (isCurveTapered) {
            numRadiuses += numSegments * 2;
            numIndices += numSegments * 4;
        } else {
            if (m_widthsInterpolation == HdInterpolationUniform ||
                m_widthsInterpolation == HdInterpolationConstant) {
                ++numRadiuses;
            }
            numIndices += numSegments * 2;

            // RPR requires curves to consist only of segments of kRprNumPointsPerSegment length
            auto numPointsInCurve = (numVertices - 1) * 2;
            auto numTrailingPoints = numPointsInCurve % kRprNumPointsPerSegment;
            if (numTrailingPoints > 0) {
                numIndices += kRprNumPointsPerSegment - numTrailingPoints;
            }
        }

        curveSegmentOffset += numSegments;
        curveIndicesOffset += numVertices;
    }
    rprRadiuses.reserve(numRadiuses);
    rprIndices.reserve(numIndices);

    // Convert Hydra curve data to RPR data.
    //
    curveIndicesOffset = 0;
    for (size_t iCurve = 0; iCurve < curveCounts.size(); ++iCurve) {
        auto numVertices = curveCounts[iCurve];
        if (numVertices < 2) {
            continue;
        }

        int numSegments = (numVertices - (kNumPointsPerSegment - kVstep)) / kVstep;
        if (periodic) numSegments++;

        if (isCurveTapered) {
            rprSegmentPerCurve.push_back(numVertices - 1);
            for (int iSegment = 0; iSegment < numSegments; ++iSegment) {
                const int segmentIndicesOffset = iSegment * kVstep;

                const int i0 = indexSampler(curveIndicesOffset + segmentIndicesOffset);
                const int i1 = indexSampler(curveIndicesOffset + (segmentIndicesOffset + 1) % numVertices);

                // Each 2 vertices of USD curve corresponds to 1 tapered RPR curve segment
                rprIndices.push_back(i0);
                rprIndices.push_back(i0);
                rprIndices.push_back(i1);
                rprIndices.push_back(i1);

                // Each segment of tapered curve have 2 radiuses
                rprRadiuses.push_back(sampleTaperRadius(iSegment, true));
                rprRadiuses.push_back(sampleTaperRadius(iSegment, false));
            }
        } else {
            for (int iSegment = 0; iSegment < numSegments; ++iSegment) {
                const int segmentIndicesOffset = iSegment * kVstep;

                rprIndices.push_back(indexSampler(curveIndicesOffset + segmentIndicesOffset));
                rprIndices.push_back(indexSampler(curveIndicesOffset + (segmentIndicesOffset + 1) % numVertices));
            }

            // RPR requires curves to consist only of segments of kRprNumPointsPerSegment length
            auto numPointsInCurve = (numVertices - 1) * 2;
            auto extraPoints = numPointsInCurve % kRprNumPointsPerSegment;
            if (extraPoints) {
                extraPoints = kRprNumPointsPerSegment - extraPoints;

                auto lastPointIndex = indexSampler(curveIndicesOffset + numVertices - 1);
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

            rprSegmentPerCurve.push_back((numPointsInCurve + extraPoints) / kRprNumPointsPerSegment);
        }

        curveIndicesOffset += numVertices;
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

rpr::Curve* HdRprBasisCurves::CreateBezierRprCurve(HdRprApi* rprApi) {
    if (m_topology.GetCurveWrap() == HdTokens->segmented) {
        TF_RUNTIME_ERROR("[%s] corrupted curve data: bezier curve can not be of segmented wrap type", GetId().GetText());
        return nullptr;
    }

    VtIntArray rprIndices;
    VtIntArray rprSegmentPerCurve;
    VtFloatArray rprRadiuses;
    VtVec2fArray rprUvs;

    auto& curveCounts = m_topology.GetCurveVertexCounts();
    rprSegmentPerCurve.reserve(curveCounts.size());

    const int kNumPointsPerSegment = 4;
    const int kVstep = 3;

    const bool periodic = m_topology.GetCurveWrap() == HdTokens->periodic;
    const bool isCurveTapered = m_widthsInterpolation != HdInterpolationConstant && m_widthsInterpolation != HdInterpolationUniform;

    std::function<float(int, bool)> sampleTaperRadius;
    if (isCurveTapered) {
        if (m_widthsInterpolation == HdInterpolationVarying) {
            sampleTaperRadius = [this](int iSegment, bool front) {
                return 0.5f * m_widths[iSegment + (front ? 0 : 1)];
            };
        } else if (m_widthsInterpolation == HdInterpolationVertex) {
            sampleTaperRadius = [=](int iSegment, bool front) {
                return 0.5f * m_widths[iSegment * kVstep + (front ? 0 : (kNumPointsPerSegment - 1))];
            };
        }
    }

    std::function<int(int)> indexSampler;
    if (m_indices.empty()) {
        indexSampler = [](int idx) { return idx; };
    } else {
        indexSampler = [this](int idx) { return m_indices.cdata()[idx]; };
    }

    // Validate Hydra curve data and calculate amount of required memory.
    //
    size_t numRadiuses = 0;
    size_t numIndices = 0;
    int curveSegmentOffset = 0;
    int curveIndicesOffset = 0;
    for (size_t iCurve = 0; iCurve < curveCounts.size(); ++iCurve) {
        auto numVertices = curveCounts[iCurve];
        if (numVertices < kNumPointsPerSegment) {
            continue;
        }

        // Validity check from the USD docs
        if ((periodic && numVertices % kVstep != 0) ||
            (!periodic && (numVertices - 4) % kVstep != 0)) {
            TF_RUNTIME_ERROR("[%s] corrupted curve data: invalid topology", GetId().GetText());
            return nullptr;
        }

        int numSegments = (numVertices - (kNumPointsPerSegment - kVstep)) / kVstep;
        if (periodic) numSegments++;

        if ((m_widthsInterpolation == HdInterpolationVarying && m_widths.size() < (curveSegmentOffset + numSegments)) ||
            (m_widthsInterpolation == HdInterpolationVertex && m_widths.size() < (curveIndicesOffset + numVertices))) {
            TF_RUNTIME_ERROR("[%s] corrupted curve data: insufficient amount of widths", GetId().GetText());
            return nullptr;
        }

        numIndices += numSegments * kNumPointsPerSegment;
        if (isCurveTapered) {
            numRadiuses += numSegments * 2;
        } else {
            if (m_widthsInterpolation == HdInterpolationUniform ||
                m_widthsInterpolation == HdInterpolationConstant) {
                numRadiuses++;
            }
        }

        curveSegmentOffset += numSegments;
        curveIndicesOffset += numVertices;
    }
    rprRadiuses.reserve(numRadiuses);
    rprIndices.reserve(numIndices);

    // Convert Hydra curve data to RPR data.
    //
    curveIndicesOffset = 0;
    for (size_t iCurve = 0; iCurve < curveCounts.size(); ++iCurve) {
        auto numVertices = curveCounts[iCurve];
        if (numVertices < kNumPointsPerSegment) {
            continue;
        }

        int numSegments = (numVertices - (kNumPointsPerSegment - kVstep)) / kVstep;
        if (periodic) numSegments++;

        rprSegmentPerCurve.push_back(numSegments);

        for (int iSegment = 0; iSegment < numSegments; ++iSegment) {
            const int segmentIndicesOffset = iSegment * kVstep;

            const int i0 = indexSampler(curveIndicesOffset + segmentIndicesOffset + 0);
            const int i1 = indexSampler(curveIndicesOffset + segmentIndicesOffset + 1);
            const int i2 = indexSampler(curveIndicesOffset + segmentIndicesOffset + 2);
            const int i3 = indexSampler(curveIndicesOffset + (segmentIndicesOffset + 3) % numVertices);

            rprIndices.push_back(i0);
            rprIndices.push_back(i1);
            rprIndices.push_back(i2);
            rprIndices.push_back(i3);

            if (isCurveTapered) {
                // XXX: We consciously losing data here because RPR supports only two radius samples per segment
                rprRadiuses.push_back(sampleTaperRadius(iSegment, true));
                rprRadiuses.push_back(sampleTaperRadius(iSegment, false));
            }
        }

        if (!isCurveTapered) {
            // Each cylindrical curve must have 1 radius
            if (m_widthsInterpolation == HdInterpolationUniform) {
                rprRadiuses.push_back(m_widths[iCurve] * 0.5f);
            } else if (m_widthsInterpolation == HdInterpolationConstant) {
                rprRadiuses.push_back(m_widths[0] * 0.5f);
            }
        }

        curveIndicesOffset += numVertices;
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
 
    HdRprBaseRprim::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
