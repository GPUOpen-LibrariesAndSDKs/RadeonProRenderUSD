#include "renderDelegate.h"

#include "pxr/imaging/hd/camera.h"

#include "config.h"
#include "renderPass.h"
#include "renderParam.h"
#include "mesh.h"
#include "instancer.h"
#include "domeLight.h"
#include "sphereLight.h"
#include "cylinderLight.h"
#include "rectLight.h"
#include "diskLight.h"
#include "distantLight.h"
#include "material.h"
#include "renderBuffer.h"
#include "basisCurves.h"

#ifdef USE_VOLUME
#include "volume.h"
#include "field.h"
#endif

PXR_NAMESPACE_OPEN_SCOPE

static HdRprApi* g_rprApi = nullptr;

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (openvdbAsset) \
    (rpr) \
    (percentDone)
);

const TfTokenVector HdRprDelegate::SUPPORTED_RPRIM_TYPES = {
    HdPrimTypeTokens->mesh,
    HdPrimTypeTokens->basisCurves,

#ifdef USE_VOLUME
    HdPrimTypeTokens->volume,
#endif
};

const TfTokenVector HdRprDelegate::SUPPORTED_SPRIM_TYPES = {
    HdPrimTypeTokens->camera,
    HdPrimTypeTokens->material,
    HdPrimTypeTokens->rectLight,
    HdPrimTypeTokens->sphereLight,
    HdPrimTypeTokens->cylinderLight,
    HdPrimTypeTokens->domeLight,
    HdPrimTypeTokens->diskLight,
    HdPrimTypeTokens->distantLight
};

const TfTokenVector HdRprDelegate::SUPPORTED_BPRIM_TYPES = {
#ifdef USE_VOLUME
    _tokens->openvdbAsset,
#endif
    HdPrimTypeTokens->renderBuffer
};

HdRprDelegate::HdRprDelegate() {
    m_rprApi.reset(new HdRprApi(this));
    g_rprApi = m_rprApi.get();

    m_renderParam.reset(new HdRprRenderParam(m_rprApi.get(), &m_renderThread));

    m_settingDescriptors = HdRprConfig::GetRenderSettingDescriptors();
    _PopulateDefaultSettings(m_settingDescriptors);

    m_renderThread.SetRenderCallback([this]() {
        m_rprApi->Render(&m_renderThread);
    });
    m_renderThread.SetStopCallback([this]() {
        m_rprApi->AbortRender();
    });
    m_renderThread.StartThread();
}

HdRprDelegate::~HdRprDelegate() {
    g_rprApi = nullptr;
}

HdRenderParam* HdRprDelegate::GetRenderParam() const {
    return m_renderParam.get();
}

void HdRprDelegate::CommitResources(HdChangeTracker* tracker) {
    // CommitResources() is called after prim sync has finished, but before any
    // tasks (such as draw tasks) have run.

}

TfToken HdRprDelegate::GetMaterialNetworkSelector() const {
    return _tokens->rpr;
}

TfTokenVector const& HdRprDelegate::GetSupportedRprimTypes() const {
    return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const& HdRprDelegate::GetSupportedSprimTypes() const {
    return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const& HdRprDelegate::GetSupportedBprimTypes() const {
    return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr HdRprDelegate::GetResourceRegistry() const {
    return HdResourceRegistrySharedPtr(new HdResourceRegistry());
}

HdRenderPassSharedPtr HdRprDelegate::CreateRenderPass(HdRenderIndex* index,
                                                      HdRprimCollection const& collection) {
    return HdRenderPassSharedPtr(new HdRprRenderPass(index, collection, m_renderParam.get()));
}

HdInstancer* HdRprDelegate::CreateInstancer(HdSceneDelegate* delegate,
                                            SdfPath const& id,
                                            SdfPath const& instancerId) {
    return new HdRprInstancer(delegate, id, instancerId);
}

void HdRprDelegate::DestroyInstancer(HdInstancer* instancer) {
    delete instancer;
}

HdRprim* HdRprDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId,
                                    SdfPath const& instancerId) {
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdRprMesh(rprimId, instancerId);
    } else if (typeId == HdPrimTypeTokens->basisCurves) {
        return new HdRprBasisCurves(rprimId, instancerId);
    }
#ifdef USE_VOLUME
    else if (typeId == HdPrimTypeTokens->volume) {
        return new HdRprVolume(rprimId);
    }
#endif

    TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());
    return nullptr;
}

void HdRprDelegate::DestroyRprim(HdRprim* rPrim) {
    delete rPrim;
}

