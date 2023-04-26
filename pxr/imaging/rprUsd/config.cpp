#include "config.h"
#include "pxr/base/arch/env.h"
#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/tf/tf.h"
#include "pxr/base/tf/instantiateSingleton.h"
#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/thisPlugin.h"

#include <json.hpp>
using json = nlohmann::json;

#include <fstream>

#ifdef WIN32
#include <shlobj_core.h>
#pragma comment(lib,"Shell32.lib")
#elif defined(__linux__)
#include <limits.h>
#include <sys/stat.h>
#endif // __APPLE__

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(HDRPR_CACHE_PATH_OVERRIDE, "",
    "Set this to override shaders cache path");

namespace {

bool ArchCreateDirectory(std::string const & path) {
    return TfMakeDirs(path, -1, true);
}

bool ArchDirectoryExists(const char* path) {
#ifdef WIN32
    DWORD ftyp = GetFileAttributesA(path);
    if (ftyp == INVALID_FILE_ATTRIBUTES)
        return false;  //something is wrong with your path!

    if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
        return true;   // this is a directory!

    return false;    // this is not a directory!
#else
    throw std::runtime_error("ArchDirectoryExists not implemented for this platform");
    return false;
#endif
}

std::string GetAppDataPath() {
#ifdef WIN32
    char appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, 0, appDataPath))) {
        return appDataPath;
    }
#elif defined(__linux__)
    auto homeEnv = ArchGetEnv("XDG_DATA_HOME");
    if (!homeEnv.empty() && homeEnv[0] == '/') {
        return homeEnv;
    }

    int uid = getuid();
    homeEnv = ArchGetEnv("HOME");
    if (uid != 0 && !homeEnv.empty()) {
        return homeEnv + "/.config";
    }

#elif defined(__APPLE__)
    auto homeEnv = ArchGetEnv("HOME");
    if (!homeEnv.empty() && homeEnv[0] == '/') {
        return homeEnv + "/Library/Application Support";
    }
#else
#error "Unknown platform"
#endif

    return ".";
}

std::string GetDefaultCacheDir(const char* cacheType) {
    // Return HDRPR_CACHE_PATH_OVERRIDE if provided
    auto overriddenCacheDir = TfGetEnvSetting(HDRPR_CACHE_PATH_OVERRIDE);
    if (!overriddenCacheDir.empty()) {
        overriddenCacheDir = overriddenCacheDir + ARCH_PATH_SEP + cacheType;

        bool directoryExists = ArchDirectoryExists(overriddenCacheDir.c_str());
        if (!directoryExists) {
            bool succeeded = ArchCreateDirectory(overriddenCacheDir);
            if (!succeeded)
            {
                TF_RUNTIME_ERROR("Can't create shader cache directory at: %s", overriddenCacheDir.c_str());
            }
        }

        return overriddenCacheDir;
    }
    
    PlugPluginPtr plugin = PLUG_THIS_PLUGIN;
    auto cacheDir = plugin->GetResourcePath();
    if (cacheDir.empty()) {
        // Fallback to AppData
        cacheDir = GetAppDataPath() + (ARCH_PATH_SEP "hdRpr");
        ArchCreateDirectory(cacheDir);
    }

    cacheDir += (ARCH_PATH_SEP "cache");
    ArchCreateDirectory(cacheDir);

    cacheDir = cacheDir + ARCH_PATH_SEP + cacheType;
    ArchCreateDirectory(cacheDir);

    return cacheDir;
}

template <typename T>
bool InitJsonProperty(const char* propertyName, T const& defaultValue, json* json) {
    bool setDefaultValue = false;

    auto it = json->find(propertyName);
    if (it == json->end()) {
        setDefaultValue = true;
    } else {
        try {
            it->get<T>();
        } catch (json::exception& e) {
            TF_UNUSED(e);
            setDefaultValue = true;
        }
    }

    if (setDefaultValue) {
        (*json)[propertyName] = defaultValue;
    }
    return setDefaultValue;
}

