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

#include "renderDelegate.h"
#include "aovDescriptor.h"

#include "pxr/imaging/rprUsd/materialRegistry.h"
#include "pxr/imaging/hd/extComputation.h"
#include "pxr/base/tf/diagnosticMgr.h"
#include "pxr/base/tf/getenv.h"

#include "camera.h"
#include "config.h"
#include "renderPass.h"
#include "renderParam.h"
#include "mesh.h"
#include "instancer.h"
#include "domeLight.h"
#include "distantLight.h"
#include "light.h"
#include "material.h"
#include "renderBuffer.h"
#include "basisCurves.h"
#include "points.h"

#ifdef USE_VOLUME
#include "volume.h"
#include "field.h"
#endif

#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdio>

PXR_NAMESPACE_OPEN_SCOPE

static HdRprApi* g_rprApi = nullptr;

class HdRprDiagnosticMgrDelegate : public TfDiagnosticMgr::Delegate {
public:
    explicit HdRprDiagnosticMgrDelegate(std::string const& logFile) : m_outputFile(nullptr) {
        if (logFile == "stderr") {
            m_output = stderr;
        } else if (logFile == "stdout") {
            m_output = stdout;
        } else {
            m_outputFile = fopen(logFile.c_str(), "a+");
            if (!m_outputFile) {
                TF_RUNTIME_ERROR("Failed to open error output file: \"%s\". Defaults to stderr\n", logFile.c_str());
                m_output = stderr;
            } else {
                m_output = m_outputFile;
            }
        }
    }
    ~HdRprDiagnosticMgrDelegate() override {
        if (m_outputFile) {
            fclose(m_outputFile);
        }
    }

    void IssueError(TfError const &err) override {
        IssueDiagnosticBase(err);
    };
    void IssueFatalError(TfCallContext const &context, std::string const &msg) override {
        std::string message = TfStringPrintf(
            "[FATAL ERROR] %s -- in %s at line %zu of %s",
            msg.c_str(),
            context.GetFunction(),
            context.GetLine(),
            context.GetFile());
        IssueMessage(msg);
    };
    void IssueStatus(TfStatus const &status) override {
        IssueDiagnosticBase(status);
    };
    void IssueWarning(TfWarning const &warning) override {
        IssueDiagnosticBase(warning);
    };

private:
    void IssueDiagnosticBase(TfDiagnosticBase const& d) {
        std::string msg = TfStringPrintf(
            "%s -- %s in %s at line %zu of %s",
            d.GetCommentary().c_str(),
            TfDiagnosticMgr::GetCodeName(d.GetDiagnosticCode()).c_str(),
            d.GetContext().GetFunction(),
            d.GetContext().GetLine(),
            d.GetContext().GetFile());
        IssueMessage(msg);
    }

    void IssueMessage(std::string const& message) {
        std::time_t t = std::time(nullptr);
        std::stringstream ss;
        ss << "[" << std::put_time(std::gmtime(&t), "%T%z %F") << "] " << message;
        auto str = ss.str();

        fprintf(m_output, "%s\n", str.c_str());
    }

private:
    FILE* m_output;
    FILE* m_outputFile;
};

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (openvdbAsset) \
    (percentDone) \
    (renderMode) \
    (batch) \
    (progressive)
);

const TfTokenVector HdRprDelegate::SUPPORTED_RPRIM_TYPES = {
    HdPrimTypeTokens->mesh,
    HdPrimTypeTokens->basisCurves,
    HdPrimTypeTokens->points,
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
    HdPrimTypeTokens->distantLight,
    HdPrimTypeTokens->extComputation
};

const TfTokenVector HdRprDelegate::SUPPORTED_BPRIM_TYPES = {
#ifdef USE_VOLUME
    _tokens->openvdbAsset,
#endif
    HdPrimTypeTokens->renderBuffer
};

