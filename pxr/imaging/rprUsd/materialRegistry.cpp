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

#include "pxr/imaging/rprUsd/materialRegistry.h"
#include "pxr/imaging/rprUsd/imageCache.h"
#include "pxr/imaging/rprUsd/debugCodes.h"
#include "pxr/imaging/rprUsd/material.h"
#include "pxr/base/tf/instantiateSingleton.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/getenv.h"

#include "materialNodes/usdNode.h"
#include "materialNodes/houdiniPrincipledShaderNode.h"

#include "materialNodes/mtlxNode.h"
#include <MaterialXFormat/XmlIo.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(RprUsdMaterialRegistry);

TF_DEFINE_ENV_SETTING(RPRUSD_MATERIAL_NETWORK_SELECTOR, "rpr",
        "Material network selector to be used in hdRpr");

RprUsdMaterialRegistry::RprUsdMaterialRegistry()
    : m_materialNetworkSelector(TfGetEnvSetting(RPRUSD_MATERIAL_NETWORK_SELECTOR)) {

}

std::vector<RprUsdMaterialNodeDesc> const&
RprUsdMaterialRegistry::GetRegisteredNodes() {
    if (m_mtlxDefsDirty) {
        m_mtlxDefsDirty = false;

        auto RPR = TfGetenv("RPR");
        if (RPR.empty()) {
            TF_WARN("RPR environment variable is not set");
            return m_registeredNodes;
        }

        auto rprMaterialsPath = TfAbsPath(TfNormPath(RPR + "/materials"));

        auto materialFiles = TfGlob(TfNormPath(rprMaterialsPath + "/*/*.mtlx"), ARCH_GLOB_DEFAULT | ARCH_GLOB_NOSORT);
        if (materialFiles.empty()) {
            TF_WARN("No materials found");
        }

        for (auto& file : materialFiles) {
            // UI Folder corresponds to subsections on UI
            // e.g. $RPR/Patterns/material.mtlx corresponds to Pattern UI folder
            auto uiFolder = file.substr(rprMaterialsPath.size() + 1);
            uiFolder = TfNormPath(TfGetPathName(uiFolder));
            if (uiFolder == ".") {
                uiFolder = std::string();
            }

            try {
                auto mtlxDoc = MaterialX::createDocument();
                MaterialX::readFromXmlFile(mtlxDoc, file);

                auto nodeDefs = mtlxDoc->getNodeDefs();
                if (nodeDefs.size() == 0) {
                    TF_WARN("\"%s\" file has no node definitions", file.c_str());
                } else {
                    for (auto& nodeDef : nodeDefs) {
                        auto shaderInfo = std::make_unique<RprUsd_MtlxNodeInfo>(mtlxDoc, nodeDef, uiFolder);
                        if (auto factory = shaderInfo->GetFactory()) {
                            Register(TfToken(shaderInfo->GetName()), factory, shaderInfo.get());
                            m_mtlxInfos.push_back(std::move(shaderInfo));
                        }
                    }
                }
            } catch (MaterialX::Exception& e) {
                TF_RUNTIME_ERROR("Error on parsing of \"%s\": materialX error - %s", file.c_str(), e.what());
            }
        }
    }

    return m_registeredNodes;
}

namespace {

void DumpMaterialNetwork(HdMaterialNetworkMap const& networkMap) {
    SdfPath const* primitivePath = nullptr;
    if (!networkMap.terminals.empty()) {
        primitivePath = &networkMap.terminals[0];
    } else if (!networkMap.map.empty()) {
        auto& network = networkMap.map.begin()->second;
        if (!network.nodes.empty()) {
            primitivePath = &network.nodes[0].path;
        }
    }

    bool closeFile = false;
    FILE* file = stdout;
    if (primitivePath) {
        auto materialPath = primitivePath->GetParentPath();
        std::string filepath = materialPath.GetString();
        for (size_t i = 0; i < filepath.size(); ++i) {
            if (std::strchr("/\\", filepath[i])) {
                filepath[i] = '_';
            }
        }
        file = fopen(filepath.c_str(), "w");
        if (!file) {
            file = stdout;
        } else {
            closeFile = true;
        }
    }

    fprintf(file, "terminals: [\n");
    for (auto& terminal : networkMap.terminals) {
        fprintf(file, "  \"%s\",\n", terminal.GetText());
    }
    fprintf(file, "]\n");

    fprintf(file, "map: {\n");
    for (auto& entry : networkMap.map) {
        fprintf(file, "  \"%s\": {\n", entry.first.GetText());

        auto& network = entry.second;
        fprintf(file, "    relationships: [\n");
        for (auto& rel : network.relationships) {
            SdfPath inputId;
            TfToken inputName;
            SdfPath outputId;
            TfToken outputName;
            fprintf(file, "      {\n");
            fprintf(file, "        inputId=%s\n", rel.inputId.GetText());
            fprintf(file, "        inputName=%s\n", rel.inputName.GetText());
            fprintf(file, "        outputId=%s\n", rel.outputId.GetText());
            fprintf(file, "        outputName=%s\n", rel.outputName.GetText());
            fprintf(file, "      },\n");
        }
        fprintf(file, "    ],\n");

        fprintf(file, "    primvars: [\n");
        for (auto& primvar : network.primvars) {
            fprintf(file, "      %s,\n", primvar.GetText());
        }
        fprintf(file, "    ]\n");

        fprintf(file, "    nodes: [\n");
        for (auto& node : network.nodes) {
            fprintf(file, "      {\n");
            fprintf(file, "        path=%s\n", node.path.GetText());
            fprintf(file, "        identifier=%s\n", node.identifier.GetText());
            fprintf(file, "        parameters: {\n");
            for (auto& param : node.parameters) {
                fprintf(file, "          {%s: %s},\n", param.first.GetText(), param.second.GetTypeName().c_str());
            }
            fprintf(file, "        }\n");
            fprintf(file, "      },\n");
        }
        fprintf(file, "    ]\n");

        fprintf(file, "  }\n");
    }
    fprintf(file, "}\n");

    if (closeFile) {
        fclose(file);
    }
}

// Structures are taken from hdSt/materialNetwork.cpp

struct RprUsd_MaterialNetwork {
    struct Connection {
        SdfPath upstreamNode;
        TfToken upstreamOutputName;
    };

