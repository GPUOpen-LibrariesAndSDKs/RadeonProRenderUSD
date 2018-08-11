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

PXR_NAMESPACE_OPEN_SCOPE

HdRprMesh::HdRprMesh(SdfPath const & id, HdRprApiSharedPtr rprApiShared, SdfPath const & instancerId) : HdMesh(id, instancerId)
{
	m_rprApiWeakPrt = rprApiShared;
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

void HdRprMesh::_UpdateRepr(HdSceneDelegate * sceneDelegate, TfToken const & reprName, HdDirtyBits * dirtyBitsState)
{
	TF_UNUSED(sceneDelegate);
	TF_UNUSED(reprName);
	TF_UNUSED(dirtyBitsState);

	// No-op
}


void HdRprMesh::Sync(
	HdSceneDelegate * sceneDelegate
	, HdRenderParam * renderParam
	, HdDirtyBits * dirtyBits
	, TfToken const & reprName
	, bool forcedRepr)
{
	HD_TRACE_FUNCTION();
	HF_MALLOC_TAG_FUNCTION();

	//HdDirtyBits originalDirtyBits = *dirtyBits;

	/*HdRprim::_Sync(sceneDelegate,
		reprName,
		forcedRepr,
		&originalDirtyBits); */ // removed with 0.8.5+

	HdRprApiSharedPtr rprApi = m_rprApiWeakPrt.lock();
	if (!rprApi)
	{
		TF_CODING_ERROR("RprApi is expired");
		return;
	}

	SdfPath const& id = GetId();

	if (*dirtyBits & HdChangeTracker::DirtyTopology)
	{
		HdMeshTopology meshTopology = GetMeshTopology(sceneDelegate);

		VtValue value;
		value = sceneDelegate->Get(id, HdTokens->points);
		VtVec3fArray points = value.Get<VtVec3fArray>();

		VtVec2fArray st;

		// TODO check if 'st' is present to avoid warning 
		value = sceneDelegate->Get(id, TfToken("st"));
		if (value.IsHolding<VtVec2fArray>())
		{
			st = value.Get<VtVec2fArray>();
		}

		Hd_VertexAdjacency adjacency;
		adjacency.BuildAdjacencyTable(&meshTopology);

		//VtVec3fArray normals = adjacency.ComputeSmoothNormals(points.size(), points.cdata());
		VtVec3fArray normals = Hd_SmoothNormals::ComputeSmoothNormals(&adjacency, points.size(), points.cdata());

		const VtIntArray & indexes = meshTopology.GetFaceVertexIndices();
		const VtIntArray & vertexPerFace = meshTopology.GetFaceVertexCounts();

		if (m_rprMesh)
		{
			rprApi->DeleteMesh(m_rprMesh);
		}
		m_rprMesh = rprApi->CreateMesh(points, normals, st, indexes, vertexPerFace);

		const HdRprMaterial * material = static_cast<const HdRprMaterial *>(sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, sceneDelegate->GetMaterialId(GetId())));

		if (material == NULL) {
			// get Color
			HdPrimvarDescriptorVector primvars = sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationConstant);
			
			TF_FOR_ALL(primvarIt, primvars) {
				if (primvarIt->name == HdTokens->color) {
					VtValue val = sceneDelegate->Get(id, HdTokens->color);
				
					if (!val.IsEmpty()) {
						VtArray<GfVec4f> color = val.Get<VtArray<GfVec4f>>();
						MaterialAdapter matAdapter = MaterialAdapter(EMaterialType::COLOR, 
							MaterialParams{{ HdTokens->color, VtValue(color[0]) }});
						RprApiMaterial * rprMaterial = rprApi->CreateMaterial(matAdapter);

						rprApi->SetMeshMaterial(m_rprMesh, rprMaterial);
					}
					break;
				}
			}
		}
		else if (material &&  material->GetRprMaterialObject())
		{
			rprApi->SetMeshMaterial(m_rprMesh, material->GetRprMaterialObject());
		}
	}

	if (m_rprMesh && *dirtyBits & HdChangeTracker::DirtyTransform)
	{
		GfMatrix4d transform = sceneDelegate->GetTransform(id);
		rprApi->SetMeshTransform(m_rprMesh, transform);
	}


	if (m_rprMesh && *dirtyBits & HdChangeTracker::DirtyDisplayStyle)
	{
		int refineLevel = sceneDelegate->GetDisplayStyle(id).refineLevel;
		TfToken boundaryInterpolation = sceneDelegate->GetSubdivTags(id).GetVertexInterpolationRule();
		rprApi->SetMeshRefineLevel(m_rprMesh, refineLevel, boundaryInterpolation);
	}
	
	if (HdRprInstancer *instancer = static_cast<HdRprInstancer*>(sceneDelegate->GetRenderIndex().GetInstancer(GetInstancerId())))
	{
		VtMatrix4dArray transforms = instancer->ComputeTransforms(_sharedData.rprimID);
		rprApi->CreateInstances(m_rprMesh, transforms, m_rprMeshInstances);
	}

	*dirtyBits = HdChangeTracker::Clean;
}

