#ifndef HDRPR_CONFIG_H
#define HDRPR_CONFIG_H

#include <pxr/pxr.h>

#include "rprcpp/rprContext.h"

PXR_NAMESPACE_OPEN_SCOPE

enum class HdRprHybridQuality {
    NONE = -1,
    LOW = 0,
    MEDIUM,
    HIGH,
    FIRST = LOW,
    LAST = HIGH
};

class HdRprConfig {
public:
    constexpr static int kDefaultMaxSamples = 256;
    constexpr static int kDefaultMinSamples = 64;
    constexpr static float kDefaultVariance = 0.0f;

    enum ChangeTracker {
        Clean = 0,
        DirtyAll = ~0u,
        DirtyRenderDevice = 1 << 0,
        DirtyPlugin = 1 << 1,
        DirtyHybridQuality = 1 << 2,
        DirtyDenoising = 1 << 3,
        DirtySampling = 1 << 4
    };

    static HdRprConfig& GetInstance();

    void SetRenderDevice(rpr::RenderDeviceType renderDevice);
    rpr::RenderDeviceType GetRenderDevice() const;

    void SetHybridQuality(HdRprHybridQuality quality);
    HdRprHybridQuality GetHybridQuality() const;

    void SetPlugin(rpr::PluginType plugin);
    rpr::PluginType GetPlugin();

    void SetDenoising(bool enableDenoising);
    bool IsDenoisingEnabled() const;

    void SetMinSamples(int minSamples);
    int GetMinSamples() const;

    void SetMaxSamples(int maxSamples);
    int GetMaxSamples() const;

    void SetVariance(float variance);
    int GetVariance() const;

    bool IsDirty(ChangeTracker dirtyFlag) const;
    void CleanDirtyFlag(ChangeTracker dirtyFlag);
    void ResetDirty();

private:
    HdRprConfig();
    ~HdRprConfig();

    bool Load();
    void Save();

    bool IsValid();

    struct PrefData {
        rpr::RenderDeviceType m_renderDevice;
        rpr::PluginType m_plugin;
        HdRprHybridQuality m_hybridQuality;
        bool m_enableDenoising;
        int m_minSamples;
        int m_maxSamples;
        float m_variance;

        PrefData();
        void SetDefault();
    };
    PrefData m_prefData;

    uint32_t m_dirtyFlags = DirtyAll;

    constexpr static const char* k_rprPreferenceFilename = "hdRprPreferences.dat";
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_CONFIG_H
