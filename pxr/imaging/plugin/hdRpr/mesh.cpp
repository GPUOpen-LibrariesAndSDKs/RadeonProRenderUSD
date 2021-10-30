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

#include "mesh.h"
#include "instancer.h"
#include "renderParam.h"
#include "material.h"
#include "primvarUtil.h"
#include "rprApi.h"

#include "pxr/imaging/rprUsd/material.h"
#include "pxr/imaging/rprUsd/debugCodes.h"

#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/imaging/pxOsd/subdivTags.h"

#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/sprim.h"
#include "pxr/imaging/hd/smoothNormals.h"
#include "pxr/imaging/hd/extComputationUtils.h"

#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/vec4f.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprMesh::HdRprMesh(SdfPath const& id HDRPR_INSTANCER_ID_ARG_DECL)
    : HdRprBaseRprim(id HDRPR_INSTANCER_ID_ARG) {

}

HdDirtyBits HdRprMesh::_PropagateDirtyBits(HdDirtyBits bits) const {
    return bits;
}

HdDirtyBits HdRprMesh::GetInitialDirtyBitsMask() const {
    // The initial dirty bits control what data is available on the first
    // run through _PopulateMesh(), so it should list every data item
    // that _PopluateMesh requests.
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::DirtyPoints
        | HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyNormals
        | HdChangeTracker::DirtyMaterialId
        | HdChangeTracker::DirtySubdivTags
        | HdChangeTracker::DirtyDisplayStyle
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyInstancer
        | HdChangeTracker::DirtyInstanceIndex
        | HdChangeTracker::DirtyDoubleSided
        ;

    return (HdDirtyBits)mask;
}

void HdRprMesh::_InitRepr(TfToken const& reprName,
                          HdDirtyBits* dirtyBits) {
    TF_UNUSED(reprName);
    TF_UNUSED(dirtyBits);

    // No-op
}

RprUsdMaterial const* HdRprMesh::GetFallbackMaterial(
    HdSceneDelegate* sceneDelegate,
    HdRprApi* rprApi,
    HdDirtyBits dirtyBits,
    std::map<HdInterpolation, HdPrimvarDescriptorVector> const& primvarDescsPerInterpolation) {
    if (m_fallbackMaterial && (dirtyBits & HdChangeTracker::DirtyPrimvar)) {
        rprApi->Release(m_fallbackMaterial);
        m_fallbackMaterial = nullptr;
    }

    if (!m_fallbackMaterial) {
        // XXX: Currently, displayColor is used as one color for whole mesh,
        // but it should be used as attribute per vertex/face.
        // RPR does not have such functionality, yet

        GfVec3f color(0.18f);

        if (HdRprIsPrimvarExists(HdTokens->displayColor, primvarDescsPerInterpolation)) {
            VtValue val = sceneDelegate->Get(GetId(), HdTokens->displayColor);
            if (val.IsHolding<VtVec3fArray>()) {
                auto colors = val.UncheckedGet<VtVec3fArray>();
                if (!colors.empty()) {
                    color = colors[0];
                }
            } else if (val.IsHolding<GfVec3f>()) {
                color = val.UncheckedGet<GfVec3f>();
            }
        }

        m_fallbackMaterial = rprApi->CreateDiffuseMaterial(color);
        rprApi->SetName(m_fallbackMaterial, GetId().GetText());
    }

    return m_fallbackMaterial;
}