/*
void HdRprMesh::PopulateMesh(HdSceneDelegate * sceneDelegate, HdDirtyBits * dirtyBits, HdMeshReprDesc const & desc)
{
	HD_TRACE_FUNCTION();
	HF_MALLOC_TAG_FUNCTION();

	RprApi * rprApi = _renderParamPtr->GetRprApi();

	if (!rprApi)
	{
		return;
	}

	SdfPath const& id = GetId();

	// Update Visibility
	// TODO: change visibility
	_UpdateVisibility(sceneDelegate, dirtyBits);
	
	/// Forse init material
	sceneDelegate->Get(GetMaterialId(), HdPrimTypeTokens->material);

	if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
		// When pulling a new topology, we don't want to overwrite the
		// refine level or subdiv tags, which are provided separately by the
		// scene delegate, so we save and restore them.
		PxOsdSubdivTags subdivTags = m_topology.GetSubdivTags();
		int refineLevel = m_topology.GetDisplayStyle();
		m_topology = HdMeshTopology(GetMeshTopology(sceneDelegate), refineLevel);
		m_topology.SetSubdivTags(subdivTags);
	}
	if (HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id)) {
		m_topology.SetSubdivTags(sceneDelegate->GetSubdivTags(id));
	}
	if (HdChangeTracker::IsRefineLevelDirty(*dirtyBits, id)) {
		m_topology = HdMeshTopology(m_topology,
			sceneDelegate->GetDisplayStyle(id));
	}

	// Transformation is applied on mesh. Each transformation changes requite recreate new mesh
	bool isTransformDirty = HdChangeTracker::IsTransformDirty(*dirtyBits, id);

	bool isTopologyDirty = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);

	bool isPrimValDirty = HdChangeTracker::IsPrimVarDirty(*dirtyBits, id, HdTokens->points);


	if (desc.geomStyle != HdMeshGeomStyle::HdMeshGeomStyleInvalid
		&&	(isTransformDirty || isTopologyDirty || isPrimValDirty))
	{
		CreateMesh(sceneDelegate);
	}

	// Clean all dirty bits.
	*dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

*/

