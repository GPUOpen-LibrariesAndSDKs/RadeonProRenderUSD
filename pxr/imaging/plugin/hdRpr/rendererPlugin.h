#ifndef HDRPR_RENDERER_PLUGIN_H
#define HDRPR_RENDERER_PLUGIN_H

#include "pxr/imaging/hd/rendererPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprPlugin final : public HdRendererPlugin {
public:
    HdRprPlugin() = default;
    ~HdRprPlugin() override = default;

    HdRprPlugin(const HdRprPlugin&) = delete;
    HdRprPlugin& operator =(const HdRprPlugin&) = delete;

    HdRenderDelegate* CreateRenderDelegate() override;

    HdRenderDelegate* CreateRenderDelegate(HdRenderSettingsMap const& settingsMap) override;

    void DeleteRenderDelegate(HdRenderDelegate* renderDelegate) override;

    bool IsSupported() const override { return true; }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDERER_PLUGIN_H