void HdRprMesh::Sync(HdSceneDelegate* sceneDelegate,
                     HdRenderParam* renderParam,
                     HdDirtyBits* dirtyBits,
                     TfToken const& reprName) {
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    SdfPath const& id = GetId();

    ////////////////////////////////////////////////////////////////////////
    // 1. Pull scene data.

    bool newMesh = false;

    std::map<HdInterpolation, HdPrimvarDescriptorVector> primvarDescsPerInterpolation;

    bool isRefineLevelDirty = false;
    if (*dirtyBits & HdChangeTracker::DirtyDisplayStyle) {
        m_displayStyle = sceneDelegate->GetDisplayStyle(id);
        if (m_refineLevel != m_displayStyle.refineLevel) {
            isRefineLevelDirty = true;
            m_refineLevel = m_displayStyle.refineLevel;
        }
    }

    bool isIgnoreContourDirty = false;
    bool isVisibilityMaskDirty = false;
    bool isIdDirty = false;
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        HdRprGeometrySettings geomSettings = {};
        geomSettings.visibilityMask = kVisibleAll;
        HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, id, &primvarDescsPerInterpolation);
        HdRprParseGeometrySettings(sceneDelegate, id, primvarDescsPerInterpolation, &geomSettings);

        if (m_refineLevel != geomSettings.subdivisionLevel) {
            m_refineLevel = geomSettings.subdivisionLevel;
            isRefineLevelDirty = true;
        }

        if (m_visibilityMask != geomSettings.visibilityMask) {
            m_visibilityMask = geomSettings.visibilityMask;
            isVisibilityMaskDirty = true;
        }

        if (m_id != geomSettings.id) {
            m_id = geomSettings.id;
            isIdDirty = true;
        }

        if (m_ignoreContour != geomSettings.ignoreContour) {
            m_ignoreContour = geomSettings.ignoreContour;
            isIgnoreContourDirty = true;
        }

        if (m_cryptomatteName != geomSettings.cryptomatteName) {
            m_cryptomatteName = geomSettings.cryptomatteName;
        }

        if (m_numGeometrySamples != geomSettings.numGeometrySamples) {
            m_numGeometrySamples = geomSettings.numGeometrySamples;
            *dirtyBits |= HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyNormals;
        }
    }

    bool pointsIsComputed = false;
    auto extComputationDescs = sceneDelegate->GetExtComputationPrimvarDescriptors(id, HdInterpolationVertex);
    for (auto& desc : extComputationDescs) {
        if (desc.name != HdTokens->points) {
            continue;
        }

        if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, desc.name)) {
            m_pointSamples.clear();

#if PXR_VERSION >= 2105
            HdExtComputationUtils::SampledValueStore<2> valueStore;
            HdExtComputationUtils::SampleComputedPrimvarValues({desc}, sceneDelegate, m_numGeometrySamples, &valueStore);
            auto pointValueIt = valueStore.find(desc.name);
            if (pointValueIt != valueStore.end()) {
                auto& sampleValues = pointValueIt->second.values;
                VtArray<VtVec3fArray> newPointSamples;
                newPointSamples.reserve(sampleValues.size());
                for (auto& sampleValue : sampleValues) {
                    if (sampleValue.IsHolding<VtVec3fArray>()) {
                        newPointSamples.push_back(sampleValue.UncheckedGet<VtVec3fArray>());
                    } else {
                        newPointSamples.clear();
                        break;
                    }
                }

                if (!newPointSamples.empty()) {
                    m_pointSamples = std::move(newPointSamples);
                    m_normalsValid = false;
                    pointsIsComputed = true;

                    newMesh = true;
                }
            }
#else // PXR_VERSION < 2105
            if (m_numGeometrySamples != 1) {
                TF_WARN("UsdSkel deformation motion blur is supported only in USD 21.05+ (current version %d.%d)", PXR_MINOR_VERSION, PXR_PATCH_VERSION);
            }
            auto valueStore = HdExtComputationUtils::GetComputedPrimvarValues({desc}, sceneDelegate);
            auto pointValueIt = valueStore.find(desc.name);
            if (pointValueIt != valueStore.end()) {
                m_pointSamples = {pointValueIt->second.Get<VtVec3fArray>()};
                m_normalsValid = false;
                pointsIsComputed = true;

                newMesh = true;
            }
#endif // PXR_VERSION >= 2105
        }

        break;
    }

    if (!pointsIsComputed &&
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        if (!HdRprSamplePrimvar(id, HdTokens->points, sceneDelegate, m_numGeometrySamples, &m_pointSamples)) {
            m_pointSamples.clear();
        }

        m_normalsValid = false;
        newMesh = true;
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        for (auto& oldGeomSubset : m_geomSubsets) {
            if (!oldGeomSubset.materialId.IsEmpty()) {
                rprRenderParam->UnsubscribeFromMaterialUpdates(oldGeomSubset.materialId, id);
            }
        }

        m_topology = GetMeshTopology(sceneDelegate);
        m_faceVertexCounts = m_topology.GetFaceVertexCounts();
        m_faceVertexIndices = m_topology.GetFaceVertexIndices();

        m_adjacencyValid = false;
        m_normalsValid = false;

        m_enableSubdiv = m_topology.GetScheme() == PxOsdOpenSubdivTokens->catmullClark;
        m_geomSubsets = m_topology.GetGeomSubsets();

        // GeomSubset data is directly transfered from USD into Hydra topology.
        // This data should be validated and preprocessed before using it:
        //   1) merge subsets with the same material
        //
        std::map<SdfPath, size_t> materialToSubsetMapping;
        for (size_t i = 0; i < m_geomSubsets.size();) {
            auto& subset = m_geomSubsets[i];
            auto it = materialToSubsetMapping.find(subset.materialId);
            if (it == materialToSubsetMapping.end()) {
                materialToSubsetMapping.emplace(subset.materialId, i);
                ++i;
            } else {
                auto& baseSubset = m_geomSubsets[it->second];

                // Append indices to the base subset
                baseSubset.indices.reserve(baseSubset.indices.size() + subset.indices.size());
                for (auto index : subset.indices) {
                    baseSubset.indices.push_back(index);
                }

                // Erase the current subset
                if (i + 1 != m_geomSubsets.size()) {
                    std::swap(m_geomSubsets[i], m_geomSubsets.back());
                }
                m_geomSubsets.pop_back();
            }
        }
        //
        //   2) create a new geomSubset that consists of unused faces
        //
        if (!m_geomSubsets.empty()) {
            auto numFaces = m_faceVertexCounts.size();
            std::vector<bool> faceIsUnused(numFaces, true);
            size_t numUnusedFaces = faceIsUnused.size();
            for (auto const& subset : m_geomSubsets) {
                for (int index : subset.indices) {
                    if (TF_VERIFY(index < numFaces) && faceIsUnused[index]) {
                        faceIsUnused[index] = false;
                        numUnusedFaces--;
                    }
                }
            }
            if (numUnusedFaces) {
                m_geomSubsets.push_back(HdGeomSubset());
                HdGeomSubset& unusedSubset = m_geomSubsets.back();
                unusedSubset.type = HdGeomSubset::TypeFaceSet;
                unusedSubset.id = id;
                unusedSubset.materialId = m_materialId;
                unusedSubset.indices.resize(numUnusedFaces);
                size_t count = 0;
                for (size_t i = 0; i < faceIsUnused.size() && count < numUnusedFaces; ++i) {
                    if (faceIsUnused[i]) {
                        unusedSubset.indices[count] = i;
                        count++;
                    }
                }
            }
        }

        for (auto& newGeomSubset : m_geomSubsets) {
            if (!newGeomSubset.materialId.IsEmpty()) {
                rprRenderParam->SubscribeForMaterialUpdates(newGeomSubset.materialId, id);
            }
        }

        newMesh = true;
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals)) {
        HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, id, &primvarDescsPerInterpolation);
        HdInterpolation interpolation;
        m_authoredNormals = HdRprSamplePrimvar(id, HdTokens->normals, sceneDelegate, primvarDescsPerInterpolation, m_numGeometrySamples, &m_normalSamples, &interpolation);
        if (m_authoredNormals) {
            HdRprGetPrimvarIndices(interpolation, m_faceVertexIndices, &m_normalIndices);
        } else {
            m_normalSamples.clear();
            m_normalIndices.clear();
        }

        newMesh = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        UpdateMaterialId(sceneDelegate, rprRenderParam);
    }

    // We are loading mesh UVs only when it has material
    auto material = static_cast<const HdRprMaterial*>(
        sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, m_materialId)
    );

    // Check all materials, including those from geomSubsets
    if (!material || !material->GetRprMaterialObject()) {
        for (auto& subset : m_geomSubsets) {
            if (subset.type == HdGeomSubset::TypeFaceSet &&
                !subset.materialId.IsEmpty()) {
                material = static_cast<const HdRprMaterial*>(
                    sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, subset.materialId)
                );
                if (material && material->GetRprMaterialObject()) {
                    break;
                }
            }
        }
    }

    if (material && material->GetRprMaterialObject()) {
        auto rprMaterial = material->GetRprMaterialObject();

        auto uvPrimvarName = &rprMaterial->GetUvPrimvarName();
        if (uvPrimvarName->IsEmpty()) {
            static TfToken st("st", TfToken::Immortal);
            uvPrimvarName = &st;
        }

        if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, *uvPrimvarName)) {
            HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, id, &primvarDescsPerInterpolation);

            HdInterpolation interpolation;
            if (HdRprSamplePrimvar(id, *uvPrimvarName, sceneDelegate, primvarDescsPerInterpolation, m_numGeometrySamples, &m_uvSamples, &interpolation)) {
                HdRprGetPrimvarIndices(interpolation, m_faceVertexIndices, &m_uvIndices);
            } else {
                m_uvSamples.clear();
                m_uvIndices.clear();
            }

            newMesh = true;
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        UpdateVisibility(sceneDelegate);
    }

    ////////////////////////////////////////////////////////////////////////
    // 2. Resolve drawstyles

    m_smoothNormals = !m_displayStyle.flatShadingEnabled;
    // Don't compute smooth normals on a refined mesh. They are implicitly smooth.
    if (m_enableSubdiv && m_refineLevel != 0) {
        m_smoothNormals = false;
    }

    if (!m_authoredNormals && m_smoothNormals) {
        if (!m_adjacencyValid) {
            m_adjacency.BuildAdjacencyTable(&m_topology);
            m_adjacencyValid = true;
            m_normalsValid = false;
        }

        if (!m_normalsValid) {
            m_normalSamples.clear();
            for (auto& points : m_pointSamples) {
                m_normalSamples.push_back(Hd_SmoothNormals::ComputeSmoothNormals(&m_adjacency, points.size(), points.cdata()));
            }
            m_normalsValid = true;

            newMesh = true;
        }
    }

    bool updateTransform = newMesh;
    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        sceneDelegate->SampleTransform(id, &m_transformSamples);
        updateTransform = true;
    }

    ////////////////////////////////////////////////////////////////////////
    // 3. Create RPR meshes

    if (newMesh) {
        for (auto mesh : m_rprMeshes) {
            rprApi->Release(mesh);
        }
        for (auto instances : m_rprMeshInstances) {
            for (auto instance : instances) {
                rprApi->Release(instance);
            }
        }
        m_rprMeshInstances.clear();
        m_rprMeshes.clear();

        if (m_geomSubsets.empty()) {
            if (auto rprMesh = rprApi->CreateMesh(m_pointSamples, m_faceVertexIndices, m_normalSamples, m_normalIndices, m_uvSamples, m_uvIndices, m_faceVertexCounts, m_topology.GetOrientation())) {
                m_rprMeshes.push_back(rprMesh);
            }
        } else {
            // GeomSubset may reference face subset in any given order so we need to be able to
            //   randomly lookup face indexes but each face may be of an arbitrary number of vertices
            std::vector<int> indexesOffsetPrefixSum;
            indexesOffsetPrefixSum.reserve(m_faceVertexCounts.size());
            int offset = 0;
            for (auto numVerticesInFace : m_faceVertexCounts) {
                indexesOffsetPrefixSum.push_back(offset);
                offset += numVerticesInFace;
            }

            VtIntArray vertexIndexRemapping;
            VtIntArray normalIndexRemapping;
            VtIntArray uvIndexRemapping;

            for (auto it = m_geomSubsets.begin(); it != m_geomSubsets.end();) {
                auto const& subset = *it;
                if (subset.type != HdGeomSubset::TypeFaceSet) {
                    TF_RUNTIME_ERROR("Unknown HdGeomSubset Type");
                    it = m_geomSubsets.erase(it);
                    continue;
                }

                VtArray<VtVec3fArray> subsetPointSamples(m_pointSamples.size());
                VtArray<VtVec3fArray> subsetNormalSamples(m_normalSamples.size());
                VtArray<VtVec2fArray> subsetUvSamples(m_uvSamples.size());
                VtIntArray subsetNormalIndices;
                VtIntArray subsetUvIndices;
                VtIntArray subsetIndexes;
                VtIntArray subsetVertexPerFace;
                subsetVertexPerFace.reserve(subset.indices.size());

                vertexIndexRemapping.reserve(m_pointSamples.front().size());
                std::fill(vertexIndexRemapping.begin(), vertexIndexRemapping.begin() + m_pointSamples.front().size(), -1);
                if (!m_normalIndices.empty()) {
                    normalIndexRemapping.reserve(m_normalSamples.front().size());
                    std::fill(normalIndexRemapping.begin(), normalIndexRemapping.begin() + m_normalSamples.front().size(), -1);
                }
                if (!m_uvIndices.empty()) {
                    uvIndexRemapping.reserve(m_uvSamples.front().size());
                    std::fill(uvIndexRemapping.begin(), uvIndexRemapping.begin() + m_uvSamples.front().size(), -1);
                }

                for (auto faceIndex : subset.indices) {
                    int numVerticesInFace = m_faceVertexCounts[faceIndex];
                    subsetVertexPerFace.push_back(numVerticesInFace);

                    int faceIndexesOffset = indexesOffsetPrefixSum[faceIndex];

                    for (int i = 0; i < numVerticesInFace; ++i) {
                        const int pointIndex = m_faceVertexIndices[faceIndexesOffset + i];
                        int subsetPointIndex = vertexIndexRemapping[pointIndex];

                        bool newPoint = subsetPointIndex == -1;
                        if (newPoint) {
                            subsetPointIndex = static_cast<int>(subsetPointSamples.front().size());
                            vertexIndexRemapping[pointIndex] = subsetPointIndex;

                            for (int sampleIndex = 0; sampleIndex < m_pointSamples.size(); ++sampleIndex) {
                                subsetPointSamples[sampleIndex].push_back(m_pointSamples[sampleIndex][pointIndex]);
                            }
                        }
                        subsetIndexes.push_back(subsetPointIndex);

                        if (!m_normalSamples.empty()) {
                            if (m_normalIndices.empty()) {
                                if (newPoint) {
                                    for (int sampleIndex = 0; sampleIndex < m_normalSamples.size(); ++sampleIndex) {
                                        subsetNormalSamples[sampleIndex].push_back(m_normalSamples[sampleIndex][pointIndex]);
                                    }
                                }
                            } else {
                                const int normalIndex = m_normalIndices[faceIndexesOffset + i];
                                int subsetNormalIndex = normalIndexRemapping[normalIndex];
                                if (subsetNormalIndex == -1) {
                                    subsetNormalIndex = static_cast<int>(subsetNormalSamples.front().size());
                                    normalIndexRemapping[normalIndex] = subsetNormalIndex;

                                    for (int sampleIndex = 0; sampleIndex < m_normalSamples.size(); ++sampleIndex) {
                                        subsetNormalSamples[sampleIndex].push_back(m_normalSamples[sampleIndex][normalIndex]);
                                    }
                                }
                                subsetNormalIndices.push_back(subsetNormalIndex);
                            }
                        }

                        if (!m_uvSamples.empty()) {
                            if (m_uvIndices.empty()) {
                                if (newPoint) {
                                    for (int sampleIndex = 0; sampleIndex < m_uvSamples.size(); ++sampleIndex) {
                                        subsetUvSamples[sampleIndex].push_back(m_uvSamples[sampleIndex][pointIndex]);
                                    }
                                }
                            } else {
                                const int uvIndex = m_uvIndices[faceIndexesOffset + i];
                                int subsetuvIndex = uvIndexRemapping[uvIndex];
                                if (subsetuvIndex == -1) {
                                    subsetuvIndex = static_cast<int>(subsetUvSamples.front().size());
                                    uvIndexRemapping[uvIndex] = subsetuvIndex;

                                    for (int sampleIndex = 0; sampleIndex < m_uvSamples.size(); ++sampleIndex) {
                                        subsetUvSamples[sampleIndex].push_back(m_uvSamples[sampleIndex][uvIndex]);
                                    }
                                }
                                subsetUvIndices.push_back(subsetuvIndex);
                            }
                        }
                    }
                }

                if (auto rprMesh = rprApi->CreateMesh(subsetPointSamples, subsetIndexes, subsetNormalSamples, subsetNormalIndices, subsetUvSamples, subsetUvIndices, subsetVertexPerFace, m_topology.GetOrientation())) {
                    m_rprMeshes.push_back(rprMesh);
                    ++it;
                } else {
                    it = m_geomSubsets.erase(it);
                }
            }
        }
    }

    if (!m_rprMeshes.empty()) {
        auto name = m_cryptomatteName.empty() ? id.GetText() : m_cryptomatteName.c_str();
        for (auto& rprMesh : m_rprMeshes) {
            rprApi->SetName(rprMesh, name);
        }

        if (newMesh || (*dirtyBits & HdChangeTracker::DirtySubdivTags)) {
            PxOsdSubdivTags subdivTags = sceneDelegate->GetSubdivTags(id);

            // XXX: RPR does not support this
            /*
            auto& cornerIndices = subdivTags.GetCornerIndices();
            auto& cornerSharpness = subdivTags.GetCornerWeights();
            if (!cornerIndices.empty() && !cornerSharpness.empty()) {

            }

            auto& creaseIndices = subdivTags.GetCreaseIndices();
            auto& creaseSharpness = subdivTags.GetCreaseWeights();
            if (!creaseIndices.empty() && !creaseSharpness.empty()) {

            }
            */

            auto vertexInterpolationRule = subdivTags.GetVertexInterpolationRule();
            for (auto& rprMesh : m_rprMeshes) {
                rprApi->SetMeshVertexInterpolationRule(rprMesh, vertexInterpolationRule);
            }
        }

        if (newMesh || isRefineLevelDirty) {
            for (auto& rprMesh : m_rprMeshes) {
                rprApi->SetMeshRefineLevel(rprMesh, m_enableSubdiv ? m_refineLevel : 0);
            }
        }

        if (newMesh || (*dirtyBits & HdChangeTracker::DirtyMaterialId) ||
            (*dirtyBits & HdChangeTracker::DirtyDoubleSided) || // update twosided material node
            (*dirtyBits & HdChangeTracker::DirtyDisplayStyle) || isRefineLevelDirty) { // update displacement material
            auto getMeshMaterial = [sceneDelegate, rprApi, dirtyBits, &primvarDescsPerInterpolation, this](SdfPath const& materialId) {
                auto material = static_cast<const HdRprMaterial*>(sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, materialId));
                if (material && material->GetRprMaterialObject()) {
                    return material->GetRprMaterialObject();
                } else {
                    HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, GetId(), &primvarDescsPerInterpolation);
                    return GetFallbackMaterial(sceneDelegate, rprApi, *dirtyBits, primvarDescsPerInterpolation);
                }
            };

            if (m_geomSubsets.empty()) {
                auto material = getMeshMaterial(m_materialId);
                for (auto& mesh : m_rprMeshes) {
                    rprApi->SetMeshMaterial(mesh, material, m_displayStyle.displacementEnabled);
                }
            } else {
                if (m_geomSubsets.size() == m_rprMeshes.size()) {
                    for (int i = 0; i < m_rprMeshes.size(); ++i) {
                        auto material = getMeshMaterial(m_geomSubsets[i].materialId);
                        rprApi->SetMeshMaterial(m_rprMeshes[i], material, m_displayStyle.displacementEnabled);
                    }
                } else {
                    TF_CODING_ERROR("Unexpected number of meshes");
                }
            }
        }

        if (newMesh || (*dirtyBits & HdChangeTracker::DirtyInstancer)) {
            _UpdateInstancer(sceneDelegate, dirtyBits);
            if (auto instancer = static_cast<HdRprInstancer*>(sceneDelegate->GetRenderIndex().GetInstancer(GetInstancerId()))) {
                auto instanceTransforms = instancer->SampleInstanceTransforms(id);
                auto newNumInstances = (instanceTransforms.count > 0) ? instanceTransforms.values[0].size() : 0;
                if (newNumInstances == 0) {
                    // Reset to state without instances
                    for (auto instances : m_rprMeshInstances) {
                        for (auto instance : instances) {
                            rprApi->Release(instance);
                        }
                    }
                    m_rprMeshInstances.clear();

                    auto visibilityMask = GetVisibilityMask();
                    for (int i = 0; i < m_rprMeshes.size(); ++i) {
                        rprApi->SetMeshVisibility(m_rprMeshes[i], visibilityMask);
                    }
                } else {
                    updateTransform = false;

                    std::vector<TfSmallVector<GfMatrix4d, 2>> combinedTransforms;
                    combinedTransforms.reserve(newNumInstances);
                    for (size_t i = 0; i < newNumInstances; ++i) {
                        // Convert transform
                        // Apply prototype transform (m_transformSamples) to all the instances
                        combinedTransforms.emplace_back(instanceTransforms.count);
                        auto& instanceTransform = combinedTransforms.back();

                        if (m_transformSamples.count == 0 ||
                            (m_transformSamples.count == 1 && (m_transformSamples.values[0] == GfMatrix4d(1)))) {
                            for (size_t j = 0; j < instanceTransforms.count; ++j) {
                                instanceTransform[j] = instanceTransforms.values[j][i];
                            }
                        } else {
                            for (size_t j = 0; j < instanceTransforms.count; ++j) {
                                GfMatrix4d xf_j = m_transformSamples.Resample(instanceTransforms.times[j]);
                                instanceTransform[j] = xf_j * instanceTransforms.values[j][i];
                            }
                        }
                    }

                    // Release excessive mesh instances if any
                    for (size_t i = m_rprMeshes.size(); i < m_rprMeshInstances.size(); ++i) {
                        for (auto instance : m_rprMeshInstances[i]) {
                            rprApi->Release(instance);
                        }
                    }

                    m_rprMeshInstances.resize(m_rprMeshes.size());

                    for (int i = 0; i < m_rprMeshes.size(); ++i) {
                        auto& meshInstances = m_rprMeshInstances[i];
                        if (meshInstances.size() != newNumInstances) {
                            if (meshInstances.size() > newNumInstances) {
                                for (size_t i = newNumInstances; i < meshInstances.size(); ++i) {
                                    rprApi->Release(meshInstances[i]);
                                }
                                meshInstances.resize(newNumInstances);
                            } else {
                                for (int j = meshInstances.size(); j < newNumInstances; ++j) {
                                    meshInstances.push_back(rprApi->CreateMeshInstance(m_rprMeshes[i]));
                                }
                            }
                        }

                        for (int j = 0; j < newNumInstances; ++j) {
                            rprApi->SetTransform(meshInstances[j], instanceTransforms.count, instanceTransforms.times.data(), combinedTransforms[j].data());
                        }

                        // Hide prototype
                        rprApi->SetMeshVisibility(m_rprMeshes[i], kInvisible);
                    }
                }
            }
        }

        if (newMesh || ((*dirtyBits & HdChangeTracker::DirtyVisibility) || isVisibilityMaskDirty)) {
            auto visibilityMask = GetVisibilityMask();
            if (m_rprMeshInstances.empty()) {
                for (auto& rprMesh : m_rprMeshes) {
                    rprApi->SetMeshVisibility(rprMesh, visibilityMask);
                }
            } else {
                // Do not touch prototype meshes (m_rprMeshes), set visibility for instances only
                for (auto& instances : m_rprMeshInstances) {
                    for (auto& rprMesh : instances) {
                        rprApi->SetMeshVisibility(rprMesh, visibilityMask);
                    }
                }
            }
        }

        if (newMesh || isIdDirty) {
            uint32_t id = m_id >= 0 ? uint32_t(m_id) : GetPrimId();
            for (auto& rprMesh : m_rprMeshes) {
                rprApi->SetMeshId(rprMesh, id);
            }
            for (auto& instances : m_rprMeshInstances) {
                for (auto& rprMesh : instances) {
                    rprApi->SetMeshId(rprMesh, id);
                }
            }
        }

        if (newMesh || isIgnoreContourDirty) {
            for (auto& rprMesh : m_rprMeshes) {
                rprApi->SetMeshIgnoreContour(rprMesh, m_ignoreContour);
            }
        }

        if (updateTransform) {
            for (auto& rprMesh : m_rprMeshes) {
                rprApi->SetTransform(rprMesh, m_transformSamples.count, m_transformSamples.times.data(), m_transformSamples.values.data());
            }
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void HdRprMesh::Finalize(HdRenderParam* renderParam) {
    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    for (auto mesh : m_rprMeshes) {
        rprApi->Release(mesh);
    }
    for (auto instances : m_rprMeshInstances) {
        for (auto instance : instances) {
            rprApi->Release(instance);
        }
    }
    m_rprMeshInstances.clear();
    m_rprMeshes.clear();

    rprApi->Release(m_fallbackMaterial);
    m_fallbackMaterial = nullptr;

    for (auto& oldGeomSubset : m_geomSubsets) {
        if (!oldGeomSubset.materialId.IsEmpty()) {
            rprRenderParam->UnsubscribeFromMaterialUpdates(oldGeomSubset.materialId, GetId());
        }
    }

    HdRprBaseRprim::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