    struct Node {
        TfToken nodeTypeId;
        std::map<TfToken, VtValue> parameters;
        std::map<TfToken, Connection> inputConnections;
    };

    std::map<SdfPath, Node> nodes;
    std::map<TfToken, Connection> terminals;
};

void ConvertLegacyHdMaterialNetwork(
    HdMaterialNetworkMap const& hdNetworkMap,
    RprUsd_MaterialNetwork *result) {

    for (auto& entry : hdNetworkMap.map) {
        auto& terminalName = entry.first;
        auto& hdNetwork = entry.second;

        // Transfer over individual nodes
        for (auto& node : hdNetwork.nodes) {
            // Check if this node is a terminal
            auto termIt = std::find(hdNetworkMap.terminals.begin(), hdNetworkMap.terminals.end(), node.path);
            if (termIt != hdNetworkMap.terminals.end()) {
                result->terminals.emplace(
                    terminalName,
                    RprUsd_MaterialNetwork::Connection{node.path, terminalName});
            }

            if (result->nodes.count(node.path)) {
                continue;
            }

            auto& newNode = result->nodes[node.path];
            newNode.nodeTypeId = node.identifier;
            newNode.parameters = node.parameters;
        }

        // Transfer relationships to inputConnections on receiving/downstream nodes.
        for (HdMaterialRelationship const& rel : hdNetwork.relationships) {
            // outputId (in hdMaterial terms) is the input of the receiving node
            auto const& iter = result->nodes.find(rel.outputId);
            // skip connection if the destination node doesn't exist
            if (iter == result->nodes.end()) {
                continue;
            }
            auto &connection = iter->second.inputConnections[rel.outputName];
            connection.upstreamNode = rel.inputId;
            connection.upstreamOutputName = rel.inputName;
        }

        // Currently unused
        // Transfer primvars:
        //result->primvars.insert(hdNetwork.primvars.begin(), hdNetwork.primvars.end());
    }
}

} // namespace anonymous

