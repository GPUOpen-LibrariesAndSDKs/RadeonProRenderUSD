#include "config.h"
#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

const int HdRprConfig::kDefaultMaxSamples = 256;
const int HdRprConfig::kDefaultMinSamples = 64;
const float HdRprConfig::kDefaultVariance = 0.0f;

HdRprConfig& HdRprConfig::GetInstance() {
    static HdRprConfig instance;
    return instance;
}

void HdRprConfig::SetRenderDevice(rpr::RenderDeviceType renderDevice) {
    if (m_prefData.m_renderDevice != renderDevice) {
        m_prefData.m_renderDevice = renderDevice;
        m_dirtyFlags |= DirtyRenderDevice;
        Save();
    }
}

rpr::RenderDeviceType HdRprConfig::GetRenderDevice() const {
    return m_prefData.m_renderDevice;
}

void HdRprConfig::SetHybridQuality(HdRprHybridQuality quality) {
    if (m_prefData.m_hybridQuality != quality) {
        m_prefData.m_hybridQuality = quality;
        m_dirtyFlags |= DirtyHybridQuality;
        Save();
    }
}

HdRprHybridQuality HdRprConfig::GetHybridQuality() const {
    if (m_prefData.m_hybridQuality == HdRprHybridQuality::MEDIUM) {
        // temporarily disable until issues on hybrid side is not solved
        //   otherwise driver crashes guaranteed
        return HdRprHybridQuality::HIGH;
    }
    return m_prefData.m_hybridQuality;
}

void HdRprConfig::SetPlugin(rpr::PluginType plugin) {
    if (m_prefData.m_plugin != plugin) {
        m_prefData.m_plugin = plugin;
        m_dirtyFlags |= DirtyPlugin;
        Save();
    }
}

rpr::PluginType HdRprConfig::GetPlugin() {
    return m_prefData.m_plugin;
}

void HdRprConfig::SetDenoising(bool enableDenoising) {
    if (m_prefData.m_enableDenoising != enableDenoising) {
        m_prefData.m_enableDenoising = enableDenoising;
        m_dirtyFlags |= DirtyDenoising;
        Save();
    }
}

bool HdRprConfig::IsDenoisingEnabled() const {
    return m_prefData.m_enableDenoising;
}

void HdRprConfig::SetMinSamples(int minSamples) {
    if (m_prefData.m_minSamples != minSamples) {
        m_prefData.m_minSamples = minSamples;
        m_dirtyFlags |= DirtySampling;
        Save();
    }
}

int HdRprConfig::GetMinSamples() const {
    return m_prefData.m_minSamples;
}

void HdRprConfig::SetMaxSamples(int maxSamples) {
    if (m_prefData.m_maxSamples != maxSamples) {
        m_prefData.m_maxSamples = maxSamples;
        m_dirtyFlags |= DirtySampling;
        Save();
    }
}

int HdRprConfig::GetMaxSamples() const {
    return m_prefData.m_maxSamples;
}

void HdRprConfig::SetVariance(float variance) {
    if (m_prefData.m_variance != variance) {
        m_prefData.m_variance = variance;
        m_dirtyFlags |= DirtySampling;
        Save();
    }
}

int HdRprConfig::GetVariance() const {
    return m_prefData.m_variance;
}

bool HdRprConfig::IsDirty(ChangeTracker dirtyFlag) const {
    return m_dirtyFlags & dirtyFlag;
}

void HdRprConfig::CleanDirtyFlag(ChangeTracker dirtyFlag) {
    m_dirtyFlags &= ~dirtyFlag;
}

void HdRprConfig::ResetDirty() {
    m_dirtyFlags = Clean;
}

HdRprConfig::HdRprConfig() {
    if (!Load()) {
        m_prefData.SetDefault();
    }
}

HdRprConfig::~HdRprConfig() {
    Save();
}

bool HdRprConfig::Load() {
    std::string tmpDir = HdRprApi::GetTmpDir();
    std::string rprPreferencePath = (tmpDir.empty()) ? k_rprPreferenceFilename : tmpDir + k_rprPreferenceFilename;

    if (FILE* f = fopen(rprPreferencePath.c_str(), "rb")) {
        if (!fread(&m_prefData, sizeof(PrefData), 1, f)) {
            TF_CODING_ERROR("Fail to read rpr preferences dat file");
        }
        fclose(f);
        return IsValid();
    }

    return false;
}

void HdRprConfig::Save() {
    std::string tmpDir = HdRprApi::GetTmpDir();
    std::string rprPreferencePath = (tmpDir.empty()) ? k_rprPreferenceFilename : tmpDir + k_rprPreferenceFilename;

    if (FILE* f = fopen(rprPreferencePath.c_str(), "wb")) {
        if (!fwrite(&m_prefData, sizeof(PrefData), 1, f)) {
            TF_CODING_ERROR("Fail to write rpr preferences dat file");
        }
        fclose(f);
    }
}

bool HdRprConfig::IsValid() {
    return m_prefData.m_renderDevice >= rpr::RenderDeviceType::FIRST && m_prefData.m_renderDevice <= rpr::RenderDeviceType::LAST &&
           m_prefData.m_plugin >= rpr::PluginType::FIRST && m_prefData.m_plugin <= rpr::PluginType::LAST;
}

HdRprConfig::PrefData::PrefData() {
    SetDefault();
}

void HdRprConfig::PrefData::SetDefault() {
    m_renderDevice = rpr::RenderDeviceType::GPU;
    m_plugin = rpr::PluginType::TAHOE;
    m_hybridQuality = HdRprHybridQuality::LOW;
    m_enableDenoising = false;
    m_minSamples = kDefaultMinSamples;
    m_maxSamples = kDefaultMaxSamples;
    m_variance = kDefaultVariance;
}

PXR_NAMESPACE_CLOSE_SCOPE