HdSprim* HdRprDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId) {
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(sprimId);
    } else if (typeId == HdPrimTypeTokens->domeLight) {
        return new HdRprDomeLight(sprimId);
    } else if (typeId == HdPrimTypeTokens->rectLight) {
        return new HdRprRectLight(sprimId);
    } else if (typeId == HdPrimTypeTokens->sphereLight) {
        return new HdRprSphereLight(sprimId);
    } else if (typeId == HdPrimTypeTokens->cylinderLight) {
        return new HdRprCylinderLight(sprimId);
    } else if (typeId == HdPrimTypeTokens->distantLight) {
        return new HdRprDistantLight(sprimId);
    } else if (typeId == HdPrimTypeTokens->diskLight) {
        return new HdRprDiskLight(sprimId);
    } else if (typeId == HdPrimTypeTokens->material) {
        return new HdRprMaterial(sprimId);
    }

    TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    return nullptr;
}

HdSprim* HdRprDelegate::CreateFallbackSprim(TfToken const& typeId) {
    // For fallback sprims, create objects with an empty scene path.
    // They'll use default values and won't be updated by a scene delegate.
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->domeLight) {
        return new HdRprDomeLight(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->rectLight) {
        return new HdRprRectLight(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->sphereLight) {
        return new HdRprSphereLight(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->cylinderLight) {
        return new HdRprCylinderLight(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->diskLight) {
        return new HdRprDiskLight(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->distantLight) {
        return new HdRprDistantLight(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->material) {
        return new HdRprMaterial(SdfPath::EmptyPath());
    }

    TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    return nullptr;
}

void HdRprDelegate::DestroySprim(HdSprim* sPrim) {
    delete sPrim;
}

HdBprim* HdRprDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId) {
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdRprRenderBuffer(bprimId);
    }
#ifdef USE_VOLUME
    else if (typeId == _tokens->openvdbAsset) {
        return new HdRprField(bprimId);
    }
#endif

    TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    return nullptr;
}

HdBprim* HdRprDelegate::CreateFallbackBprim(TfToken const& typeId) {
    return nullptr;
}

void HdRprDelegate::DestroyBprim(HdBprim* bPrim) {
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

    if (!m_rprApi->IsAovFormatConversionAvailable()) {
        if (name == HdAovTokens->primId) {
            // Integer images required, no way to support it
            return HdAovDescriptor();
        }
        // Only native RPR format can be used for AOVs when there is no support for AOV format conversion
        return HdAovDescriptor(HdFormatFloat32Vec4, false, VtValue(GfVec4f(0.0f)));
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

VtDictionary HdRprDelegate::GetRenderStats() const {
    VtDictionary stats;
    int numCompletedSamples = m_rprApi->GetNumCompletedSamples();
    stats[HdPerfTokens->numCompletedSamples.GetString()] = numCompletedSamples;

    double percentDone = 0.0;
    {
        HdRprConfig* config;
        auto configInstanceLock = HdRprConfig::GetInstance(&config);
        percentDone = double(numCompletedSamples) / config->GetMaxSamples();
    }
    int numActivePixels = m_rprApi->GetNumActivePixels();
    if (numActivePixels != -1) {
        auto size = m_rprApi->GetViewportSize();
        int numPixels = size[0] * size[1];
        percentDone = std::max(percentDone, double(numPixels - numActivePixels) / numPixels);
    }
    stats[_tokens->percentDone.GetString()] = 100.0 * percentDone;
    return stats;
}

bool HdRprDelegate::IsPauseSupported() const {
    return true;
}

bool HdRprDelegate::Pause() {
    m_renderThread.PauseRender();
    return true;
}

bool HdRprDelegate::Resume() {
    m_renderThread.ResumeRender();
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE

void SetHdRprRenderDevice(int renderDevice) {
    PXR_INTERNAL_NS::HdRprConfig* config;
    auto configInstanceLock = PXR_INTERNAL_NS::HdRprConfig::GetInstance(&config);
    config->SetRenderDevice(renderDevice);
}

void SetHdRprRenderQuality(int quality) {
    PXR_INTERNAL_NS::HdRprConfig* config;
    auto configInstanceLock = PXR_INTERNAL_NS::HdRprConfig::GetInstance(&config);
    config->SetRenderQuality(quality);
}

int GetHdRprRenderQuality() {
    if (!PXR_INTERNAL_NS::g_rprApi) {
        return -1;
    }
    return PXR_INTERNAL_NS::g_rprApi->GetCurrentRenderQuality();
}
