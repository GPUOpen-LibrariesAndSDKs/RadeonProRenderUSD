#include "mesh.h"
#include "instancer.h"
#include "material.h"
#include "materialFactory.h"

#include "pxr/imaging/pxOsd/tokens.h"

#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/sprim.h"
#include "pxr/imaging/hd/smoothNormals.h"

#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/quaternion.h"
#include "pxr/base/gf/rotation.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec4f.h"

#include "pxr/imaging/pxOsd/subdivTags.h"

#include "pxr/usd/usdUtils/pipeline.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprMesh::HdRprMesh(SdfPath const & id, HdRprApiSharedPtr rprApiShared, SdfPath const & instancerId) : HdMesh(id, instancerId)
{
	m_rprApiWeakPtr = rprApiShared;
}

HdRprMesh::~HdRprMesh() {
    if (auto rprApi = m_rprApiWeakPtr.lock()) {
        rprApi->DeleteMaterial(m_fallbackMaterial);
        for (auto rprMesh : m_rprMeshes)
        {
            rprApi->DeleteMesh(rprMesh);
        }
    }
}

HdDirtyBits
HdRprMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
	return bits;
}

HdDirtyBits
HdRprMesh::GetInitialDirtyBitsMask() const
{
	// The initial dirty bits control what data is available on the first
	// run through _PopulateMesh(), so it should list every data item
	// that _PopluateMesh requests.
	int mask = HdChangeTracker::Clean
		| HdChangeTracker::DirtyPoints
		| HdChangeTracker::DirtyTopology
		| HdChangeTracker::DirtyTransform
		| HdChangeTracker::DirtyPrimvar
		| HdChangeTracker::DirtyNormals
		| HdChangeTracker::DirtyInstanceIndex
		| HdChangeTracker::AllDirty
		;

	return (HdDirtyBits)mask;
}

void
HdRprMesh::_InitRepr(TfToken const &reprName,
	HdDirtyBits *dirtyBits)
{
	TF_UNUSED(reprName);
	TF_UNUSED(dirtyBits);

	// No-op
}