HdRprDelegate::HdRprDelegate(HdRenderSettingsMap const& renderSettings) {
    for (auto& entry : renderSettings) {
        SetRenderSetting(entry.first, entry.second);
    }

    m_isBatch = GetRenderSetting(_tokens->renderMode) == _tokens->batch;
    m_isProgressive = GetRenderSetting(_tokens->progressive).GetWithDefault(true);

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

    auto errorOutputFile = TfGetenv("HD_RPR_ERROR_OUTPUT_FILE");
    if (!errorOutputFile.empty()) {
        m_diagnosticMgrDelegate = DiagnostMgrDelegatePtr(
            new HdRprDiagnosticMgrDelegate(errorOutputFile),
            [](HdRprDiagnosticMgrDelegate* delegate) {
                TfDiagnosticMgr::GetInstance().RemoveDelegate(delegate);
                delete delegate;
            });
        TfDiagnosticMgr::GetInstance().AddDelegate(m_diagnosticMgrDelegate.get());
    }
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
    m_rprApi->CommitResources();
}

TfToken HdRprDelegate::GetMaterialNetworkSelector() const {
    return RprUsdMaterialRegistry::GetInstance().GetMaterialNetworkSelector();
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
    } else if (typeId == HdPrimTypeTokens->points) {
        return new HdRprPoints(rprimId, instancerId);
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
        return new HdRprCamera(sprimId);
    } else if (typeId == HdPrimTypeTokens->domeLight) {
        return new HdRprDomeLight(sprimId);
    } else if (typeId == HdPrimTypeTokens->distantLight) {
        return new HdRprDistantLight(sprimId);
    } else if (typeId == HdPrimTypeTokens->rectLight ||
        typeId == HdPrimTypeTokens->sphereLight ||
        typeId == HdPrimTypeTokens->cylinderLight ||
        typeId == HdPrimTypeTokens->diskLight) {
        return new HdRprLight(sprimId, typeId);
    } else if (typeId == HdPrimTypeTokens->material) {
        return new HdRprMaterial(sprimId);
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(sprimId);
    }

    TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    return nullptr;
}

HdSprim* HdRprDelegate::CreateFallbackSprim(TfToken const& typeId) {
    // For fallback sprims, create objects with an empty scene path.
    // They'll use default values and won't be updated by a scene delegate.
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdRprCamera(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->domeLight) {
        return new HdRprDomeLight(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->rectLight ||
        typeId == HdPrimTypeTokens->sphereLight ||
        typeId == HdPrimTypeTokens->cylinderLight ||
        typeId == HdPrimTypeTokens->diskLight) {
        return new HdRprLight(SdfPath::EmptyPath(), typeId);
    } else if (typeId == HdPrimTypeTokens->distantLight) {
        return new HdRprDistantLight(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->material) {
        return new HdRprMaterial(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(SdfPath::EmptyPath());
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
    auto& rprAovDesc = HdRprAovRegistry::GetInstance().GetAovDesc(name);

    HdAovDescriptor hdAovDesc;
    hdAovDesc.format = rprAovDesc.format;
    hdAovDesc.multiSampled = rprAovDesc.multiSampled;
    hdAovDesc.clearValue = VtValue(rprAovDesc.clearValue);

    return hdAovDesc;
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

#if PXR_VERSION >= 2005

bool HdRprDelegate::IsStopSupported() const {
    return true;
}

bool HdRprDelegate::Stop() {
    m_renderThread.StopRender();
    return true;
}

bool HdRprDelegate::Restart() {
    m_renderParam->RestartRender();
    m_renderThread.StartRender();
    return true;
}

#endif // PXR_VERSION >= 2005

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

int HdRprExportRprSceneOnNextRender(const char* exportPath) {
    if (!PXR_INTERNAL_NS::g_rprApi) {
        return -1;
    }
    PXR_INTERNAL_NS::g_rprApi->ExportRprSceneOnNextRender(exportPath);
    return 0;
}
