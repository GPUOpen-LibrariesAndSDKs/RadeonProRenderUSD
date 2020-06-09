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

#include "contextHelpers.h"
#include "error.h"
#include "debugCodes.h"

#include "pxr/imaging/glf/glew.h"
#include "pxr/base/arch/env.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/envSetting.h"

#include <RadeonProRender.hpp>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <dlfcn.h>
#elif defined(__linux__)
#include <link.h>
#endif // __APPLE__

#include <map>

#define PRINT_CONTEXT_CREATION_DEBUG_INFO(format, ...) \
    if (!PXR_NS::TfDebug::IsEnabled(PXR_NS::HD_RPR_DEBUG_CONTEXT_CREATION)) /* empty */; else PXR_NS::TfDebug::Helper().Msg(format, ##__VA_ARGS__)

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(HDRPR_ENABLE_TRACING, false, "Enable tracing of RPR core");
TF_DEFINE_ENV_SETTING(HDRPR_TRACING_DIR, "", "Where to store RPR core tracing files. Must be a path to valid directory");

PXR_NAMESPACE_CLOSE_SCOPE

namespace rpr {

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
    if (PXR_NS::TfGetEnvSetting(PXR_NS::HDRPR_ENABLE_TRACING)) {
        RPR_ERROR_CHECK(rprContextSetParameterByKey1u(nullptr, RPR_CONTEXT_TRACING_ENABLED, 1), "Failed to set context tracing parameter");

        auto tracingDir = PXR_NS::TfGetEnvSetting(PXR_NS::HDRPR_TRACING_DIR);
        if (!tracingDir.empty()) {
            printf("RPR tracing directory: %s\n", tracingDir.c_str());
        }
        RPR_ERROR_CHECK(rprContextSetParameterByKeyString(nullptr, RPR_CONTEXT_TRACING_PATH, tracingDir.c_str()), "Failed to set tracing directory parameter");
    }
}

const std::map<PluginType, const char*> kPluginLibNames = {
#ifdef WIN32
    {kPluginTahoe, "Tahoe64.dll"},
    {kPluginNorthStar, "Northstar64.dll"},
    {kPluginHybrid, "Hybrid.dll"},
#elif defined __linux__
    {kPluginTahoe, "libTahoe64.so"},
    {kPluginHybrid, "Hybrid.so"},
#elif defined __APPLE__
    {kPluginTahoe, "libTahoe64.dylib"},
#endif
};

rpr::CreationFlags getAllCompatibleGpuFlags(rpr_int pluginID, const char* cachePath) {
    rpr::CreationFlags additionalFlags = 0x0;

#if defined(__APPLE__)
    additionalFlags |= RPR_CREATION_FLAGS_ENABLE_METAL;
#endif

    auto contextIsCreatable = [additionalFlags, pluginID, cachePath](rpr::CreationFlags creationFlag, rpr_context_info contextInfo) {
        rpr_context temporaryContext = nullptr;
        rpr_int id = pluginID;
        auto status = rprCreateContext(RPR_API_VERSION, &id, 1, creationFlag | additionalFlags, nullptr, cachePath, &temporaryContext);
        if (status == RPR_SUCCESS) {
            size_t size = 0;
            auto infoStatus = rprContextGetInfo(temporaryContext, contextInfo, 0, 0, &size);
            if (infoStatus == RPR_SUCCESS) {
                std::string deviceName;
                deviceName.resize(size);
                infoStatus = rprContextGetInfo(temporaryContext, contextInfo, size, &deviceName[0], 0);
                if (infoStatus == RPR_SUCCESS) {
                    PRINT_CONTEXT_CREATION_DEBUG_INFO("%s\n", deviceName.c_str());
                }
            }
            if (infoStatus != RPR_SUCCESS) {
                PRINT_CONTEXT_CREATION_DEBUG_INFO("Failed to query device name: %d\n", infoStatus);
                return false;
            }

            rprObjectDelete(temporaryContext);
        }
        return status == RPR_SUCCESS;
    };

    PRINT_CONTEXT_CREATION_DEBUG_INFO("GPUs:\n");

    rpr::CreationFlags creationFlags = 0x0;
#define TEST_GPU_COMPATIBILITY(index) \
    if (contextIsCreatable(RPR_CREATION_FLAGS_ENABLE_GPU ## index, RPR_CONTEXT_GPU ## index ## _NAME)) { \
        creationFlags |= RPR_CREATION_FLAGS_ENABLE_GPU ## index; \
    }

    TEST_GPU_COMPATIBILITY(0);
    TEST_GPU_COMPATIBILITY(1);
    TEST_GPU_COMPATIBILITY(2);
    TEST_GPU_COMPATIBILITY(3);
    TEST_GPU_COMPATIBILITY(4);
    TEST_GPU_COMPATIBILITY(5);
    TEST_GPU_COMPATIBILITY(6);
    TEST_GPU_COMPATIBILITY(7);
    TEST_GPU_COMPATIBILITY(8);
    TEST_GPU_COMPATIBILITY(9);
    TEST_GPU_COMPATIBILITY(10);
    TEST_GPU_COMPATIBILITY(11);
    TEST_GPU_COMPATIBILITY(12);
    TEST_GPU_COMPATIBILITY(13);
    TEST_GPU_COMPATIBILITY(14);
    TEST_GPU_COMPATIBILITY(15);

    if (!creationFlags) {
        PRINT_CONTEXT_CREATION_DEBUG_INFO("None\n");
    }

    return creationFlags;
}

rpr::CreationFlags getRprCreationFlags(RenderDeviceType renderDevice, rpr_int pluginID, const char* cachePath) {
    rpr::CreationFlags flags = 0x0;

    if (kRenderDeviceCPU == renderDevice) {
        PRINT_CONTEXT_CREATION_DEBUG_INFO("hdRpr CPU context\n");
        flags = RPR_CREATION_FLAGS_ENABLE_CPU;
    } else if (kRenderDeviceGPU == renderDevice) {
        PRINT_CONTEXT_CREATION_DEBUG_INFO("hdRpr GPU context\n");
        flags = getAllCompatibleGpuFlags(pluginID, cachePath);
    } else {
        return 0x0;
    }

#if __APPLE__
    flags |= RPR_CREATION_FLAGS_ENABLE_METAL;
#endif

    return flags;
}

} // namespace anonymous

Context* CreateContext(char const* cachePath, ContextMetadata* metadata) {
    SetupRprTracing();

    auto pluginLibNameIter = kPluginLibNames.find(metadata->pluginType);
    if (pluginLibNameIter == kPluginLibNames.end()) {
        PRINT_CONTEXT_CREATION_DEBUG_INFO("Plugin is not supported: %d", metadata->pluginType);
        return nullptr;
    }
    auto pluginLibName = pluginLibNameIter->second;

    const std::string rprSdkPath = GetRprSdkPath();
    const std::string pluginPath = rprSdkPath.empty() ? pluginLibName : rprSdkPath + "/" + pluginLibName;
    rpr_int pluginID = rprRegisterPlugin(pluginPath.c_str());
    if (pluginID == -1) {
        PRINT_CONTEXT_CREATION_DEBUG_INFO("Failed to register %s plugin located at \"%s\"", pluginLibName, pluginPath.c_str());
        return nullptr;
    }

    rpr::CreationFlags flags;
    if (metadata->pluginType == kPluginHybrid) {
        // Call to getRprCreationFlags is broken in case of hybrid:
        //   1) getRprCreationFlags uses 'rprContextGetInfo' to query device compatibility,
        //        but hybrid plugin does not support such call
        //   2) Hybrid is working only on GPU
        //   3) MultiGPU can be enabled only through vulkan interop
        flags = RPR_CREATION_FLAGS_ENABLE_GPU0;
    } else {
        flags = getRprCreationFlags(metadata->renderDeviceType, pluginID, cachePath);
        if (!flags) {
            bool isGpuIncompatible = metadata->renderDeviceType == kRenderDeviceGPU;
            PRINT_CONTEXT_CREATION_DEBUG_INFO("%s is not compatible", isGpuIncompatible ? "GPU" : "CPU");
            metadata->renderDeviceType = isGpuIncompatible ? kRenderDeviceCPU : kRenderDeviceGPU;
            flags = getRprCreationFlags(metadata->renderDeviceType, pluginID, cachePath);
            if (!flags) {
                PRINT_CONTEXT_CREATION_DEBUG_INFO("Could not find compatible device");
                return nullptr;
            } else {
                PRINT_CONTEXT_CREATION_DEBUG_INFO("Using %s for render computations", isGpuIncompatible ? "CPU" : "GPU");
            }
        }
    }

    if (metadata->isGlInteropEnabled) {
        if (metadata->renderDeviceType == kRenderDeviceCPU || metadata->pluginType == kPluginHybrid) {
            PRINT_CONTEXT_CREATION_DEBUG_INFO("GL interop could not be used with CPU rendering or Hybrid plugin");
            metadata->isGlInteropEnabled = false;
        } else if (!PXR_NS::GlfGlewInit()) {
            PRINT_CONTEXT_CREATION_DEBUG_INFO("Failed to init GLEW. Disabling GL interop");
            metadata->isGlInteropEnabled = false;
        } else {
            metadata->isGlInteropEnabled = true;
        }
    }

    if (metadata->isGlInteropEnabled) {
        flags |= RPR_CREATION_FLAGS_ENABLE_GL_INTEROP;
    }

    Status status;
    auto context = Context::Create(RPR_API_VERSION, &pluginID, 1, flags, nullptr, cachePath, &status);
    if (context) {
        if (RPR_ERROR_CHECK(context->SetActivePlugin(pluginID), "Failed to set active plugin")) {
            delete context;
            return nullptr;
        }
    } else {
        RPR_ERROR_CHECK(status, "Failed to create RPR context");
    }

    return context;
}

} // namespace rpr
