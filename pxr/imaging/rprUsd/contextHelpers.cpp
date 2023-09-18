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

#include <json.hpp>
using json = nlohmann::json;

#include "pxr/imaging/rprUsd/contextHelpers.h"
#include "pxr/imaging/rprUsd/contextMetadata.h"
#include "pxr/imaging/rprUsd/debugCodes.h"
#include "pxr/imaging/rprUsd/helpers.h"
#include "pxr/imaging/rprUsd/config.h"
#include "pxr/imaging/rprUsd/error.h"
#include "pxr/imaging/rprUsd/util.h"

#include "pxr/base/arch/env.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/envSetting.h"

#include <RadeonProRender.hpp>

#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
#include <RadeonProRender_VK.h>
#include <RadeonProRender_Baikal.h>
#include <vulkan/vulkan.h>
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <dlfcn.h>
#elif defined(__linux__)
#include <link.h>
#endif // __APPLE__

#include <fstream>
#include <thread>
#include <map>

#define PRINT_CONTEXT_CREATION_DEBUG_INFO(format, ...) \
    if (!TfDebug::IsEnabled(RPR_USD_DEBUG_CORE_UNSUPPORTED_ERROR)) /* empty */; else TfDebug::Helper().Msg(format, ##__VA_ARGS__)

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(RPRUSD_ENABLE_TRACING, false, "Enable tracing of RPR core");
TF_DEFINE_ENV_SETTING(RPRUSD_TRACING_DIR, "", "Where to store RPR core tracing files. Must be a path to valid directory");
TF_DEFINE_ENV_SETTING(RPRUSD_CPU_ONLY, false,
    "Disable RIF API and GPU context creation.  This will allow running on CPU only machines, but some AOV will no longer work");

namespace {

#if defined __APPLE__
const char* k_RadeonProRenderLibName = "libRadeonProRender64.dylib";
#elif defined(__linux__)
const char* k_RadeonProRenderLibName = "libRadeonProRender64.so";
#endif

std::string GetRprSdkPath() {
#ifdef __APPLE__
    uint32_t count = _dyld_image_count();
    std::string pathToRpr;
    for (uint32_t i = 0; i < count; ++i) {
        const mach_header* header = _dyld_get_image_header(i);
        if (!header) { break; }
        char* code_ptr = NULL;
        uint64_t size;
        code_ptr = getsectdatafromheader_64((const mach_header_64*)header, SEG_TEXT, SECT_TEXT, &size);
        if (!code_ptr) { continue; }
        const uintptr_t slide = _dyld_get_image_vmaddr_slide(i);
        const uintptr_t start = (const uintptr_t)code_ptr + slide;
        Dl_info info;
        if (dladdr((const void*)start, &info)) {
            std::string dlpath(info.dli_fname);
            std::size_t found = dlpath.find(k_RadeonProRenderLibName);
            if (found != std::string::npos)
            {
                return dlpath.substr(0, found);
            }
        }
    }

    PRINT_CONTEXT_CREATION_DEBUG_INFO("Path to RPR SDK with %s not found", k_RadeonProRenderLibName);
#elif defined(__linux__)
    if (auto handle = dlopen(nullptr, RTLD_NOW)) {
        link_map* map = nullptr;
        if (dlinfo(handle, RTLD_DI_LINKMAP, &map)) {
            const char* errorStr = "unknown reason";
            if (auto error = dlerror()) {
                errorStr = error;
            }
            PRINT_CONTEXT_CREATION_DEBUG_INFO("Failed to query RPR SDK path: %s", errorStr);
        } else {
            for (auto head = map; head != nullptr; head = head->l_next) {
                if (auto dlpath = std::strstr(head->l_name, k_RadeonProRenderLibName)) {
                    return std::string(head->l_name, dlpath - head->l_name);
                }
            }
        }
    }
#endif // __APPLE__

    return std::string();
}

void SetupRprTracing() {
    if (RprUsdIsTracingEnabled()) {
        auto tracingDir = TfGetEnvSetting(RPRUSD_TRACING_DIR);
        if (!tracingDir.empty()) {
            printf("RPR tracing directory: %s\n", tracingDir.c_str());
        }
        RPR_ERROR_CHECK(rprContextSetParameterByKeyString(nullptr, RPR_CONTEXT_TRACING_PATH, tracingDir.c_str()), "Failed to set tracing directory parameter");
        RPR_ERROR_CHECK(rprContextSetParameterByKey1u(nullptr, RPR_CONTEXT_TRACING_ENABLED, 1), "Failed to set context tracing parameter");
    }
}

const rpr::CreationFlags kGpuCreationFlags[] = {
    RPR_CREATION_FLAGS_ENABLE_GPU0,
    RPR_CREATION_FLAGS_ENABLE_GPU1,
    RPR_CREATION_FLAGS_ENABLE_GPU2,
    RPR_CREATION_FLAGS_ENABLE_GPU3,
    RPR_CREATION_FLAGS_ENABLE_GPU4,
    RPR_CREATION_FLAGS_ENABLE_GPU5,
    RPR_CREATION_FLAGS_ENABLE_GPU6,
    RPR_CREATION_FLAGS_ENABLE_GPU7,
    RPR_CREATION_FLAGS_ENABLE_GPU8,
    RPR_CREATION_FLAGS_ENABLE_GPU9,
    RPR_CREATION_FLAGS_ENABLE_GPU10,
    RPR_CREATION_FLAGS_ENABLE_GPU11,
    RPR_CREATION_FLAGS_ENABLE_GPU12,
    RPR_CREATION_FLAGS_ENABLE_GPU13,
    RPR_CREATION_FLAGS_ENABLE_GPU14,
    RPR_CREATION_FLAGS_ENABLE_GPU15,
};
const int kMaxNumGpus = sizeof(kGpuCreationFlags) / sizeof(kGpuCreationFlags[0]);

const std::map<RprUsdPluginType, const char*> kPluginLibNames = {
#ifdef WIN32
    {kPluginNorthstar, "Northstar64.dll"},
    {kPluginHybrid, "Hybrid.dll"},
    {kPluginHybridPro, "HybridPro.dll"},
#elif defined __linux__
    {kPluginNorthstar, "libNorthstar64.so"},
    {kPluginHybrid, "Hybrid.so"},
#elif defined __APPLE__
    {kPluginNorthstar, "libNorthstar64.dylib"},
#endif
};

rpr_int GetPluginID(RprUsdPluginType pluginType) {
    auto pluginLibNameIter = kPluginLibNames.find(pluginType);
    if (pluginLibNameIter == kPluginLibNames.end()) {
        TF_RUNTIME_ERROR("Plugin is not supported: %d", pluginType);
        return -1;
    }
    auto pluginLibName = pluginLibNameIter->second;

    const std::string rprSdkPath = GetRprSdkPath();
    const std::string pluginPath = rprSdkPath.empty() ? pluginLibName : rprSdkPath + "/" + pluginLibName;
    rpr_int pluginID = rprRegisterPlugin(pluginPath.c_str());
    if (pluginID == -1) {
        TF_RUNTIME_ERROR("Failed to register %s plugin located at \"%s\"", pluginLibName, pluginPath.c_str());
        return -1;
    }

    return pluginID;
}

std::string GetGpuName(RprUsdPluginType pluginType, rpr_int pluginID, rpr::CreationFlags creationFlag, rpr::ContextInfo gpuNameId, const char* cachePath) {
    rpr::CreationFlags additionalFlags = 0x0;

#if defined(__APPLE__)
    additionalFlags |= RPR_CREATION_FLAGS_ENABLE_METAL;
#endif

    std::vector<rpr_context_properties> properties;

#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
    // This fix is required since for 4GB VRAM default Hybrid allocation might be too big
    std::size_t allocation = 1024 * 1024; // 1 MB for each buffer

    if (RprUsdIsHybrid(pluginType)) {
        for (const std::size_t property: {
            RPR_CONTEXT_CREATEPROP_HYBRID_ACC_MEMORY_SIZE,
            RPR_CONTEXT_CREATEPROP_HYBRID_MESH_MEMORY_SIZE,
            RPR_CONTEXT_CREATEPROP_HYBRID_STAGING_MEMORY_SIZE,
            RPR_CONTEXT_CREATEPROP_HYBRID_SCRATCH_MEMORY_SIZE })
        {
            properties.push_back((rpr_context_properties)property);
            properties.push_back((rpr_context_properties)const_cast<void*>(reinterpret_cast<const void*>(&allocation)));
        }
    }
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT

    properties.push_back(nullptr);

    try {
        rpr::Status status;
        std::unique_ptr<rpr::Context> context(rpr::Context::Create(RPR_API_VERSION, &pluginID, 1, creationFlag | additionalFlags, properties.data(), cachePath, &status));
        if (context) {
            return RprUsdGetStringInfo(context.get(), gpuNameId);
        }
    } catch (RprUsdError& e) {
        PRINT_CONTEXT_CREATION_DEBUG_INFO("Failed to get gpu name: %s", e.what());
    }
    
    return {};
}

template <typename Func>
void ForEachGpu(RprUsdPluginType pluginType, rpr_int pluginID, const char* cachePath, Func&& func) {
#define GPU_ACTION(index) \
    do { \
        rpr::CreationFlags gpuFlag = RPR_CREATION_FLAGS_ENABLE_GPU ## index; \
        std::string name = GetGpuName(pluginType, pluginID, gpuFlag, RPR_CONTEXT_GPU ## index ## _NAME, cachePath); \
        func(index, gpuFlag, name); \
    } while (0);

    GPU_ACTION(0);
    GPU_ACTION(1);
    GPU_ACTION(2);
    GPU_ACTION(3);
    GPU_ACTION(4);
    GPU_ACTION(5);
    GPU_ACTION(6);
    GPU_ACTION(7);
    GPU_ACTION(8);
    GPU_ACTION(9);
    GPU_ACTION(10);
    GPU_ACTION(11);
    GPU_ACTION(12);
    GPU_ACTION(13);
    GPU_ACTION(14);
    GPU_ACTION(15);

#undef GPU_ACTION
}

RprUsdDevicesInfo LoadDevicesConfiguration(RprUsdPluginType pluginType, std::string const& deviceConfigurationFilepath) {
    RprUsdDevicesInfo devicesInfo = RprUsdGetDevicesInfo(pluginType);
    if (!devicesInfo.IsValid()) {
        return {};
    }

    RprUsdDevicesInfo ret;

    auto isLoaded = [&]() {
        try {
            std::ifstream configFile(deviceConfigurationFilepath);
            if (!configFile.is_open()) {
                return false;
            }

            json devicesConfig;
            configFile >> devicesConfig;

            auto pluginDevicesConfigIt = std::find_if(devicesConfig.begin(), devicesConfig.end(),
                [pluginType](json& entry) {
                    bool foundIt;
                    RprUsdPluginType entryPluginType = TfEnum::GetValueFromName<RprUsdPluginType>(entry["plugin_type"], &foundIt);
                    return foundIt && entryPluginType == pluginType;
                }
            );
            if (pluginDevicesConfigIt == devicesConfig.end()) {
                return false;
            }

            auto& pluginDevicesConfig = *pluginDevicesConfigIt;

            auto& cpuConfig = pluginDevicesConfig.at("cpu_config");

            auto& cpuInfo = cpuConfig.at("cpu_info");
            if (devicesInfo.cpu.numThreads != cpuInfo.at("num_threads").get<int>()) {
                return false;
            }

            cpuConfig.at("num_active_threads").get_to(ret.cpu.numThreads);

            for (auto& gpuConfig : pluginDevicesConfig.at("gpu_configs")) {
                auto& configGpuInfo = gpuConfig.at("gpu_info");
                RprUsdDevicesInfo::GPU gpuInfo(configGpuInfo.at("index"), configGpuInfo.at("name"));
                if (std::find(devicesInfo.gpus.begin(), devicesInfo.gpus.end(), gpuInfo) == devicesInfo.gpus.end()) {
                    return false;
                }

                if (gpuConfig.at("is_enabled").get<bool>()) {
                    ret.gpus.push_back(gpuInfo);
                }
            }

            return true;
        } catch (std::exception& e) {
            TF_RUNTIME_ERROR("Error on loading devices configurations: %s", e.what());
            return false;
        }
    }();

    if (!isLoaded ||
        (ret.cpu.numThreads == 0 && ret.gpus.empty())) {
        // Setup default configuration: either use the first GPU or, if it is not available, CPU
        ret = {};
        if (devicesInfo.gpus.empty()) {
            ret.cpu.numThreads = devicesInfo.cpu.numThreads;
        } else {
            ret.gpus.push_back(devicesInfo.gpus.front());
        }
    }

    return ret;
}

} // namespace anonymous

rpr::Context* RprUsdCreateContext(RprUsdContextMetadata* metadata) {
    SetupRprTracing();

    std::string cachePath;
    std::string textureCachePath;
    std::string deviceConfigurationFilepath;
    std::string precompiledKernelsPath;

    {
        RprUsdConfig* config;
        auto configLock = RprUsdConfig::GetInstance(&config);
        cachePath = config->GetKernelCacheDir();
        textureCachePath = config->GetTextureCacheDir();
        deviceConfigurationFilepath = config->GetDeviceConfigurationFilepath();
        precompiledKernelsPath = config->GetPrecompiledKernelDir();
    }

    rpr_int pluginID = GetPluginID(metadata->pluginType);
    if (pluginID == -1) {
        return nullptr;
    }

    RprUsdDevicesInfo devicesConfiguration = LoadDevicesConfiguration(metadata->pluginType, deviceConfigurationFilepath);

    std::vector<rpr_context_properties> contextProperties;
    auto appendContextProperty = [&contextProperties](uint64_t propertyKey, void* propertyValue) {
        contextProperties.push_back((rpr_context_properties)propertyKey);
        contextProperties.push_back((rpr_context_properties)propertyValue);
    };

    rpr::CreationFlags creationFlags = 0;
    for (RprUsdDevicesInfo::GPU gpu : devicesConfiguration.gpus) {
        if (gpu.index >= 0 && gpu.index < kMaxNumGpus) {
            creationFlags |= kGpuCreationFlags[gpu.index];
        }
    }
    if (devicesConfiguration.cpu.numThreads > 0) {
        creationFlags |= RPR_CREATION_FLAGS_ENABLE_CPU;
        appendContextProperty(RPR_CONTEXT_CPU_THREAD_LIMIT, (void*)(size_t)devicesConfiguration.cpu.numThreads);
    }

    if (creationFlags == 0) {
        return nullptr;
    }

#if __APPLE__
    if ((creationFlags & RPR_CREATION_FLAGS_ENABLE_CPU) == 0) {
        creationFlags |= RPR_CREATION_FLAGS_ENABLE_METAL;
    }
#endif

    if (metadata->isGlInteropEnabled) {
        if ((creationFlags & RPR_CREATION_FLAGS_ENABLE_CPU) ||
            RprUsdIsHybrid(metadata->pluginType)) {
            PRINT_CONTEXT_CREATION_DEBUG_INFO("GL interop could not be used with CPU rendering or Hybrid plugin");
            metadata->isGlInteropEnabled = false;
        } else if (!RprUsdInitGLApi()) {
            PRINT_CONTEXT_CREATION_DEBUG_INFO("Failed to init GL API. Disabling GL interop");
            metadata->isGlInteropEnabled = false;
        }
    }

    if (metadata->isGlInteropEnabled) {
        creationFlags |= RPR_CREATION_FLAGS_ENABLE_GL_INTEROP;
    }

    // set up HIP/CUDA support
    if (metadata->pluginType == kPluginNorthstar) {
        if (metadata->useOpenCL) {
            creationFlags |= RPR_CREATION_FLAGS_ENABLE_OPENCL;
        }
        else {
            appendContextProperty(RPR_CONTEXT_PRECOMPILED_BINARY_PATH, (void*)precompiledKernelsPath.c_str());
        }
    }

#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
    if (RprUsdIsHybrid(metadata->pluginType) && metadata->interopInfo) {
        appendContextProperty(RPR_CONTEXT_CREATEPROP_VK_INTEROP_INFO, metadata->interopInfo);
    }
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT

    for (const auto& entry: metadata->additionalIntProperties) {
        appendContextProperty(entry.first, const_cast<void*>(reinterpret_cast<const void*>(&entry.second)));
    }

    contextProperties.push_back(nullptr);

    rpr::Status status;
    rpr::Context* context = rpr::Context::Create(RPR_API_VERSION, &pluginID, 1, creationFlags, contextProperties.data(), cachePath.c_str(), &status);

    if (context) {
        if (RPR_ERROR_CHECK(context->SetActivePlugin(pluginID), "Failed to set active plugin")) {
            delete context;
            return nullptr;
        }

        if (metadata->pluginType == kPluginHybridPro) {
            std::string pluginName = "Hybrid";
            status = rprContextSetInternalParameterBuffer(rpr::GetRprObject(context), pluginID, "plugin.name", pluginName.c_str(), pluginName.size() + 1);
        }

        RPR_ERROR_CHECK(context->SetParameter(RPR_CONTEXT_TEXTURE_CACHE_PATH, textureCachePath.c_str()), "Failed to set texture cache path");

        metadata->creationFlags = creationFlags;
        metadata->devicesActuallyUsed = devicesConfiguration;
    } else {
        RPR_ERROR_CHECK(status, "Failed to create RPR context");
    }

    return context;
}

RprUsdDevicesInfo RprUsdGetDevicesInfo(RprUsdPluginType pluginType) {
    rpr_int pluginID = GetPluginID(pluginType);
    if (pluginID == -1) {
        return {};
    }

    std::string cachePath;
    {
        RprUsdConfig* config;
        auto configLock = RprUsdConfig::GetInstance(&config);
        cachePath = config->GetKernelCacheDir();
    }

    RprUsdDevicesInfo ret = {};

    if (RprUsdIsHybrid(pluginType)) {
        ret.cpu.numThreads = 0;

        if (!RprUsdIsCpuOnly()) {
            std::string name = GetGpuName(pluginType, pluginID, RPR_CREATION_FLAGS_ENABLE_GPU0, RPR_CONTEXT_GPU0_NAME, cachePath.c_str());
            if (!name.empty()) {
                ret.gpus.push_back({ 0, name });
            }
        }
    } else {
        ret.cpu.numThreads = std::thread::hardware_concurrency();

        if (!RprUsdIsCpuOnly()) {
            ForEachGpu(pluginType, pluginID, cachePath.c_str(),
                [&ret](int index, rpr::CreationFlags, std::string const& name) {
                if (!name.empty()) {
                    ret.gpus.push_back({index, name});
                }
            }
            );
        }
    }

    return ret;
}

bool RprUsdIsTracingEnabled() {
    return TfGetEnvSetting(RPRUSD_ENABLE_TRACING);
}

bool RprUsdIsCpuOnly() {
    return TfGetEnvSetting(RPRUSD_CPU_ONLY);
}

bool RprUsdIsGpuUsed(RprUsdContextMetadata const& contextMetadata) {
    for (auto gpuCreationFlag : kGpuCreationFlags) {
        if (contextMetadata.creationFlags & gpuCreationFlag) {
            return true;
        }
    }
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
