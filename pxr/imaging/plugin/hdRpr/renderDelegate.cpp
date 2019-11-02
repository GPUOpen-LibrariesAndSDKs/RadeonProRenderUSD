#include "renderDelegate.h"

#include "pxr/imaging/hd/camera.h"

#include "config.h"
#include "renderPass.h"
#include "mesh.h"
#include "instancer.h"
#include "domeLight.h"
#include "sphereLight.h"
#include "cylinderLight.h"
#include "rectLight.h"
#include "material.h"
#include "renderBuffer.h"
#include "basisCurves.h"

#ifdef USE_VOLUME
#include "volume.h"
#include "field.h"
#endif


PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens,
	(openvdbAsset) \
	(rpr)
);

const TfTokenVector HdRprDelegate::SUPPORTED_RPRIM_TYPES =
{
	HdPrimTypeTokens->mesh,
	HdPrimTypeTokens->basisCurves,

#ifdef USE_VOLUME
	HdPrimTypeTokens->volume,
#endif

};

const TfTokenVector HdRprDelegate::SUPPORTED_SPRIM_TYPES =
{
	HdPrimTypeTokens->camera,
	HdPrimTypeTokens->material,
	HdPrimTypeTokens->rectLight,
    HdPrimTypeTokens->sphereLight,
    HdPrimTypeTokens->cylinderLight,
	HdPrimTypeTokens->domeLight,
};

const TfTokenVector HdRprDelegate::SUPPORTED_BPRIM_TYPES =
{
#ifdef USE_VOLUME
    _tokens->openvdbAsset,
#endif
    HdPrimTypeTokens->renderBuffer
};


HdRprDelegate::HdRprDelegate() {
    m_rprApiSharedPtr = std::shared_ptr<HdRprApi>(new HdRprApi);

    m_settingDescriptors = HdRprConfig::GetRenderSettingDescriptors();
    _PopulateDefaultSettings(m_settingDescriptors);
}

HdRprDelegate::~HdRprDelegate() {
    if (m_rprApiSharedPtr.use_count() > 1) {
        TF_CODING_ERROR("Leaked rprApi reference");
    }
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
	return _tokens->rpr;
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
	else if (typeId == HdPrimTypeTokens->basisCurves) {
		return new HdRprBasisCurves(rprimId, m_rprApiSharedPtr, instancerId);
	}
#ifdef USE_VOLUME
	else if (typeId == HdPrimTypeTokens->volume)
	{
		return new HdRprVolume(rprimId, m_rprApiSharedPtr);
	}
#endif
	else {
		TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());
	}
	return nullptr;
}

void
HdRprDelegate::DestroyRprim(HdRprim *rPrim)
{
	delete rPrim;
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
    else if (typeId == HdPrimTypeTokens->cylinderLight) {
        return new HdRprCylinderLight(sprimId, m_rprApiSharedPtr);
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
    else if (typeId == HdPrimTypeTokens->cylinderLight)
    {
        return new HdRprCylinderLight(SdfPath::EmptyPath(), m_rprApiSharedPtr);
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
	if(typeId == HdPrimTypeTokens->renderBuffer)
    {
        return new HdRprRenderBuffer(bprimId, m_rprApiSharedPtr);
    }

#ifdef USE_VOLUME
	if (typeId == _tokens->openvdbAsset)
	{
		return new HdRprField(bprimId, m_rprApiSharedPtr);
	}
#endif

	TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
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
	delete bPrim;
}

HdAovDescriptor HdRprDelegate::GetDefaultAovDescriptor(TfToken const& name) const {
    HdParsedAovToken aovId(name);
    if (name != HdAovTokens->color &&
        name != HdAovTokens->normal &&
        name != HdAovTokens->primId &&
        name != HdAovTokens->depth &&
        name != HdAovTokens->linearDepth &&
        !(aovId.isPrimvar && aovId.name == "st")) {
        // TODO: implement support for instanceId and elementId aov
        return HdAovDescriptor();
    }

    HdFormat format = HdFormatInvalid;

    float clearColorValue = 0.0f;
    if (name == HdAovTokens->depth ||
        name == HdAovTokens->linearDepth) {
        clearColorValue = name == HdAovTokens->linearDepth ? 0.0f : 1.0f;
        format = HdFormatFloat32;
    } else if (name == HdAovTokens->color) {
        format = HdFormatFloat32Vec4;
    } else if (name == HdAovTokens->primId) {
        format = HdFormatInt32;
    } else {
        format = HdFormatFloat32Vec3;
    }

    return HdAovDescriptor(format, false, VtValue(GfVec4f(clearColorValue)));
}

HdRenderSettingDescriptorList HdRprDelegate::GetRenderSettingDescriptors() const {
    return m_settingDescriptors;
}

PXR_NAMESPACE_CLOSE_SCOPE

void SetHdRprRenderDevice(int renderDevice) {
    using namespace PXR_INTERNAL_NS;

    if (renderDevice == 0) {
        HdRprConfig::GetInstance().SetRenderDevice(rpr::RenderDeviceType::CPU);
    } else if (renderDevice == 1) {
        HdRprConfig::GetInstance().SetRenderDevice(rpr::RenderDeviceType::GPU);
    } else {
        TF_WARN("Invalid parameter: renderDevice = %d", renderDevice);
    }
}

void SetHdRprRenderQuality(int quality) {
    using namespace PXR_INTERNAL_NS;

    if (quality >= 0 && quality <= 2) {
        HdRprConfig::GetInstance().SetPlugin(rpr::PluginType::HYBRID);
        HdRprConfig::GetInstance().SetHybridQuality(static_cast<HdRprHybridQuality>(quality));
    } else if (quality == 3) {
        HdRprConfig::GetInstance().SetPlugin(rpr::PluginType::TAHOE);
    } else {
        TF_WARN("Invalid parameter: quality = %d", quality);
    }
}