template <typename T>
bool GetJsonProperty(const char* propertyName, json const& json, T* property) {
    auto it = json.find(propertyName);
    if (it != json.end()) {
        try {
            *property = it->get<T>();
            return true;
        } catch (json::exception& e) {
            TF_UNUSED(e);
        }
    }

    return false;
}

const char* kShowRestartRequiredMessage = "ShowRestartRequiredMessage";
const char* kTextureCacheDir = "TextureCacheDir";
const char* kKernelCacheDir = "KernelCacheDir";

} // namespace anonymous

TF_INSTANTIATE_SINGLETON(RprUsdConfig);

std::unique_lock<std::mutex> RprUsdConfig::GetInstance(RprUsdConfig** instance) {
    static std::mutex instanceMutex;
    std::unique_lock<std::mutex> lock(instanceMutex);

    *instance = &TfSingleton<RprUsdConfig>::GetInstance();
    return lock;
}

RprUsdConfig::~RprUsdConfig() = default;

struct RprUsdConfig::Impl {
    json cfg;
};

RprUsdConfig::RprUsdConfig()
    : m_impl(new Impl) {
    auto configDir = ArchGetEnv("RPRUSD_CONFIG_PATH");
    if (configDir.empty()) {
        configDir = GetAppDataPath() + (ARCH_PATH_SEP "hdRpr");
    }
    if (!configDir.empty()) {
        ArchCreateDirectory(configDir);
    }
    m_filepath = configDir + (ARCH_PATH_SEP "cfg.json");

    std::ifstream cfgFile(m_filepath);
    if (cfgFile.is_open()) {
        cfgFile >> m_impl->cfg;
    }

    bool configDirty = false;
    configDirty |= InitJsonProperty(kShowRestartRequiredMessage, true, &m_impl->cfg);
    if (configDirty) {
        Save();
    }
}

void RprUsdConfig::Save() {
    std::ofstream cfgFile(m_filepath);
    if (!cfgFile.is_open()) {
        TF_RUNTIME_ERROR("Failed to save RprUsd config: cannot open file \"%s\"", m_filepath.c_str());
        return;
    }

    cfgFile << m_impl->cfg;
}

void RprUsdConfig::SetRestartWarning(bool newValue) {
    if (m_impl->cfg[kShowRestartRequiredMessage] != newValue) {
        m_impl->cfg[kShowRestartRequiredMessage] = newValue;
        Save();
    }
}
bool RprUsdConfig::IsRestartWarningEnabled() const {
    return m_impl->cfg[kShowRestartRequiredMessage];
}

void RprUsdConfig::SetTextureCacheDir(std::string const& newValue) {
    if (m_impl->cfg[kTextureCacheDir] != newValue) {
        m_impl->cfg[kTextureCacheDir] = newValue;
        Save();
    }
}
std::string RprUsdConfig::GetTextureCacheDir() const {
    std::string ret;
    if (!GetJsonProperty(kTextureCacheDir, m_impl->cfg, &ret)) {
        ret = GetDefaultCacheDir("texture");
    }
    return ret;
}

void RprUsdConfig::SetKernelCacheDir(std::string const& newValue) {
    if (m_impl->cfg[kKernelCacheDir] != newValue) {
        m_impl->cfg[kKernelCacheDir] = newValue;
        Save();
    }
}
std::string RprUsdConfig::GetKernelCacheDir() const {
    std::string ret;
    if (!GetJsonProperty(kKernelCacheDir, m_impl->cfg, &ret)) {
        ret = GetDefaultCacheDir("kernel");
    }
    return ret;
}
std::string RprUsdConfig::GetPrecompiledKernelDir() const {
    PlugPluginPtr plugin = PLUG_THIS_PLUGIN;
    auto kernelsDir = PlugFindPluginResource(plugin, "ns_kernels", true);
    if (kernelsDir.empty()) {
        TF_RUNTIME_ERROR("Failed to find precompiled kernels for Northstar");
    }
    return kernelsDir;
}

std::string RprUsdConfig::GetDeviceConfigurationFilepath() const {
    std::string configCacheDir = GetDefaultCacheDir("config");
    return configCacheDir + ARCH_PATH_SEP + "devicesConfig.txt";
}

PXR_NAMESPACE_CLOSE_SCOPE
