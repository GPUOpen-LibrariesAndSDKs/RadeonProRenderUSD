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
    (progressive) \
    (RPR)
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
        return new HdRprRenderBuffer(bprimId, m_rprApi.get());
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
    auto rprStats = m_rprApi->GetRenderStats();

    VtDictionary stats;
    stats[_tokens->percentDone.GetString()] = rprStats.percentDone;
    stats["averageRenderTimePerSample"] = rprStats.averageRenderTimePerSample;
    stats["averageResolveTimePerSample"] = rprStats.averageResolveTimePerSample;
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

void HdRprDelegate::SetDrivers(HdDriverVector const& drivers) {
    for (HdDriver* hdDriver : drivers) {
        if (hdDriver->name == _tokens->RPR && hdDriver->driver.IsHolding<VtDictionary>()) {
            VtDictionary dictionary = hdDriver->driver.UncheckedGet<VtDictionary>();

            // Interop info is used to create context
            void* interopInfo = dictionary["interop_info"].Get<void*>();

            // Condition variable is used to prevent this issue:
            // [Plugin] Render_Frame_1 & Flush_Frame_1
            // [Plugin] Render_Frame_2 & Flush_Frame_2
            // [Client] Present frame
            // Hybrid correct usage prohibit flushing next frame before previous was presented
            // Render thread would wait on next flush till previous frame would be presented, example:
            // [Plugin] Render_Frame_1 & Flush_Frame_1
            // [Plugin] Render_Frame_2 & [Wait for present] <- Here frame wasn't presented yet
            // [Client] Present Frame_1
            // [Plugin] [Wake up] Flush Frame_2, continue work
            std::condition_variable* presentedConditionVariable = dictionary["presented_condition_variable"].Get<std::condition_variable*>();
            bool* presentedCondition = dictionary["presented_condition"].Get<bool*>();

            // Set condition to true to render first frame
            *presentedCondition = true;

            m_rprApi->SetInteropInfo(interopInfo, presentedConditionVariable, presentedCondition);
            break;
        }
    }
}

#endif // PXR_VERSION >= 2005

PXR_NAMESPACE_CLOSE_SCOPE

void HdRprSetRenderDevice(const char* renderDevice) {
    PXR_INTERNAL_NS::HdRprConfig* config;
    auto configInstanceLock = PXR_INTERNAL_NS::HdRprConfig::GetInstance(&config);
    config->SetRenderDevice(PXR_INTERNAL_NS::TfToken(renderDevice));
}

void HdRprSetRenderQuality(const char* quality) {
    PXR_INTERNAL_NS::HdRprConfig* config;
    auto configInstanceLock = PXR_INTERNAL_NS::HdRprConfig::GetInstance(&config);
    config->SetRenderQuality(PXR_INTERNAL_NS::TfToken(quality));
}

char* HdRprGetRenderQuality() {
    if (!PXR_INTERNAL_NS::g_rprApi) {
        return nullptr;
    }
    auto currentRenderQuality = PXR_INTERNAL_NS::g_rprApi->GetCurrentRenderQuality().GetText();

    auto len = std::strlen(currentRenderQuality);
    auto copy = (char*)malloc(len + 1);
    copy[len] = '\0';
    std::strncpy(copy, currentRenderQuality, len);
    return copy;
}

void HdRprFree(void* ptr) {
    free(ptr);
}
