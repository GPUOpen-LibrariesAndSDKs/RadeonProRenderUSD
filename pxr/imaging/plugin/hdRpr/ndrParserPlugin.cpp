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
#include "pxr/usd/sdr/shaderNode.h"
#include "pxr/usd/sdr/parserPlugin.h"
#include "pxr/usd/sdr/shaderNodeDiscoveryResult.h"
#else
#include "pxr/usd/ndr/node.h"
#include "pxr/usd/ndr/parserPlugin.h"
#include "pxr/usd/ndr/nodeDiscoveryResult.h"
#endif

#include "pxr/base/tf/staticTokens.h"

#include "pxr/imaging/rprUsd/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdRprNdrParserPlugin
///
/// This does the minimal amount of work so Hydra will let us have our shaders.
///
#if RPR_USD_SDR_DISCOVERY
using RprParserBase = SdrParserPlugin;
using RprParserTypes = SdrTokenVec;
#else
using RprParserBase = NdrParserPlugin;
using RprParserTypes = NdrTokenVec;
#endif

class HdRprNdrParserPlugin final : public RprParserBase {
public:
#if RPR_USD_SDR_DISCOVERY
    SdrShaderNodeUniquePtr ParseShaderNode(const SdrShaderNodeDiscoveryResult& d) override {
        return std::make_unique<SdrShaderNode>(
            d.identifier, d.version, d.name, d.family,
            RprUsdTokens->rpr, RprUsdTokens->rpr, d.uri, d.resolvedUri,
            SdrShaderPropertyUniquePtrVec{}, d.metadata, d.sourceCode);
    }
#else
    NdrNodeUniquePtr Parse(const NdrNodeDiscoveryResult& discoveryResult) override {
        return std::make_unique<NdrNode>(
            /* identifier  = */ discoveryResult.identifier,
            /* version     = */ discoveryResult.version,
            /* name        = */ discoveryResult.name,
            /* family      = */ discoveryResult.family,
            /* context     = */ RprUsdTokens->rpr,
            /* sourceType  = */ RprUsdTokens->rpr,
            /* uri         = */ discoveryResult.uri,
#if PXR_VERSION > 1911
            /* resolvedUri = */ discoveryResult.resolvedUri,
#endif
            /* properties  = */ NdrPropertyUniquePtrVec{},
            /* metadata    = */ discoveryResult.metadata,
            /* sourceCode  = */ discoveryResult.sourceCode
        );
    }

#endif

    const RprParserTypes& GetDiscoveryTypes() const override {
        static RprParserTypes s_discoveryTypes{RprUsdTokens->rpr};
        return s_discoveryTypes;
    }

    const TfToken& GetSourceType() const override {
        return RprUsdTokens->rpr;
    }
};

#if RPR_USD_SDR_DISCOVERY
SDR_REGISTER_PARSER_PLUGIN(HdRprNdrParserPlugin);
#else
NDR_REGISTER_PARSER_PLUGIN(HdRprNdrParserPlugin);
#endif

PXR_NAMESPACE_CLOSE_SCOPE