void HdRprMesh::Sync(
    HdSceneDelegate * sceneDelegate
    , HdRenderParam * renderParam
    , HdDirtyBits * dirtyBits
    , TfToken const & reprName) {
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
    if (!rprApi) {
        TF_CODING_ERROR("RprApi is expired");
        return;
    }

    SdfPath const& id = GetId();

    if (*dirtyBits & HdChangeTracker::DirtyTopology) {
        HdMeshTopology meshTopology = GetMeshTopology(sceneDelegate);
        const VtIntArray& indexes = meshTopology.GetFaceVertexIndices();
        const VtIntArray& vertexPerFace = meshTopology.GetFaceVertexCounts();
        int numFaces = meshTopology.GetNumFaces();

        auto hdMaterialId = sceneDelegate->GetMaterialId(id);

        auto geomSubsets = meshTopology.GetGeomSubsets();
        // If the geometry has been partitioned into subsets, add an
        // additional subset representing anything left over.
        if (!geomSubsets.empty()) {
            std::vector<bool> faceIsUnused(numFaces, true);
            size_t numUnusedFaces = faceIsUnused.size();
            for (auto const& subset : geomSubsets) {
                for (int index : subset.indices) {
                    if (TF_VERIFY(index < numFaces) && faceIsUnused[index]) {
                        faceIsUnused[index] = false;
                        numUnusedFaces--;
                    }
                }
            }
            // If we found any unused faces, build a final subset with those faces.
            // Use the material bound to the parent mesh.
            if (numUnusedFaces) {
                geomSubsets.push_back(HdGeomSubset());
                HdGeomSubset& unusedSubset = geomSubsets.back();
                unusedSubset.type = HdGeomSubset::TypeFaceSet;
                unusedSubset.id = id;
                unusedSubset.materialId = hdMaterialId;
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

        HdPrimvarDescriptorVector primvarDescsPerInterpolation[HdInterpolationCount];
        for (int i = 0; i < HdInterpolationCount; ++i) {
            auto interpolation = static_cast<HdInterpolation>(i);
            primvarDescsPerInterpolation[i] = sceneDelegate->GetPrimvarDescriptors(id, interpolation);
        }
        auto getPrimvarIndices = [&primvarDescsPerInterpolation, &indexes](
            TfToken const& primvarName, VtIntArray& outIndices) {
            auto it = std::find_if(primvarDescsPerInterpolation, primvarDescsPerInterpolation + HdInterpolationCount,
                [&primvarName](HdPrimvarDescriptorVector const& descriptors) {
                    return std::any_of(descriptors.begin(), descriptors.end(), [&primvarName](HdPrimvarDescriptor const& descriptor) {return descriptor.name == primvarName;});
            });
            auto interpolationType = static_cast<HdInterpolation>(std::distance(primvarDescsPerInterpolation, it));

            if (interpolationType == HdInterpolationFaceVarying) {
                outIndices.reserve(indexes.size());
                for (size_t i = 0; i < indexes.size(); ++i) {
                    outIndices.push_back(i);
                }
            } else if (interpolationType == HdInterpolationVertex
                       // HdInterpolationCount implies no interpolation specified - assume vertex
                       || interpolationType == HdInterpolationCount) {
                // stIndexes same as pointIndexes - do nothing
                return;
            } else {
                TF_CODING_WARNING("Not handled %s primvar interpolation type: %d", primvarName.GetText(),
                                  interpolationType);
            }
        };

        VtValue value;
        value = sceneDelegate->Get(id, HdTokens->points);
        VtVec3fArray points = value.Get<VtVec3fArray>();

        auto stToken = UsdUtilsGetPrimaryUVSetName();
        VtVec2fArray st;
        VtIntArray stIndexes;
        value = sceneDelegate->Get(id, stToken);
        if (value.IsHolding<VtVec2fArray>()) {
            st = value.UncheckedGet<VtVec2fArray>();
            if (!st.empty()) {
                getPrimvarIndices(stToken, stIndexes);
            }
        }

        VtVec3fArray normals;
        VtIntArray normalIndexes;
        value = sceneDelegate->Get(id, HdTokens->normals);
        if (value.IsHolding<VtVec3fArray>()) {
            normals = value.UncheckedGet<VtVec3fArray>();
            if (!normals.empty()) {
                getPrimvarIndices(HdTokens->normals, normalIndexes);
            }
        } else {
            Hd_VertexAdjacency adjacency;
            adjacency.BuildAdjacencyTable(&meshTopology);

            normals = Hd_SmoothNormals::ComputeSmoothNormals(&adjacency, points.size(), points.cdata());
        }

        if (!m_fallbackMaterial) {
            // get Color
            HdPrimvarDescriptorVector primvars = sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationConstant);

            TF_FOR_ALL(primvarIt, primvars) {
                if (primvarIt->name == HdTokens->displayColor) {
                    VtValue val = sceneDelegate->Get(id, HdTokens->displayColor);

                    if (!val.IsEmpty()) {
                        VtArray<GfVec3f> color = val.Get<VtArray<GfVec3f>>();
                        MaterialAdapter matAdapter = MaterialAdapter(EMaterialType::COLOR,
                            MaterialParams{ {HdPrimvarRoleTokens->color, VtValue(color[0]) } });
                        m_fallbackMaterial = rprApi->CreateMaterial(matAdapter);
                    }
                    break;
                }
            }
        }

        for (auto& rprMesh : m_rprMeshes) {
            rprApi->DeleteMesh(rprMesh);
        }
        m_rprMeshes.clear();

        auto setMeshMaterial = [&sceneDelegate, &rprApi](RprApiObject rprMesh, SdfPath const& materialId, RprApiMaterial* fallbackMaterial) {
            auto material = static_cast<const HdRprMaterial*>(sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, materialId));
            if (material && material->GetRprMaterialObject()) {
                rprApi->SetMeshMaterial(rprMesh, material->GetRprMaterialObject());
            } else if (fallbackMaterial) {
                rprApi->SetMeshMaterial(rprMesh, fallbackMaterial);
            }
        };
        if (geomSubsets.empty()) {
            if (auto rprMesh = rprApi->CreateMesh(points, indexes, normals, normalIndexes, st, stIndexes, vertexPerFace)) {
                m_rprMeshes.push_back(rprMesh);
                setMeshMaterial(rprMesh, hdMaterialId, m_fallbackMaterial);
            }
        } else {
            if (geomSubsets.size() == 1) {
                if (auto rprMesh = rprApi->CreateMesh(points, indexes, normals, normalIndexes, st, stIndexes, vertexPerFace)) {
                    m_rprMeshes.push_back(rprMesh);
                    setMeshMaterial(rprMesh, geomSubsets.back().materialId, m_fallbackMaterial);
                }
            } else {
                // GeomSubset may reference face subset in any given order so we need to be able to
                //   randomly lookup face indexes but each face may be of an arbitrary number of vertices
                std::vector<int> indexesOffsetPrefixSum;
                indexesOffsetPrefixSum.reserve(vertexPerFace.size());
                int offset = 0;
                for (auto numVerticesInFace : vertexPerFace) {
                    indexesOffsetPrefixSum.push_back(offset);
                    offset += numVerticesInFace;
                }

                for (auto const& subset : geomSubsets) {
                    if (subset.type != HdGeomSubset::TypeFaceSet) {
                        TF_WARN("Unknown HdGeomSubset Type");
                        continue;
                    }

                    VtVec3fArray subsetPoints;
                    VtVec3fArray subsetNormals;
                    VtVec2fArray subsetSt;
                    VtIntArray subsetIndexes;
                    VtIntArray subsetVertexPerFace;
                    subsetVertexPerFace.reserve(subset.indices.size());

                    int count = 0;
                    for (auto faceIndex : subset.indices) {
                        int numVerticesInFace = vertexPerFace[faceIndex];
                        subsetVertexPerFace.push_back(numVerticesInFace);

                        int faceIndexesOffset = indexesOffsetPrefixSum[faceIndex];

                        for (int i = 0; i < numVerticesInFace; ++i) {
                            subsetIndexes.push_back(count++);

                            int pointIndex = indexes[faceIndexesOffset + i];
                            subsetPoints.push_back(points[pointIndex]);

                            if (!normals.empty()) {
                                int normalIndex = normalIndexes.empty() ? pointIndex : normalIndexes[faceIndexesOffset + i];
                                subsetNormals.push_back(normals[normalIndex]);
                            }

                            if (!st.empty()) {
                                int stIndex = stIndexes.empty() ? pointIndex : stIndexes[faceIndexesOffset + i];
                                subsetSt.push_back(st[stIndex]);
                            }
                        }
                    }

                    if (auto rprMesh = rprApi->CreateMesh(subsetPoints, subsetIndexes, subsetNormals, VtIntArray(), subsetSt, VtIntArray(), subsetVertexPerFace))
                    {
                        m_rprMeshes.push_back(rprMesh);
                        setMeshMaterial(rprMesh, subset.materialId, m_fallbackMaterial);
                    }
                }
            }
        }
    }

    if (!m_rprMeshes.empty()) {
        if (*dirtyBits & HdChangeTracker::DirtyTransform) {
            GfMatrix4d transform = sceneDelegate->GetTransform(id);
            for (auto rprMesh : m_rprMeshes) {
                rprApi->SetMeshTransform(rprMesh, transform);
            }
        }

        if (*dirtyBits & HdChangeTracker::DirtyDisplayStyle) {
            int refineLevel = sceneDelegate->GetDisplayStyle(id).refineLevel;
            auto boundaryInterpolation = refineLevel > 0 ? sceneDelegate->GetSubdivTags(id).GetVertexInterpolationRule() : TfToken();
            for (auto rprMesh : m_rprMeshes)
            {
                rprApi->SetMeshRefineLevel(rprMesh, refineLevel, boundaryInterpolation);
            }
        }

        if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
            _UpdateVisibility(sceneDelegate, dirtyBits);
            for (auto mesh : m_rprMeshes) {
                rprApi->SetMeshVisibility(mesh, _sharedData.visible);
            }
        }

        if (*dirtyBits & HdChangeTracker::DirtyInstancer) {
            if (auto instancer = static_cast<HdRprInstancer*>(sceneDelegate->GetRenderIndex().GetInstancer(GetInstancerId()))) {
                // XXX: if current number of meshes less than previous we have a memory leak
                //      but instead of releasing it here we should refactor rprApi to return
                //      complete object that obeys RAII idiom
                m_rprMeshInstances.resize(m_rprMeshes.size());

                auto transforms = instancer->ComputeTransforms(_sharedData.rprimID);
                if (transforms.empty()) {
                    for (int i = 0; i < m_rprMeshes.size(); ++i) {
                        rprApi->SetMeshVisibility(m_rprMeshes[i], _sharedData.visible);
                        for (auto instance : m_rprMeshInstances[i]) {
                            rprApi->DeleteInstance(instance);
                        }
                        m_rprMeshInstances[i].clear();
                    }
                } else {
                    for (int i = 0; i < m_rprMeshes.size(); ++i) {
                        auto& meshInstances = m_rprMeshInstances[i];
                        if (meshInstances.size() != transforms.size()) {
                            if (meshInstances.size() > transforms.size()) {
                                for (int j = transforms.size(); j < meshInstances.size(); ++j) {
                                    rprApi->DeleteInstance(meshInstances[j]);
                                }
                                meshInstances.resize(transforms.size(), nullptr);
                            }
                            else {
                                for (int j = meshInstances.size(); j < transforms.size(); ++j) {
                                    meshInstances.push_back(rprApi->CreateMeshInstance(m_rprMeshes[i]));
                                }
                            }
                        }

                        for (int j = 0; j < transforms.size(); ++j) {
                            rprApi->SetMeshTransform(meshInstances[j], transforms[j]);
                        }

                        // Hide prototype
                        rprApi->SetMeshVisibility(m_rprMeshes[i], false);
                    }
                }
            }
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

PXR_NAMESPACE_CLOSE_SCOPE