void HdRprMesh::CreateMesh(HdSceneDelegate * sceneDelegate)
{	
	/*HD_TRACE_FUNCTION();
	RprApi * rprApi = _renderParamPtr->GetRprApi();
	if (!rprApi)
	{
		return;
	}

	SdfPath const& id = GetId();
	GfMatrix4d transform = sceneDelegate->GetTransform(id);

	VtValue value = sceneDelegate->Get(id, HdTokens->points);
	VtVec3fArray points = value.Get<VtVec3fArray>();

	VtValue data = GetPrimVar(sceneDelegate, HdTokens->color);
	VtVec4fArray colors = data.UncheckedGet<VtVec4fArray>();

	VtValue dataUv = GetPrimVar(sceneDelegate, UsdRprToken::uv);
	VtValue dataIdx = GetPrimVar(sceneDelegate, UsdRprToken::uvIndexes);
	
	VtVec2fArray uvArray;
	VtIntArray uvIdxArray;

	//__debugbreak();

	bool IsUv = (dataUv.IsHolding<VtVec2fArray>() && dataIdx.IsHolding<VtIntArray>());
	if (IsUv)
	{
		uvArray = dataUv.Get<VtVec2fArray>();
		uvIdxArray = dataIdx.Get<VtIntArray>();
	}


	Hd_VertexAdjacency adjacency;
	adjacency.BuildAdjacencyTable(&m_topology);

	VtVec3fArray normals = adjacency.ComputeSmoothNormals(points.size(), points.cdata());

	std::vector<int> vpf;
	std::vector<int> idx;

	/// Topology does not constain uv coordinates and can not be triangulater
	if (k_isUseTriangulate && !IsUv)
	{
		const int vertPerTriangle = 3;

		VtVec3iArray triangulatedIndices;
		VtIntArray trianglePrimitiveParams;

		HdMeshUtil meshUtil(&m_topology, GetId());
		meshUtil.ComputeTriangleIndices(&triangulatedIndices,
			&trianglePrimitiveParams);

		idx.resize(triangulatedIndices.size() * vertPerTriangle);
		memcpy(idx.data(), triangulatedIndices.data(), sizeof(int) * triangulatedIndices.size() * vertPerTriangle);

		vpf.resize(triangulatedIndices.size(), vertPerTriangle);
	}
	else
	{
		const VtIntArray & indexes = m_topology.GetFaceVertexIndices();
		idx = std::vector<int>(indexes.begin(), indexes.end());

		const VtIntArray & vertexPerFace = m_topology.GetFaceVertexCounts();
		vpf = std::vector<int>(vertexPerFace.begin(), vertexPerFace.end());
	}

	const size_t colorCount = colors.size();
	const size_t vertSize = points.size() * 3;

	std::vector<float> vert3f(vertSize, 0.f);
	std::vector<float> norm3f(vertSize, 0.f);
	std::vector<float> uv2f;
	std::vector<int> uvIdx(idx.size());
	std::vector<const void *> materials;

	const HdRprMaterial * material = static_cast<const HdRprMaterial *>(sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, GetMaterialId()));
	if (material &&  material->GetRprMaterial())
	{
		materials.resize(vpf.size(), material->GetRprMaterial());
	}
	else
	{
		const void * mat = rprApi->GetSimpleMaterial(1.f, 1.f, 1.f, 1.f);
		materials.resize(vpf.size(), mat);

	}

	if (!(uvArray.empty() && uvIdxArray.empty()))
	{
		uv2f.resize(uvArray.size() * 2);
		uvIdx.resize(uvIdxArray.size());

		memcpy(uv2f.data(), uvArray.data(), sizeof(float) *  uvArray.size() * 2);
		memcpy(uvIdx.data(), uvIdxArray.data(),  sizeof(int) * uvIdxArray.size());
	}

	memcpy(vert3f.data(), points.data(), sizeof(float) * vert3f.size());
	memcpy(norm3f.data(), normals.data(), sizeof(float) * norm3f.size());

	{
		std::string meshName = id.GetName() + std::to_string(this->GetPrimId());

		rprApi->CreateMesh(meshName, vert3f, norm3f, uv2f, idx, uvIdx, vpf, materials);
		rprApi->SetTransformation(meshName, transform);

		HdRprInstancer *instancer = static_cast<HdRprInstancer*>(
			sceneDelegate->GetRenderIndex().GetInstancer(GetInstancerId()));


		if (instancer)
		{
			rprApi->CreateInstances(meshName
				, instancer->ComputeTransforms(_sharedData.rprimID));
		}
	}*/
}

PXR_NAMESPACE_CLOSE_SCOPE