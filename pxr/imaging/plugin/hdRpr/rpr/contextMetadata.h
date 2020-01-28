#ifndef HDRPR_CORE_CONTEXT_METADATA_H
#define HDRPR_CORE_CONTEXT_METADATA_H

namespace rpr {

enum PluginType {
    kPluginInvalid = -1,
    kPluginTahoe,
    kPluginNorthStar,
    kPluginHybrid,
    kPluginsCount
};

enum RenderDeviceType {
    kRenderDeviceInvalid = -1,
    kRenderDeviceCPU,
    kRenderDeviceGPU,
    kRenderDevicesCount
};

struct ContextMetadata {
    PluginType pluginType = kPluginInvalid;
    RenderDeviceType renderDeviceType = kRenderDeviceInvalid;
    bool isGlInteropEnabled = false;
};

} // namespace rpr

#endif // HDRPR_CORE_CONTEXT_METADATA_H