RprUsdMaterial* RprUsdMaterialRegistry::CreateMaterial(
    HdSceneDelegate* sceneDelegate,
    HdMaterialNetworkMap const& legacyNetworkMap,
    rpr::Context* rprContext,
    RprUsdImageCache* imageCache) const {

    if (TfDebug::IsEnabled(RPR_USD_DEBUG_DUMP_MATERIALS)) {
        DumpMaterialNetwork(legacyNetworkMap);
    }

    // HdMaterialNetworkMap is deprecated,
    // convert HdMaterialNetwork over to the new description
    // so we do not have to redo all the code when new description comes in Hd
    RprUsd_MaterialNetwork network;
    ConvertLegacyHdMaterialNetwork(legacyNetworkMap, &network);

    RprUsd_MaterialBuilderContext context = {};
    context.rprContext = rprContext;
    context.imageCache = imageCache;

    // The simple wrapper to retain material nodes that are used to build terminal outputs
    struct RprUsdGraphBasedMaterial : public RprUsdMaterial {
        std::map<SdfPath, std::unique_ptr<RprUsd_MaterialNode>> materialNodes;

        bool Finalize(RprUsd_MaterialBuilderContext& context,
            VtValue const& surfaceOutput,
            VtValue const& displacementOutput,
            VtValue const& volumeOutput) {

            auto getTerminalRprNode = [](VtValue const& terminalOutput) -> rpr::MaterialNode* {
                if (!terminalOutput.IsEmpty()) {
                    if (terminalOutput.IsHolding<std::shared_ptr<rpr::MaterialNode>>()) {
                        return terminalOutput.UncheckedGet<std::shared_ptr<rpr::MaterialNode>>().get();
                    } else {
                        TF_RUNTIME_ERROR("Terminal node should output material node");
                    }
                }

                return nullptr;
            };

            m_volumeNode = getTerminalRprNode(volumeOutput);
            m_surfaceNode = getTerminalRprNode(surfaceOutput);
            m_displacementNode = getTerminalRprNode(displacementOutput);

            m_isShadowCatcher = context.isShadowCatcher;
            m_isReflectionCatcher = context.isReflectionCatcher;
            m_uvPrimvarName = std::move(context.uvPrimvarName);
            m_displacementScale = std::move(context.displacementScale);

            return m_volumeNode || m_surfaceNode || m_displacementNode;
        }
    };

    auto out = std::make_unique<RprUsdGraphBasedMaterial>();

    // Houdini's principled shader node does not have a valid nodeTypeId
    // So we find both surface and displacement nodes and then create one material node
    bool isSurfaceNode;
    SdfPath const* houdiniPrincipledShaderNodePath = nullptr;
    std::map<TfToken, VtValue> const* houdiniPrincipledShaderSurfaceParams = nullptr;
    std::map<TfToken, VtValue> const* houdiniPrincipledShaderDispParams = nullptr;

    // Create RprUsd_MaterialNode for each Hydra node
    auto& materialNodes = out->materialNodes;
    for (auto& entry : network.nodes) {
        auto& nodePath = entry.first;
        auto& node = entry.second;

        try {
            // Check if we have registered node that match nodeTypeId
            auto nodeLookupIt = m_registeredNodesLookup.find(node.nodeTypeId);
            if (nodeLookupIt != m_registeredNodesLookup.end()) {
                if (auto materialNode = m_registeredNodes[nodeLookupIt->second].factory(&context, node.parameters)) {
                    materialNodes[nodePath].reset(materialNode);
                }
            } else if (IsHoudiniPrincipledShaderHydraNode(sceneDelegate, nodePath, &isSurfaceNode)) {
                if (isSurfaceNode) {
                    houdiniPrincipledShaderNodePath = &nodePath;
                    houdiniPrincipledShaderSurfaceParams = &node.parameters;
                } else {
                    houdiniPrincipledShaderDispParams = &node.parameters;
                }
            } else {
                TF_WARN("Unknown node type: id=%s", node.nodeTypeId.GetText());
            }
        } catch (RprUsd_NodeError& e) {
            TF_RUNTIME_ERROR("Failed to create %s(%s): %s", nodePath.GetText(), node.nodeTypeId.GetText(), e.what());
        } catch (RprUsd_NodeEmpty&) {
            TF_WARN("Empty node: %s", nodePath.GetText());
        }
    }

    if (houdiniPrincipledShaderNodePath) {
        auto materialNode = new RprUsd_HoudiniPrincipledNode(&context, *houdiniPrincipledShaderSurfaceParams, houdiniPrincipledShaderDispParams);
        materialNodes[*houdiniPrincipledShaderNodePath].reset(materialNode);
    }

    std::set<SdfPath> visited;
    std::function<VtValue(RprUsd_MaterialNetwork::Connection const&)> getNodeOutput =
        [&materialNodes, &network, &getNodeOutput, &visited]
        (RprUsd_MaterialNetwork::Connection const& nodeConnection) -> VtValue {
        auto& nodePath = nodeConnection.upstreamNode;

        auto nodeIt = network.nodes.find(nodePath);
        if (nodeIt == network.nodes.end()) {
            TF_CODING_ERROR("Invalid connection: %s", nodePath.GetText());
            return VtValue();
        }
        auto& node = nodeIt->second;

        auto materialNodeIt = materialNodes.find(nodePath);
        if (materialNodeIt != materialNodes.end()) {
            auto materialNode = materialNodeIt->second.get();

            // Set node inputs only once
            if (visited.count(nodePath) == 0) {
                visited.insert(nodePath);

                for (auto& inputConnection : node.inputConnections) {
                    auto& connection = inputConnection.second;

                    auto nodeOutput = getNodeOutput(connection);
                    if (!nodeOutput.IsEmpty()) {
                        auto& inputId = inputConnection.first;
                        materialNode->SetInput(inputId, nodeOutput);
                    }
                }
            }

            return materialNode->GetOutput(nodeConnection.upstreamOutputName);
        } else {
            // Rpr node can be missing in two cases:
            //   a) we failed to create the node
            //   b) this node has no effect on the input
            // In such a case, we simply interpret the output of the
            // first connection as the output of the current node
            if (node.inputConnections.empty()) {
                return VtValue();
            } else {
                return getNodeOutput(node.inputConnections.begin()->second);
            }
        }
    };

    auto getTerminalOutput = [&network, &getNodeOutput](TfToken const& terminalName) {
        auto terminalIt = network.terminals.find(terminalName);
        if (terminalIt == network.terminals.end()) {
            return VtValue();
        }

        return getNodeOutput(terminalIt->second);
    };

    auto volumeOutput = getTerminalOutput(HdMaterialTerminalTokens->volume);
    auto surfaceOutput = getTerminalOutput(HdMaterialTerminalTokens->surface);
    auto displacementOutput = getTerminalOutput(HdMaterialTerminalTokens->displacement);

    return out->Finalize(context, surfaceOutput, displacementOutput, volumeOutput) ? out.release() : nullptr;
}

PXR_NAMESPACE_CLOSE_SCOPE