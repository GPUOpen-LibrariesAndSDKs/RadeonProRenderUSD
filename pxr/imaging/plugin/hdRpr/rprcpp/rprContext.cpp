#include "rprContext.h"
#include "rprError.h"

#include "../rprTools/RprTools.h"
#include "../rprTools/RprTools.cpp"

#include "pxr/imaging/glf/glew.h"
#include "pxr/base/arch/env.h"
#include "pxr/base/tf/diagnostic.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <dlfcn.h>
#elif defined(__linux__)
#include <link.h>
#endif // __APPLE__

PXR_NAMESPACE_OPEN_SCOPE

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

    TF_CODING_ERROR("Path to RPR SDK with %s not found", k_RadeonProRenderLibName);
#elif defined(__linux__)
    if (auto handle = dlopen(nullptr, RTLD_NOW)) {
        link_map* map = nullptr;
        if (dlinfo(handle, RTLD_DI_LINKMAP, &map)) {
            const char* errorStr = "unknown reason";
            if (auto error = dlerror()) {
                errorStr = error;
            }
            TF_RUNTIME_ERROR("Failed to query RPR SDK path: %s", errorStr);
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
    auto enableTracingEnv = ArchGetEnv("RPR_ENABLE_TRACING");
    if (enableTracingEnv == "1") {
        RPR_ERROR_CHECK(rprContextSetParameterByKey1u(nullptr, RPR_CONTEXT_TRACING_ENABLED, 1), "Fail to set context tracing parameter");

        auto tracingFolder = ArchGetEnv("RPR_TRACING_PATH");
        if (tracingFolder.empty()) {
#ifdef WIN32
            tracingFolder = "C:\\ProgramData\\hdRPR";
#elif defined __linux__ || defined(__APPLE__)
            std::vector<std::string> pathVariants = { ArchGetEnv("TMPDIR"), ArchGetEnv("P_tmpdir"), "/tmp" };
            for (auto& pathVariant : pathVariants) {
                if (pathVariant.empty()) {
                    continue;
                }

                tracingFolder = pathVariant + "/hdRPR";
                break;
            }
#else
#error "Unsupported platform"
#endif
        }
        printf("RPR tracing folder: %s\n", tracingFolder.c_str());
        RPR_ERROR_CHECK(rprContextSetParameterByKeyString(nullptr, RPR_CONTEXT_TRACING_PATH, tracingFolder.c_str()), "Fail to set tracing folder parameter");
    }
}

const char* k_PluginLibNames[] = {
#ifdef WIN32
        "Tahoe64.dll",
        "Hybrid.dll",
#elif defined __linux__
        "libTahoe64.so",
        "libHybrid.so",
#elif defined __APPLE__
        "libTahoe64.dylib",
        "libHybrid.dylib",
#endif
};

rpr_creation_flags getAllCompatibleGpuFlags(rpr_int pluginID, const char* cachePath) {
    rpr_creation_flags additionalFlags = 0x0;
#ifdef WIN32
    RPR_TOOLS_OS rprToolOs = RPR_TOOLS_OS::RPRTOS_WINDOWS;
#elif defined(__APPLE__)
    RPR_TOOLS_OS rprToolOs = RPR_TOOLS_OS::RPRTOS_MACOS;
    additionalFlags |= RPR_CREATION_FLAGS_ENABLE_METAL;
#else
    RPR_TOOLS_OS rprToolOs = RPR_TOOLS_OS::RPRTOS_LINUX;
#endif // WIN32

    rpr_creation_flags creationFlags = 0x0;
#define TEST_GPU_COMPATIBILITY(index) \
    if (rprIsDeviceCompatible(pluginID, RPRTD_GPU ## index, cachePath, false, rprToolOs, additionalFlags) == RPRTC_COMPATIBLE) { \
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

    return creationFlags;
}

rpr_creation_flags getRprCreationFlags(RenderDeviceType renderDevice, rpr_int pluginID, const char* cachePath) {
    rpr_creation_flags flags = 0x0;

    if (RenderDeviceType::CPU == renderDevice) {
        flags = RPR_CREATION_FLAGS_ENABLE_CPU;
    } else if (RenderDeviceType::GPU == renderDevice) {
        flags = getAllCompatibleGpuFlags(pluginID, cachePath);
    } else {
        TF_CODING_ERROR("Unknown RenderDeviceType");
        return 0x0;
    }

    #if __APPLE__
        flags |= RPR_CREATION_FLAGS_ENABLE_METAL;
    #endif

    return flags;
}

} // namespace anonymous

std::unique_ptr<Context> Context::CreateContext(PluginType plugin, RenderDeviceType renderDevice, bool enableGlInterop, char const* cachePath) {
    auto context = std::unique_ptr<Context>(new Context);
    context->m_activePlugin = plugin;
    context->m_renderDevice = renderDevice;

    int pluginIdx = static_cast<int>(context->m_activePlugin);
    int numPlugins = sizeof(k_PluginLibNames) / sizeof(k_PluginLibNames[0]);
    if (pluginIdx < 0 || pluginIdx >= numPlugins) {
        TF_CODING_ERROR("Invalid plugin requested: index out of bounds - %d", pluginIdx);
        return nullptr;
    }
    auto pluginName = k_PluginLibNames[pluginIdx];

    const std::string rprSdkPath = GetRprSdkPath();
    const std::string pluginPath = (rprSdkPath.empty()) ? pluginName : rprSdkPath + "/" + pluginName;
    rpr_int pluginID = rprRegisterPlugin(pluginPath.c_str());
    if (pluginID == -1) {
        TF_RUNTIME_ERROR("Failed to register %s plugin", pluginName);
        return nullptr;
    }

    context->m_useGlInterop = enableGlInterop;
    if (context->m_useGlInterop && (context->m_renderDevice == RenderDeviceType::CPU ||
        context->m_activePlugin == PluginType::HYBRID)) {
        context->m_useGlInterop = false;
    }
    if (context->m_useGlInterop && !GlfGlewInit()) {
        TF_WARN("Failed to init GLEW. Disabling GL interop");
        context->m_useGlInterop = false;
    }

    rpr_creation_flags flags;
    if (context->m_activePlugin == PluginType::HYBRID) {
        // Call to getRprCreationFlags is broken in case of hybrid:
        //   1) getRprCreationFlags uses 'rprContextGetInfo' to query device compatibility,
        //        but hybrid plugin does not support such call
        //   2) Hybrid is working only on GPU
        //   3) MultiGPU can be enabled only through vulkan interop
        flags = RPR_CREATION_FLAGS_ENABLE_GPU0;
    } else {
        flags = getRprCreationFlags(context->m_renderDevice, pluginID, cachePath);
        if (!flags) {
            bool isGpuUncompatible = context->m_renderDevice == RenderDeviceType::GPU;
            TF_WARN("%s is not compatible", isGpuUncompatible ? "GPU" : "CPU");
            context->m_renderDevice = isGpuUncompatible ? RenderDeviceType::CPU : RenderDeviceType::GPU;
            flags = getRprCreationFlags(context->m_renderDevice, pluginID, cachePath);
            if (!flags) {
                TF_RUNTIME_ERROR("Could not find compatible device");
                return nullptr;
            } else {
                TF_WARN("Using %s for render computations", isGpuUncompatible ? "CPU" : "GPU");
                if (context->m_renderDevice == RenderDeviceType::CPU) {
                    context->m_useGlInterop = false;
                }
            }
        }
    }

    if (context->m_useGlInterop) {
        flags |= RPR_CREATION_FLAGS_ENABLE_GL_INTEROP;
    }

    rpr_context contextHandle;
    auto status = rprCreateContext(RPR_API_VERSION, &pluginID, 1, flags, nullptr, cachePath, &contextHandle);
    if (status != RPR_SUCCESS) {
        TF_RUNTIME_ERROR("Fail to create context with %s plugin. Error code: %d", pluginName, status);
        return nullptr;
    }

    status = rprContextSetActivePlugin(contextHandle, pluginID);
    if (status != RPR_SUCCESS) {
        rprObjectDelete(contextHandle);
        TF_RUNTIME_ERROR("Fail to set active %s plugin. Error code: %d", pluginName, status);
        return nullptr;
    }

    context->m_rprObjectHandle = contextHandle;
    return context;
}

std::unique_ptr<Context> Context::Create(PluginType requestedPlugin, RenderDeviceType renderDevice, bool enableGlInterop, char const* cachePath) {
    SetupRprTracing();

    auto context = CreateContext(requestedPlugin, renderDevice, enableGlInterop, cachePath);
    if (!context) {
        TF_WARN("Failed to create context with requested plugin. Trying to create with first working variant");
        for (auto plugin = PluginType::FIRST; plugin != PluginType::LAST; plugin = PluginType(int(plugin) + 1)) {
            if (plugin == requestedPlugin) {
                continue;
            }
            context = CreateContext(plugin, renderDevice, enableGlInterop, cachePath);
            if (context) {
                break;
            }
        }
    }

    return context;
}

rpr_context Context::GetHandle() const {
    return reinterpret_cast<rpr_context>(m_rprObjectHandle);
}

bool Context::IsGlInteropEnabled() const {
    return m_useGlInterop;
}

PluginType Context::GetActivePluginType() const {
    return m_activePlugin;
}

RenderDeviceType Context::GetActiveRenderDeviceType() const {
    return m_renderDevice;
}

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE
