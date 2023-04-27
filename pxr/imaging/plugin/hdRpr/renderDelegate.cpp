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
#include "pxr/imaging/hgi/tokens.h"
#include "pxr/base/tf/diagnosticMgr.h"
#include "pxr/base/tf/getenv.h"
#include "pxr/base/arch/env.h"

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
    (RPR) \
    (mtlx)
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

HdRprDelegate* HdRprDelegate::m_lastCreatedInstance = nullptr;

HdRprDelegate::HdRprDelegate(HdRenderSettingsMap const& renderSettings) {
    for (auto& entry : renderSettings) {
        SetRenderSetting(entry.first, entry.second);
    }

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

    m_lastCreatedInstance = this;
}

HdRprDelegate::~HdRprDelegate() {
    g_rprApi = nullptr;
    m_lastCreatedInstance = nullptr;
}

HdRenderParam* HdRprDelegate::GetRenderParam() const {
    return m_renderParam.get();
}

void HdRprDelegate::CommitResources(HdChangeTracker* tracker) {
    // CommitResources() is called after prim sync has finished, but before any
    // tasks (such as draw tasks) have run.
    m_rprApi->CommitResources();
}

TfTokenVector HdRprDelegate::GetMaterialRenderContexts() const {
    return {RprUsdMaterialRegistry::GetInstance().GetMaterialNetworkSelector(), _tokens->mtlx};
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
                                            SdfPath const& id
                                            HDRPR_INSTANCER_ID_ARG_DECL) {
    return new HdRprInstancer(delegate, id HDRPR_INSTANCER_ID_ARG);
}

void HdRprDelegate::DestroyInstancer(HdInstancer* instancer) {
    delete instancer;
}

HdRprim* HdRprDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId
                                    HDRPR_INSTANCER_ID_ARG_DECL) {
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdRprMesh(rprimId HDRPR_INSTANCER_ID_ARG);
    } else if (typeId == HdPrimTypeTokens->basisCurves) {
        return new HdRprBasisCurves(rprimId HDRPR_INSTANCER_ID_ARG);
    } else if (typeId == HdPrimTypeTokens->points) {
        return new HdRprPoints(rprimId HDRPR_INSTANCER_ID_ARG);
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

std::string join(const std::vector<std::string>& vec, const std::string& delim)
{
    std::stringstream res;
    copy(vec.begin(), vec.end(), std::ostream_iterator<std::string>(res, delim.c_str()));
    return res.str();
}

VtDictionary HdRprDelegate::GetRenderStats() const {
    auto rprStats = m_rprApi->GetRenderStats();

    VtDictionary stats;
    stats[_tokens->percentDone.GetString()] = rprStats.percentDone;
    stats["averageRenderTimePerSample"] = rprStats.averageRenderTimePerSample;
    stats["averageResolveTimePerSample"] = rprStats.averageResolveTimePerSample;

    stats["gpuUsedNames"] = join(m_rprApi->GetGpuUsedNames(), ", ");
    stats["threadCountUsed"] = m_rprApi->GetCpuThreadCountUsed();

    stats["firstIterationRenderTime"] = m_rprApi->GetFirstIterationRenerTime();

    stats["totalRenderTime"] = rprStats.totalRenderTime;
    stats["frameRenderTotalTime"] = rprStats.frameRenderTotalTime;
    stats["frameResolveTotalTime"] = rprStats.frameResolveTotalTime;
    
    stats["cacheCreationTime"] = rprStats.cacheCreationTime;
    stats["syncTime"] = rprStats.syncTime;

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

#if PXR_VERSION >= 2203
bool HdRprDelegate::Stop(bool blocking) {
#else
bool HdRprDelegate::Stop() {
#endif
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
#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
        if (hdDriver->name == HgiTokens->Vulkan && hdDriver->driver.IsHolding<VtDictionary>()) {
            VtDictionary dictionary = hdDriver->driver.UncheckedGet<VtDictionary>();

            // Interop info is used to create context
            static VkInteropInfo::VkInstance vkInstance;
            static VkInteropInfo vkInteropInfo;
            vkInteropInfo.instance_count = 1;
            vkInteropInfo.main_instance_index = 0;
            vkInteropInfo.frames_in_flight = dictionary["interop_info_frames_in_flight"].Get<unsigned int>();
            vkInteropInfo.framebuffers_release_semaphores = dictionary["interop_info_semaphore_array"].Get<void*>();
            vkInteropInfo.instances = &vkInstance;
            vkInteropInfo.instances->physical_device = dictionary["interop_info_physicalDevice"].Get<void*>();
            vkInteropInfo.instances->device = dictionary["interop_info_device"].Get<void*>();

            m_rprApi->SetInteropInfo(&vkInteropInfo);
            break;
        }
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
    }
}

#endif // PXR_VERSION >= 2005

PXR_NAMESPACE_CLOSE_SCOPE

void HdRprSetRenderQuality(const char* quality) {
    // Set environment variable that would outlive render delegate dll, because config is destroyed on plugin reload
    // HDRPR_RENDER_QUALITY_OVERRIDE env setting wasn't used because it seems not possible to declare it in .h file
    PXR_INTERNAL_NS::ArchSetEnv("HDRPR_USDVIEW_RENDER_QUALITY", quality, true);
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
