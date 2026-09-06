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

#include "pxr/imaging/rprUsd/apiCapabilities.h"

#if RPR_USD_SDR_DISCOVERY
#include "pxr/usd/sdr/discoveryPlugin.h"
#else
#include "pxr/usd/ndr/discoveryPlugin.h"
#endif
#include "pxr/imaging/rprUsd/materialRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdRprNdrDiscoveryPlugin
///
/// Enumerates materials from RprUsdMaterialRegistry. 
///
#if RPR_USD_SDR_DISCOVERY
using RprDiscoveryBase = SdrDiscoveryPlugin;
using RprDiscoveryResults = SdrShaderNodeDiscoveryResultVec;
using RprDiscoveryVersion = SdrVersion;
using RprDiscoveryMetadata = SdrTokenMap;
using RprSearchURIs = SdrStringVec;
#else
using RprDiscoveryBase = NdrDiscoveryPlugin;
using RprDiscoveryResults = NdrNodeDiscoveryResultVec;
using RprDiscoveryVersion = NdrVersion;
using RprDiscoveryMetadata = NdrTokenMap;
using RprSearchURIs = NdrStringVec;
#endif

class HdRprNdrDiscoveryPlugin final : public RprDiscoveryBase {
public:
#if RPR_USD_SDR_DISCOVERY
    RprDiscoveryResults DiscoverShaderNodes(const Context& ctx) override {
#else
    RprDiscoveryResults DiscoverNodes(const Context& ctx) override {
#endif
        static TfToken rpr("rpr", TfToken::Immortal);

        RprDiscoveryResults ret;
        for (auto& nodeDesc : RprUsdMaterialRegistry::GetInstance().GetRegisteredNodes()) {
            if (!nodeDesc.info) continue;

            ret.emplace_back(
                /* identifier    = */ TfToken(nodeDesc.info->GetName()),
                /* version       = */ RprDiscoveryVersion(1),
                /* name          = */ nodeDesc.info->GetName(),
                /* family        = */ TfToken(nodeDesc.info->GetUIFolder()),
                /* discoveryType = */ rpr,
                /* sourceType    = */ rpr,
                /* uri           = */ std::string(),
                /* resolvedUri   = */ std::string(),
                /* sourceCode    = */ std::string(),
                /* metadata      = */ RprDiscoveryMetadata(),
                /* blindData     = */ std::string()
            );
        }
        return ret;
    }

    const RprSearchURIs& GetSearchURIs() const override {
        static RprSearchURIs s_searchURIs;
        return s_searchURIs;
    }
};

#if RPR_USD_SDR_DISCOVERY
SDR_REGISTER_DISCOVERY_PLUGIN(HdRprNdrDiscoveryPlugin);
#else
NDR_REGISTER_DISCOVERY_PLUGIN(HdRprNdrDiscoveryPlugin);
#endif

PXR_NAMESPACE_CLOSE_SCOPE
