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

#include "pxr/usd/ndr/discoveryPlugin.h"
#include "pxr/imaging/rprUsd/materialRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdRprNdrDiscoveryPlugin
///
/// Enumerates materials from RprUsdMaterialRegistry. 
///
class HdRprNdrDiscoveryPlugin final : public NdrDiscoveryPlugin {
public:
    NdrNodeDiscoveryResultVec DiscoverNodes(const Context& ctx) override {
        static TfToken rpr("rpr", TfToken::Immortal);

        NdrNodeDiscoveryResultVec ret;
        for (auto& nodeDesc : RprUsdMaterialRegistry::GetInstance().GetRegisteredNodes()) {
            if (!nodeDesc.info) continue;

            ret.emplace_back(
                /* identifier    = */ TfToken(nodeDesc.info->GetName()),
                /* version       = */ NdrVersion(1),
                /* name          = */ nodeDesc.info->GetName(),
                /* family        = */ TfToken(nodeDesc.info->GetUIFolder()),
                /* discoveryType = */ rpr,
                /* sourceType    = */ rpr,
                /* uri           = */ std::string(),
                /* resolvedUri   = */ std::string(),
                /* sourceCode    = */ std::string(),
                /* metadata      = */ NdrTokenMap(),
                /* blindData     = */ std::string()
            );
        }
        return ret;
    }

    const NdrStringVec& GetSearchURIs() const override {
        static NdrStringVec s_searchURIs;
        return s_searchURIs;
    }
};

NDR_REGISTER_DISCOVERY_PLUGIN(HdRprNdrDiscoveryPlugin);

PXR_NAMESPACE_CLOSE_SCOPE
