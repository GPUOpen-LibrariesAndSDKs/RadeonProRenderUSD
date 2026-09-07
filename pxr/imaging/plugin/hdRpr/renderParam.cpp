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

#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

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

void HdRprRenderParam::SubscribeForMaterialUpdates(SdfPath const& materialId, SdfPath const& rPrimId) {
    std::lock_guard<std::mutex> lock(m_materialSubscriptionsMutex);
    m_materialSubscriptions[materialId].insert(rPrimId);
}

void HdRprRenderParam::UnsubscribeFromMaterialUpdates(SdfPath const& materialId, SdfPath const& rPrimId) {
    std::lock_guard<std::mutex> lock(m_materialSubscriptionsMutex);
    auto subscriptionsIt = m_materialSubscriptions.find(materialId);
    if (TF_VERIFY(subscriptionsIt != m_materialSubscriptions.end())) {
        subscriptionsIt->second.erase(rPrimId);
        if (subscriptionsIt->second.empty()) {
            m_materialSubscriptions.erase(subscriptionsIt);
        }
    }
}

void HdRprRenderParam::MaterialDidChange(HdSceneDelegate* sceneDelegate, SdfPath const materialId) {
    std::lock_guard<std::mutex> lock(m_materialSubscriptionsMutex);
    auto subscriptionsIt = m_materialSubscriptions.find(materialId);
    if (subscriptionsIt != m_materialSubscriptions.end()) {
        HdChangeTracker& changeTracker = sceneDelegate->GetRenderIndex().GetChangeTracker();
        for (auto& rPrimId : subscriptionsIt->second) {
            changeTracker.MarkRprimDirty(rPrimId, HdChangeTracker::DirtyMaterialId);
        }
    }
}

RprApiSafeWrapper::RprApiSafeWrapper(HdRprRenderParam* renderParam, HdRprApi* rprApi)
    : m_renderParam(renderParam)
    , m_rprApi(rprApi) {
    std::lock_guard<std::mutex> lock(m_renderParam->m_threadControlMutex);
    if (m_renderParam->m_editCount == 0) {
        m_renderParam->GetRenderThread()->StopRender();
    }
    ++m_renderParam->m_editCount;
}

RprApiSafeWrapper::RprApiSafeWrapper(RprApiSafeWrapper&& other) noexcept
    : m_renderParam(other.m_renderParam)
    , m_rprApi(other.m_rprApi) {
    // Transfer the existing edit ownership; a moved-from guard owns no count.
    other.m_renderParam = nullptr;
    other.m_rprApi = nullptr;
}

RprApiSafeWrapper::~RprApiSafeWrapper() {
    if (!m_renderParam) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_renderParam->m_threadControlMutex);
    if (--m_renderParam->m_editCount == 0) {
        m_renderParam->RestartRender();
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
