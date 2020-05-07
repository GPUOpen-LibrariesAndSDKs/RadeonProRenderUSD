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

#include "renderParam.h"
#include "volume.h"

#include "pxr/base/tf/envSetting.h"

#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdRprMaterialNetworkSelectorTokens, HDRPR_MATERIAL_NETWORK_SELECTOR_TOKENS);

TF_DEFINE_ENV_SETTING(HDRPR_MATERIAL_NETWORK_SELECTOR, HDRPR_DEFAULT_MATERIAL_NETWORK_SELECTOR,
        "Material network selector to be used in hdRpr");

void HdRprRenderParam::InitializeEnvParameters() {
    m_materialNetworkSelector = TfToken(TfGetEnvSetting(HDRPR_MATERIAL_NETWORK_SELECTOR));
}

HdRprVolumeFieldSubscription HdRprRenderParam::SubscribeVolumeForFieldUpdates(
    HdRprVolume* volume, SdfPath const& fieldId) {
    auto sub = HdRprVolumeFieldSubscription(volume, [](HdRprVolume* volume) {});
    {
        std::lock_guard<std::mutex> lock(m_subscribedVolumesMutex);
        m_subscribedVolumes[fieldId].push_back(std::move(sub));
    }
    return sub;
}

void HdRprRenderParam::NotifyVolumesAboutFieldChange(HdSceneDelegate* sceneDelegate, SdfPath const& fieldId) {
    std::lock_guard<std::mutex> lock(m_subscribedVolumesMutex);
    for (auto subscriptionsIt = m_subscribedVolumes.begin();
         subscriptionsIt != m_subscribedVolumes.end();) {
        auto& subscriptions = subscriptionsIt->second;
        for (size_t i = 0; i < subscriptions.size(); ++i) {
            if (auto volume = subscriptions[i].lock()) {
                // Force HdVolume Sync
                sceneDelegate->GetRenderIndex().GetChangeTracker().MarkRprimDirty(volume->GetId(), HdChangeTracker::DirtyTopology);

                // Possible Optimization: notify volume about exact changed field
                // Does not make sense right now because Hydra removes and creates
                // from scratch all HdFields whenever one of them is changed (e.g added/removed/edited primvar)
                // (USD 20.02)
            } else {
                std::swap(subscriptions[i], subscriptions.back());
                subscriptions.pop_back();
            }
        }
        if (subscriptions.empty()) {
            subscriptionsIt = m_subscribedVolumes.erase(subscriptionsIt);
        } else {
            ++subscriptionsIt;
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
