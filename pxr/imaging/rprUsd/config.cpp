#include "config.h"
#include "pxr/base/arch/env.h"
#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/tf/tf.h"
#include "pxr/base/tf/instantiateSingleton.h"

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

namespace {

bool ArchCreateDirectory(const char* path) {
#ifdef WIN32
    return CreateDirectory(path, NULL) == TRUE;
#else
    return mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0;
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
        ArchCreateDirectory(configDir.c_str());
    }
    m_filepath = configDir + (ARCH_PATH_SEP "cfg.json");

    std::ifstream cfgFile(m_filepath);
    if (cfgFile.is_open()) {
        cfgFile >> m_impl->cfg;
    }

    auto defaultCacheDir = configDir + (ARCH_PATH_SEP "cache");

    bool configDirty = false;
    configDirty |= InitJsonProperty(kShowRestartRequiredMessage, true, &m_impl->cfg);
    configDirty |= InitJsonProperty(kTextureCacheDir, defaultCacheDir, &m_impl->cfg);
    configDirty |= InitJsonProperty(kKernelCacheDir, defaultCacheDir, &m_impl->cfg);
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
std::string const& RprUsdConfig::GetTextureCacheDir() const {
    return m_impl->cfg[kTextureCacheDir].get_ref<std::string const&>();
}

void RprUsdConfig::SetKernelCacheDir(std::string const& newValue) {
    if (m_impl->cfg[kKernelCacheDir] != newValue) {
        m_impl->cfg[kKernelCacheDir] = newValue;
        Save();
    }
}
std::string const& RprUsdConfig::GetKernelCacheDir() const {
    return m_impl->cfg.at(kKernelCacheDir).get_ref<std::string const&>();
}

PXR_NAMESPACE_CLOSE_SCOPE
