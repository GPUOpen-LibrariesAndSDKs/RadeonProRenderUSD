#include "renderDelegate.h"

#include "pxr/imaging/glf/glew.h"
#include "pxr/imaging/hd/camera.h"

#include "renderPass.h"
#include "mesh.h"
#include "instancer.h"
#include "domeLight.h"
#include "sphereLight.h"
#include "rectLight.h"
#include "material.h"
#include "tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
	HdRprDelegate * g_HdRprDelegate = nullptr;
}

const TfTokenVector HdRprDelegate::SUPPORTED_RPRIM_TYPES =
{
	HdPrimTypeTokens->mesh,
};

const TfTokenVector HdRprDelegate::SUPPORTED_SPRIM_TYPES =
{
	HdPrimTypeTokens->camera,
	HdPrimTypeTokens->material,
	HdPrimTypeTokens->rectLight,
	HdPrimTypeTokens->sphereLight,
	HdPrimTypeTokens->domeLight,
};

const TfTokenVector HdRprDelegate::SUPPORTED_BPRIM_TYPES =
{
};

//std::atomic_int HdRprDelegate::_counterResourceRegistry;
//HdResourceRegistrySharedPtr HdRprDelegate::_resourceRegistry;


HdRprDelegate::HdRprDelegate()
{	
	m_rprApiSharedPtr = std::shared_ptr<HdRprApi>(new HdRprApi);
	m_rprApiSharedPtr->Init();
	g_HdRprDelegate = this;
}

HdRprDelegate::~HdRprDelegate()
{
	m_rprApiSharedPtr->Deinit();
	m_rprApiSharedPtr.reset();
	g_HdRprDelegate = nullptr;
}

HdRenderParam*
HdRprDelegate::GetRenderParam() const
{
	//return _renderParam.get();
	return nullptr;
}

void
HdRprDelegate::CommitResources(HdChangeTracker *tracker)
{
    // CommitResources() is called after prim sync has finished, but before any
    // tasks (such as draw tasks) have run. 

}

TfToken
HdRprDelegate::GetMaterialNetworkSelector() const {
	return HdRprTokens->rpr;
}

TfTokenVector const&
HdRprDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const&
HdRprDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const&
HdRprDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr
HdRprDelegate::GetResourceRegistry() const
{
	//return _resourceRegistry;
	return HdResourceRegistrySharedPtr(new HdResourceRegistry());
}

HdRenderPassSharedPtr
HdRprDelegate::CreateRenderPass(HdRenderIndex *index,
                            HdRprimCollection const& collection)
{
	//HdRprParam * param = (HdRprParam * ) GetRenderParam();
	return HdRenderPassSharedPtr(
		new HdRprRenderPass(index, collection, m_rprApiSharedPtr));
}

HdInstancer *
HdRprDelegate::CreateInstancer(HdSceneDelegate *delegate,
                                        SdfPath const& id,
                                        SdfPath const& instancerId)
{
	return new HdRprInstancer(delegate, id, instancerId);
}

void
HdRprDelegate::DestroyInstancer(HdInstancer *instancer)
{
}

HdRprim *
HdRprDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId,
                                    SdfPath const& instancerId)
{
	if (typeId == HdPrimTypeTokens->mesh) {
		return new HdRprMesh(rprimId, m_rprApiSharedPtr, instancerId);
	}
	else {
		TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());
	}
	return nullptr;
}

void
HdRprDelegate::DestroyRprim(HdRprim *rPrim)
{

}

HdSprim *
HdRprDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId)
{
	if (typeId == HdPrimTypeTokens->camera) {
		return new HdCamera(sprimId);
	}
	else if (typeId == HdPrimTypeTokens->domeLight)
	{
		return new HdRprDomeLight(sprimId, m_rprApiSharedPtr);
	}
	else if (typeId == HdPrimTypeTokens->rectLight)
	{
		return new HdRprRectLight(sprimId, m_rprApiSharedPtr);
	}
	else if (typeId == HdPrimTypeTokens->sphereLight)
	{
		return new HdRprSphereLight(sprimId, m_rprApiSharedPtr);
	}
	else if (typeId == HdPrimTypeTokens->material)
	{
		return new HdRprMaterial(sprimId, m_rprApiSharedPtr);
	}
	else {
		TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
	}

	return nullptr;
}

