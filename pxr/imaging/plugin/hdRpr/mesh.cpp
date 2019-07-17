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

namespace {
	template<typename OutputType>
	void ReadPrimvar(HdSceneDelegate * sceneDelegate, SdfPath const& primId, TfToken primvarName,
				HdPrimvarDescriptor const& primvarDescriptor, OutputType& outValue,
				InterpolationType& outInterpolation)
	{
		// if outInterpolation is non-None, we assume we've already read this primvar
		if (outInterpolation != InterpolationType::NONE)
		{
			return;
		}
		if (primvarDescriptor.name == primvarName) {
			VtValue normalsValue = sceneDelegate->Get(primId, primvarName);
			if (normalsValue.IsHolding<OutputType>()) {
				switch(primvarDescriptor.interpolation)
				{
				case HdInterpolationVertex:
					outInterpolation = InterpolationType::Vertex;
					break;
				case HdInterpolationFaceVarying:
					outInterpolation = InterpolationType::FaceVarying;
					break;
				default:
					TF_CODING_ERROR("UVs had unsupported interpolation type: %d",
									primvarDescriptor.interpolation);
					return;
				}
				outValue = normalsValue.UncheckedGet<OutputType>();
				return;
			}
		}
	}
}

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

void HdRprMesh::Sync(
	HdSceneDelegate * sceneDelegate
	, HdRenderParam * renderParam
	, HdDirtyBits * dirtyBits
	, TfToken const & reprName)
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
		InterpolationType stInterpolation = InterpolationType::NONE;

		VtVec3fArray normals;
		InterpolationType normalsInterpolation = InterpolationType::NONE;

		const HdInterpolation hdInterpTypes[] = {
				HdInterpolationVertex,
				HdInterpolationFaceVarying
		};

		// If we just try to get st, and there are none, it will raise a scary error...
		// but there's no "easy" way to check if st / uvs are defined - so we use
		// GetPrimvarDescriptors, and iterate through all
		size_t numInterpTypes = sizeof(hdInterpTypes) / sizeof(decltype(hdInterpTypes[0]));
		for (size_t i = 0; i < numInterpTypes; ++i) {
			auto primvars = GetPrimvarDescriptors(sceneDelegate, hdInterpTypes[i]);
			for (HdPrimvarDescriptor const& pv: primvars) {
				ReadPrimvar(sceneDelegate, id, UsdUtilsGetPrimaryUVSetName(), pv, st, stInterpolation);
				ReadPrimvar(sceneDelegate, id, HdTokens->normals, pv, normals, normalsInterpolation);
			}
		}

		if (normalsInterpolation == InterpolationType::NONE)
		{
			Hd_VertexAdjacency adjacency;
			adjacency.BuildAdjacencyTable(&meshTopology);

			//VtVec3fArray normals = adjacency.ComputeSmoothNormals(points.size(), points.cdata());
			normals = Hd_SmoothNormals::ComputeSmoothNormals(&adjacency, points.size(), points.cdata());
			normalsInterpolation = InterpolationType::Vertex;
		}

		const VtIntArray & indexes = meshTopology.GetFaceVertexIndices();
		const VtIntArray & vertexPerFace = meshTopology.GetFaceVertexCounts();

		if (m_rprMesh)
		{
			rprApi->DeleteMesh(m_rprMesh);
		}
		m_rprMesh = rprApi->CreateMesh(points, normals, normalsInterpolation, st, stInterpolation, indexes, vertexPerFace);

		const HdRprMaterial * material = static_cast<const HdRprMaterial *>(sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, sceneDelegate->GetMaterialId(GetId())));

		if (material == NULL) {
			// get Color
			HdPrimvarDescriptorVector primvars = sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationConstant);

			TF_FOR_ALL(primvarIt, primvars) {
				if (primvarIt->name == HdTokens->displayColor) {
					VtValue val = sceneDelegate->Get(id, HdTokens->displayColor);

					if (!val.IsEmpty()) {
						VtArray<GfVec3f> color = val.Get<VtArray<GfVec3f>>();
						MaterialAdapter matAdapter = MaterialAdapter(EMaterialType::COLOR,
							MaterialParams{{HdPrimvarRoleTokens->color, VtValue(color[0]) }});
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

PXR_NAMESPACE_CLOSE_SCOPE
