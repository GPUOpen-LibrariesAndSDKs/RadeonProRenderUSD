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

#ifndef RPRUSD_LIGHT_REGISTRY_H
#define RPRUSD_LIGHT_REGISTRY_H

#include <map>
#include <functional>
#include "pxr/base/tf/singleton.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/imaging/rprUsd/api.h"

namespace rpr {
    class Light;
}

PXR_NAMESPACE_OPEN_SCOPE

/// \class RprUsdLightRegistry
///
/// Stores connection between lights in the scene and their USD paths
/// Also stores callbacks to update its clients when the light is changed
///
class RprUsdLightRegistry {
public:
    RPRUSD_API
    static void Register(const SdfPath& id, rpr::Light* light);

    RPRUSD_API
    static void Release(const SdfPath& id);

    static rpr::Light* Get(const std::string& id, std::function<void(rpr::Light*)> callback, void* client);
    static void ReleaseClient(void* client);
private:
    RprUsdLightRegistry() = default;
    ~RprUsdLightRegistry() = default;
    RprUsdLightRegistry(RprUsdLightRegistry const&) = delete;
    RprUsdLightRegistry& operator=(RprUsdLightRegistry const&) = delete;
    RprUsdLightRegistry(RprUsdLightRegistry&&) = delete;
    RprUsdLightRegistry& operator=(RprUsdLightRegistry&&) = delete;

    static RprUsdLightRegistry& GetInstance() {
        return TfSingleton<RprUsdLightRegistry>::GetInstance();
    }

    std::map<std::string, rpr::Light*> m_Registry;                                      // Light path / pointer to the light
    std::map<std::string, std::map<void*, std::function<void(rpr::Light*)>>> m_Clients; // Light path / (client / light update callback)
    friend class TfSingleton<RprUsdLightRegistry>;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_REGISTRY_H
