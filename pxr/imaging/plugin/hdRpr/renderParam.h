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

#ifndef HDRPR_RENDER_PARAM_H
#define HDRPR_RENDER_PARAM_H

#include "renderThread.h"

#include "pxr/imaging/hd/renderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

#define HDRPR_MATERIAL_NETWORK_SELECTOR_TOKENS \
    (rpr) \
    (karma)

TF_DECLARE_PUBLIC_TOKENS(HdRprMaterialNetworkSelectorTokens, HDRPR_MATERIAL_NETWORK_SELECTOR_TOKENS);

class HdRprApi;
class HdRprVolume;

using HdRprVolumeFieldSubscription = std::shared_ptr<HdRprVolume>;
using HdRprVolumeFieldSubscriptionHandle = std::weak_ptr<HdRprVolume>;

class HdRprRenderParam final : public HdRenderParam {
public:
    HdRprRenderParam(HdRprApi* rprApi, HdRprRenderThread* renderThread)
        : m_rprApi(rprApi)
        , m_renderThread(renderThread) {
        InitializeEnvParameters();
    }
    ~HdRprRenderParam() override = default;

    HdRprApi const* GetRprApi() const { return m_rprApi; }
    HdRprApi* AcquireRprApiForEdit() {
        m_renderThread->StopRender();
        return m_rprApi;
    }

    HdRprRenderThread* GetRenderThread() { return m_renderThread; }

    TfToken const& GetMaterialNetworkSelector() const { return m_materialNetworkSelector; }

    // Hydra does not mark HdVolume as changed if HdField used by it is changed
    // We implement this volume-to-field dependency by ourself until it's implemented in Hydra
    // More info: https://groups.google.com/forum/#!topic/usd-interest/pabUE0B_5X4
    HdRprVolumeFieldSubscription SubscribeVolumeForFieldUpdates(HdRprVolume* volume, SdfPath const& fieldId);
    void NotifyVolumesAboutFieldChange(HdSceneDelegate* sceneDelegate, SdfPath const& fieldId);

    void RestartRender() { m_restartRender.store(true); }
    bool IsRenderShouldBeRestarted() { return m_restartRender.exchange(false); }

private:
    void InitializeEnvParameters();

    HdRprApi* m_rprApi;
    HdRprRenderThread* m_renderThread;

    TfToken m_materialNetworkSelector;

    std::mutex m_subscribedVolumesMutex;
    std::map<SdfPath, std::vector<HdRprVolumeFieldSubscriptionHandle>> m_subscribedVolumes;

    std::atomic<bool> m_restartRender;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_PARAM_H
