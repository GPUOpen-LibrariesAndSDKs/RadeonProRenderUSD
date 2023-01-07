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

#include "lightRegistry.h"
#include "pxr/base/tf/instantiateSingleton.h"
#include <RadeonProRender.hpp>

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(RprUsdLightRegistry);

void RprUsdLightRegistry::Register(const SdfPath& id, rpr::Light* light) {
    RprUsdLightRegistry& self = RprUsdLightRegistry::GetInstance();
    auto rit = self.m_Registry.find(id.GetString());
    if (rit != self.m_Registry.end()) {
        (*rit).second = light;
    }
    else {
        self.m_Registry.emplace(id.GetString(), light);
    }
    
    auto cit = self.m_Clients.find(id.GetString());
    if (cit != self.m_Clients.end()) {
        for (const auto& clientAndCallback : (*cit).second) {
            clientAndCallback.second(light);
        }
    }
}

void RprUsdLightRegistry::Release(const SdfPath& id) {
    RprUsdLightRegistry& self = RprUsdLightRegistry::GetInstance();
    auto rit = self.m_Registry.find(id.GetString());
    if (rit != self.m_Registry.end()) {
        self.m_Registry.erase(rit);
    }

    auto cit = self.m_Clients.find(id.GetString());
    if (cit != self.m_Clients.end()) {
        for (const auto& clientAndCallback : (*cit).second) {
            clientAndCallback.second(nullptr);
        }
    }
}

rpr::Light* RprUsdLightRegistry::Get(const std::string& id, std::function<void(rpr::Light*)> callback, void* client) {
    RprUsdLightRegistry& self = RprUsdLightRegistry::GetInstance();

    auto cit = self.m_Clients.find(id);
    if (cit != self.m_Clients.end())
    {
        auto clientsAndCallbacks = (*cit).second;
        auto cbit = clientsAndCallbacks.find(client);
        if (cbit != clientsAndCallbacks.end()) {
            (*cbit).second = callback;
        }
        else {
            clientsAndCallbacks.emplace(client, callback);
        }
    }
    else {
        std::map<void*, std::function<void(rpr::Light*)>> clientsAndCallbacks;
        clientsAndCallbacks.emplace(client, callback);
        self.m_Clients.emplace(id, clientsAndCallbacks);
    }

    auto rit = self.m_Registry.find(id);
    return (rit != self.m_Registry.end()) ? (*rit).second : nullptr;
}

void RprUsdLightRegistry::ReleaseClient(void* client) {
    RprUsdLightRegistry& self = RprUsdLightRegistry::GetInstance();
    for (auto cit = self.m_Clients.begin(); cit != self.m_Clients.end(); ) {
        auto clientsAndCallbacks = (*cit).second;
        auto it = clientsAndCallbacks.find(client);
        if (it != clientsAndCallbacks.end()) {
            clientsAndCallbacks.erase(it);
        }
        
        if (clientsAndCallbacks.empty()) {
            cit = self.m_Clients.erase(cit);
        }
        else {
            ++cit;
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