HdSprim *
HdRprDelegate::CreateFallbackSprim(TfToken const& typeId)
{
	
	// For fallback sprims, create objects with an empty scene path.
	// They'll use default values and won't be updated by a scene delegate.
	if (typeId == HdPrimTypeTokens->camera) {
		return new HdCamera(SdfPath::EmptyPath());
	}
	else if (typeId == HdPrimTypeTokens->domeLight)
	{
		return new HdRprDomeLight(SdfPath::EmptyPath(), m_rprApiSharedPtr);
	}
	else if (typeId == HdPrimTypeTokens->rectLight)
	{
		return new HdRprRectLight(SdfPath::EmptyPath(), m_rprApiSharedPtr);
	}
	else if (typeId == HdPrimTypeTokens->sphereLight)
	{
		return new HdRprSphereLight(SdfPath::EmptyPath(), m_rprApiSharedPtr);
	}
	else if (typeId == HdPrimTypeTokens->material)
	{
		return new HdRprMaterial(SdfPath::EmptyPath(), m_rprApiSharedPtr);
	}
	else {
		TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
	}
	return nullptr;
}

void
HdRprDelegate::DestroySprim(HdSprim *sPrim)
{
	delete sPrim;
}

HdBprim *
HdRprDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId)
{
	TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    return nullptr;
}

HdBprim *
HdRprDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    return nullptr;
}

void
HdRprDelegate::DestroyBprim(HdBprim *bPrim)
{
}

PXR_NAMESPACE_CLOSE_SCOPE


void SetRprGlobalRenderMode(int renderMode)
{
	switch (renderMode)
	{
	case 1:
		PXR_INTERNAL_NS::HdRprApi::SetRenderMode(PXR_INTERNAL_NS::HdRprRenderMode::DIRECT_ILLUMINATION);
		return;

		case 2:
			PXR_INTERNAL_NS::HdRprApi::SetRenderMode(PXR_INTERNAL_NS::HdRprRenderMode::DIRECT_ILLUMINATION_NO_SHADOW);
				return;

		case 3:
			PXR_INTERNAL_NS::HdRprApi::SetRenderMode(PXR_INTERNAL_NS::HdRprRenderMode::WIREFRAME);
			return;

		case 4:
			PXR_INTERNAL_NS::HdRprApi::SetRenderMode(PXR_INTERNAL_NS::HdRprRenderMode::MATERIAL_INDEX);
			return;

		case 5:
			PXR_INTERNAL_NS::HdRprApi::SetRenderMode(PXR_INTERNAL_NS::HdRprRenderMode::POSITION);
			return;

		case 6:
			PXR_INTERNAL_NS::HdRprApi::SetRenderMode(PXR_INTERNAL_NS::HdRprRenderMode::NORMAL);
			return;

		case 7:
			PXR_INTERNAL_NS::HdRprApi::SetRenderMode(PXR_INTERNAL_NS::HdRprRenderMode::TEXCOORD);
			return;

		case 8:
			PXR_INTERNAL_NS::HdRprApi::SetRenderMode(PXR_INTERNAL_NS::HdRprRenderMode::AMBIENT_OCCLUSION);
			return;

		case 9:
			PXR_INTERNAL_NS::HdRprApi::SetRenderMode(PXR_INTERNAL_NS::HdRprRenderMode::DIFFUSE);
			return;

			default:
				break;
	}
		
	PXR_INTERNAL_NS::HdRprApi::SetRenderMode(PXR_INTERNAL_NS::HdRprRenderMode::GLOBAL_ILLUMINATION);
	return;
}

void SetRprGlobalRenderDevice(int renderDevice)
{
	switch (renderDevice)
	{
	case 1:
		PXR_INTERNAL_NS::HdRprApi::SetRenderDevice(PXR_INTERNAL_NS::HdRprRenderDevice::GPU0);
		return;
	default:
		break;
	}

	PXR_INTERNAL_NS::HdRprApi::SetRenderDevice(PXR_INTERNAL_NS::HdRprRenderDevice::CPU);
}