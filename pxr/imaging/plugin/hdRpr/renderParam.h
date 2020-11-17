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

class HdRprApi;
class HdRprVolume;

using HdRprVolumeFieldSubscription = std::shared_ptr<HdRprVolume>;
using HdRprVolumeFieldSubscriptionHandle = std::weak_ptr<HdRprVolume>;

class HdRprRenderParam final : public HdRenderParam {
public:
    HdRprRenderParam(HdRprApi* rprApi, HdRprRenderThread* renderThread)
        : m_rprApi(rprApi)
        , m_renderThread(renderThread) {}
    ~HdRprRenderParam() override = default;

    HdRprApi const* GetRprApi() const { return m_rprApi; }
    HdRprApi* AcquireRprApiForEdit() {
        m_renderThread->StopRender();
        return m_rprApi;
    }

    HdRprRenderThread* GetRenderThread() { return m_renderThread; }

    // Hydra does not mark HdVolume as changed if HdField used by it is changed
    // We implement this volume-to-field dependency by ourself until it's implemented in Hydra
    // More info: https://groups.google.com/forum/#!topic/usd-interest/pabUE0B_5X4
    HdRprVolumeFieldSubscription SubscribeVolumeForFieldUpdates(HdRprVolume* volume, SdfPath const& fieldId);
    void NotifyVolumesAboutFieldChange(HdSceneDelegate* sceneDelegate, SdfPath const& fieldId);

    // Hydra does not always mark HdRprim as changed if HdMaterial used by it has been changed.
    // HdStorm marks all existing rprims as dirty when a material is changed.
    // We instead mark only those rprims that use the changed material.
    void SubscribeForMaterialUpdates(SdfPath const& materialId, SdfPath const& rPrimId);
    void UnsubscribeFromMaterialUpdates(SdfPath const& materialId, SdfPath const& rPrimId);
    void MaterialDidChange(HdSceneDelegate* sceneDelegate, SdfPath const materialId);

    void RestartRender() { m_restartRender.store(true); }
    bool IsRenderShouldBeRestarted() { return m_restartRender.exchange(false); }

private:
    HdRprApi* m_rprApi;
    HdRprRenderThread* m_renderThread;

    std::mutex m_subscribedVolumesMutex;
    std::map<SdfPath, std::vector<HdRprVolumeFieldSubscriptionHandle>> m_subscribedVolumes;

    std::mutex m_materialSubscriptionsMutex;
    std::map<SdfPath, std::set<SdfPath>> m_materialSubscriptions;

    std::atomic<bool> m_restartRender;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_PARAM_H
