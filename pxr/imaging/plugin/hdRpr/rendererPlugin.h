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

#ifndef HDRPR_RENDERER_PLUGIN_H
#define HDRPR_RENDERER_PLUGIN_H

#include "pxr/imaging/hd/rendererPlugin.h"
#include "pxr/imaging/rprUsd/apiCapabilities.h"

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

#if RPR_USD_RENDERER_CREATE_ARGS
    bool IsSupported(const HdRendererCreateArgs& args,
                     std::string* reasonWhyNot = nullptr) const override { return true; }
#elif PXR_VERSION < 2302
    bool IsSupported() const override { return true; }
#else
    bool IsSupported(bool gpuEnabled = true) const override { return true; }
#endif
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDERER_PLUGIN_H
