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
	m_rprApiWeakPtr = rprApiShared;
}

HdRprMesh::~HdRprMesh() {
    if (auto rprApi = m_rprApiWeakPtr.lock()) {
        rprApi->DeleteMaterial(m_fallbackMaterial);
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
	, TfToken const & reprName)
{
	HD_TRACE_FUNCTION();
	HF_MALLOC_TAG_FUNCTION();

	//HdDirtyBits originalDirtyBits = *dirtyBits;

	/*HdRprim::_Sync(sceneDelegate,
		reprName,
		forcedRepr,
		&originalDirtyBits); */ // removed with 0.8.5+

	HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
	if (!rprApi)
	{
		TF_CODING_ERROR("RprApi is expired");
		return;
	}

	SdfPath const& id = GetId();

	if (*dirtyBits & HdChangeTracker::DirtyTopology)
	{
		HdMeshTopology meshTopology = GetMeshTopology(sceneDelegate);

		const VtIntArray & indexes = meshTopology.GetFaceVertexIndices();
		const VtIntArray & vertexPerFace = meshTopology.GetFaceVertexCounts();

		HdPrimvarDescriptorVector primvarDescsPerInterpolation[HdInterpolationCount];
		for (int i = 0; i < HdInterpolationCount; ++i) {
			auto interpolation = static_cast<HdInterpolation>(i);
			primvarDescsPerInterpolation[i] = sceneDelegate->GetPrimvarDescriptors(id, interpolation);
		}
		auto getPrimvarInterpolation = [&primvarDescsPerInterpolation](pxr::TfToken const& token) -> HdInterpolation {
			auto it = std::find_if(primvarDescsPerInterpolation, primvarDescsPerInterpolation + HdInterpolationCount,
				[&token](HdPrimvarDescriptorVector const& descriptors) {
				return std::any_of(descriptors.begin(), descriptors.end(), [&token](HdPrimvarDescriptor const& descriptor) {
					return descriptor.name == token;
				});
			});
			return static_cast<HdInterpolation>(std::distance(primvarDescsPerInterpolation, it));
		};

		VtValue value;
		value = sceneDelegate->Get(id, HdTokens->points);
		VtVec3fArray points = value.Get<VtVec3fArray>();

		VtVec2fArray st;
		float timeSamples[1] = {0.0f};
		sceneDelegate->SamplePrimvar(id, TfToken("st"), 1, timeSamples, &value);
		if (value.IsHolding<VtVec2fArray>())
		{
			st = value.Get<VtVec2fArray>();
		}

		VtIntArray stIndexes;
		if (!st.empty()) {
			value = sceneDelegate->Get(id, TfToken("primvars:st:indices"));
			if (value.IsHolding<VtIntArray>() && value.GetArraySize() > 0)
			{
				stIndexes = value.UncheckedGet<VtIntArray>();
			} else {
				auto stInterpolation = getPrimvarInterpolation(pxr::TfToken("st"));
				if (stInterpolation == HdInterpolationFaceVarying) {
					stIndexes.reserve(indexes.size());
					for (size_t i = 0; i < indexes.size(); ++i) {
						stIndexes.push_back(i);
					}
				} else if (stInterpolation == HdInterpolationVertex) {
					// stIndexes same as pointIndexes
				} else {
					TF_CODING_WARNING("Not handled st primvar interpolation type");
				}
			}
		}

		VtVec3fArray normals;
		VtIntArray normalIndexes;
		sceneDelegate->SamplePrimvar(id, HdTokens->normals, 1, timeSamples, &value);
		if (value.IsHolding<VtVec3fArray>())
		{
			normals = value.UncheckedGet<VtVec3fArray>();
			
			value = sceneDelegate->Get(id, TfToken("primvars:normals:indices"));
			if (value.IsHolding<VtIntArray>() && value.GetArraySize() > 0) {
				normalIndexes = value.UncheckedGet<VtIntArray>();
			} else {
				auto normalsInterpolation = getPrimvarInterpolation(pxr::TfToken("normals"));
				if (normalsInterpolation == HdInterpolationFaceVarying) {
					normalIndexes.reserve(indexes.size());
					for (size_t i = 0; i < indexes.size(); ++i) {
						normalIndexes.push_back(i);
					}
				} else if (normalsInterpolation == HdInterpolationVertex) {
					// normalIndexes same as pointIndexes
				} else {
					TF_CODING_WARNING("Not handled normal primvar interpolation type");
				}
			}
		}
		else
		{
			Hd_VertexAdjacency adjacency;
			adjacency.BuildAdjacencyTable(&meshTopology);

			normals = Hd_SmoothNormals::ComputeSmoothNormals(&adjacency, points.size(), points.cdata());
		}

		if (m_rprMesh)
		{
			rprApi->DeleteMesh(m_rprMesh);
		}
		m_rprMesh = rprApi->CreateMesh(points, indexes, normals, normalIndexes, st, stIndexes, vertexPerFace);

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
						m_fallbackMaterial = rprApi->CreateMaterial(matAdapter);

						rprApi->SetMeshMaterial(m_rprMesh, m_fallbackMaterial);
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
