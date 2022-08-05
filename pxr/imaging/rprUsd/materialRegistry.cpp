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

#include "pxr/imaging/rprUsd/util.h"
#include "pxr/imaging/rprUsd/materialRegistry.h"
#include "pxr/imaging/rprUsd/imageCache.h"
#include "pxr/imaging/rprUsd/debugCodes.h"
#include "pxr/imaging/rprUsd/material.h"
#include "pxr/imaging/rprUsd/tokens.h"
#include "pxr/imaging/rprUsd/error.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/thisPlugin.h"
#include "pxr/base/arch/vsnprintf.h"
#include "pxr/base/tf/instantiateSingleton.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/getenv.h"
#include "pxr/base/work/loops.h"
#include "pxr/usd/sdr/registry.h"
#include "pxr/usd/usd/schemaBase.h"
#include "pxr/usd/usdShade/tokens.h"
#include "pxr/imaging/hd/sceneDelegate.h"

#include "materialNodes/usdNode.h"
#include "materialNodes/mtlxNode.h"
#include "materialNodes/rprApiMtlxNode.h"
#include "materialNodes/houdiniPrincipledShaderNode.h"

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/Util.h>
namespace mx = MaterialX;

#ifdef USE_CUSTOM_MATERIALX_LOADER
#include <MaterialXFormat/XmlIo.h>
#include <rprMtlxLoader.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

#ifdef USE_USDSHADE_MTLX
mx::DocumentPtr
HdMtlxCreateMtlxDocumentFromHdNetwork_Fixed(
    HdMaterialNetwork2 const& hdNetwork,
    HdMaterialNode2 const& hdMaterialXNode,
    SdfPath const& materialPath,
    mx::DocumentPtr const& libraries,
    std::set<SdfPath>* hdTextureNodes,
    mx::StringMap* mxHdTextureMap);
#else
void RprUsd_MaterialNetworkFromHdMaterialNetworkMap(
    HdMaterialNetworkMap const& hdNetworkMap,
    RprUsd_MaterialNetwork& result,
    bool* isVolume) {
    for (auto& entry : hdNetworkMap.map) {
        auto& terminalName = entry.first;
        auto& hdNetwork = entry.second;

        // Transfer over individual nodes
        for (auto& node : hdNetwork.nodes) {
            // Check if this node is a terminal
            auto termIt = std::find(hdNetworkMap.terminals.begin(), hdNetworkMap.terminals.end(), node.path);
            if (termIt != hdNetworkMap.terminals.end()) {
                result.terminals.emplace(
                    terminalName,
                    RprUsd_MaterialNetworkConnection{ node.path, terminalName });
            }

            if (result.nodes.count(node.path)) {
                continue;
            }

            auto& newNode = result.nodes[node.path];
            newNode.nodeTypeId = node.identifier;
            newNode.parameters = node.parameters;
        }

        // Transfer relationships to inputConnections on receiving/downstream nodes.
        for (HdMaterialRelationship const& rel : hdNetwork.relationships) {
            // outputId (in hdMaterial terms) is the input of the receiving node
            auto const& iter = result.nodes.find(rel.outputId);
            // skip connection if the destination node doesn't exist
            if (iter == result.nodes.end()) {
                continue;
            }
            auto &connections = iter->second.inputConnections[rel.outputName];
            connections.push_back(RprUsd_MaterialNetworkConnection{rel.inputId, rel.inputName});
        }

        // Currently unused
        // Transfer primvars:
        //result.primvars.insert(hdNetwork.primvars.begin(), hdNetwork.primvars.end());
    }
}
#endif // USE_USDSHADE_MTLX

TF_INSTANTIATE_SINGLETON(RprUsdMaterialRegistry);

TF_DEFINE_ENV_SETTING(RPRUSD_MATERIAL_NETWORK_SELECTOR, "rpr",
    "Material network selector to be used in hdRpr");

#ifdef USE_CUSTOM_MATERIALX_LOADER
TF_DEFINE_ENV_SETTING(RPRUSD_USE_RPRMTLXLOADER, true,
    "Whether to use RPRMtlxLoader or rprLoadMateriaX");
TF_DEFINE_ENV_SETTING(RPRUSD_RPRMTLXLOADER_LOG_LEVEL, int(RPRMtlxLoader::LogLevel::Error),
    "Set logging level of RPRMtlxLoader");
#endif // USE_CUSTOM_MATERIALX_LOADER

TF_DEFINE_PRIVATE_TOKENS(_tokens, (mtlx));

RprUsdMaterialRegistry::RprUsdMaterialRegistry()
    : m_materialNetworkSelector(TfGetEnvSetting(RPRUSD_MATERIAL_NETWORK_SELECTOR)) {

}

RprUsdMaterialRegistry::~RprUsdMaterialRegistry() = default;

std::vector<RprUsdMaterialNodeDesc> const&
RprUsdMaterialRegistry::GetRegisteredNodes() {
    if (m_mtlxDefsDirty) {
        m_mtlxDefsDirty = false;

        std::string mtlxResources = PlugFindPluginResource(PLUG_THIS_PLUGIN, "mtlx");
        if (mtlxResources.empty()) {
            TF_RUNTIME_ERROR("Missing mtlx resources");
            return m_registeredNodes;
        }
        TF_DEBUG(RPR_USD_DEBUG_MATERIAL_REGISTRY).Msg("mtlx resources: %s\n", mtlxResources.c_str());

        auto rprMaterialsPath = TfAbsPath(TfNormPath(mtlxResources + "/materials"));

        auto materialFiles = TfGlob(TfNormPath(rprMaterialsPath + "/*/*.mtlx"), ARCH_GLOB_DEFAULT | ARCH_GLOB_NOSORT);
        if (materialFiles.empty()) {
            TF_WARN("No materials found");
        }

        for (auto& file : materialFiles) {
            TF_DEBUG(RPR_USD_DEBUG_MATERIAL_REGISTRY).Msg("Processing material: \"%s\"\n", file.c_str());

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

#ifdef USE_CUSTOM_MATERIALX_LOADER
        if (TfGetEnvSetting(RPRUSD_USE_RPRMTLXLOADER)) {
            MaterialX::FilePathVec libraryNames = { "libraries", "materials" };
            MaterialX::FileSearchPath searchPath = mtlxResources;
            m_mtlxLoader = std::make_unique<RPRMtlxLoader>();
            m_mtlxLoader->SetupStdlib(libraryNames, searchPath);

            auto logLevel = RPRMtlxLoader::LogLevel(TfGetEnvSetting(RPRUSD_RPRMTLXLOADER_LOG_LEVEL));
            if (logLevel < RPRMtlxLoader::LogLevel::None ||
                logLevel > RPRMtlxLoader::LogLevel::Info) {
                logLevel = RPRMtlxLoader::LogLevel::Error;
            }
            m_mtlxLoader->SetLogging(logLevel);
        }
#endif // USE_CUSTOM_MATERIALX_LOADER
    }

    return m_registeredNodes;
}

void RprUsdMaterialRegistry::EnqueueTextureLoadRequest(std::weak_ptr<TextureLoadRequest> textureLoadRequest) {
    m_textureLoadRequests.push_back(std::move(textureLoadRequest));
}

void RprUsdMaterialRegistry::CommitResources(
    RprUsdImageCache* imageCache) {

    std::vector<std::shared_ptr<TextureLoadRequest>> textureLoadRequests;
    textureLoadRequests.reserve(m_textureLoadRequests.size());
    for (std::weak_ptr<TextureLoadRequest>& requestHandle : m_textureLoadRequests) {
        if (std::shared_ptr<TextureLoadRequest> request = requestHandle.lock()) {
            textureLoadRequests.push_back(std::move(request));
        }
    }
    m_textureLoadRequests.clear();

    if (textureLoadRequests.empty()) {
        return;
    }

    using LoadRequestUniqueTextureIndices = std::vector<size_t>;
    auto uniqueTextureIndicesPerLoadRequest = std::make_unique<LoadRequestUniqueTextureIndices[]>(textureLoadRequests.size());

    struct UniqueTextureInfo {
        std::string path;
        uint32_t udimTileId;

        RprUsdTextureDataRefPtr data;

        UniqueTextureInfo(std::string const& path, uint32_t udimTileId)
            : path(path), udimTileId(udimTileId), data(nullptr) {}
    };
    std::vector<UniqueTextureInfo> uniqueTextures;
    std::map<std::string, size_t> uniqueTexturesMapping;
    auto getUniqueTextureIndex = [&uniqueTexturesMapping, &uniqueTextures](std::string const& path, uint32_t udimTileId = 0) {
        auto status = uniqueTexturesMapping.emplace(path, uniqueTexturesMapping.size());
        if (status.second) {
            uniqueTextures.emplace_back(path, udimTileId);
        }
        return status.first->second;
    };

    // Iterate over all texture load requests and collect unique textures including UDIM tiles
    //
    std::string formatString;
    for (size_t i = 0; i < textureLoadRequests.size(); ++i) {
        auto& loadRequest = textureLoadRequests[i];
        if (auto rprImage = imageCache->GetImage(loadRequest->filepath, loadRequest->colorspace, loadRequest->wrapType, {}, 0)) {
            loadRequest->onDidLoadTexture(rprImage);
            continue;
        }

        auto& loadRequestTexIndices = uniqueTextureIndicesPerLoadRequest[i];

        if (RprUsdGetUDIMFormatString(loadRequest->filepath, &formatString)) {
            constexpr uint32_t kStartTile = 1001;
            constexpr uint32_t kEndTile = 1100;

            for (uint32_t tileId = kStartTile; tileId <= kEndTile; ++tileId) {
                auto tilePath = TfStringPrintf(formatString.c_str(), tileId);
                if (ArchFileAccess(tilePath.c_str(), F_OK) == 0) {
                    loadRequestTexIndices.push_back(getUniqueTextureIndex(tilePath, tileId));
                }
            }
        } else {
            loadRequestTexIndices.push_back(getUniqueTextureIndex(loadRequest->filepath));
        }
    }

    // Read all textures from disk from multi threads
    //
    WorkParallelForN(uniqueTextures.size(),
        [&uniqueTextures](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (auto textureData = RprUsdTextureData::New(uniqueTextures[i].path)) {
                    uniqueTextures[i].data = textureData;
                } else {
                    TF_RUNTIME_ERROR("Failed to load %s texture", uniqueTextures[i].path.c_str());
                }
            }
        }
    );

    // Create rpr::Image for each previously read unique texture
    // XXX(RPR): so as RPR API is single-threaded we cannot parallelize this
    //
    for (size_t i = 0; i < textureLoadRequests.size(); ++i) {
        auto& loadRequestTexIndices = uniqueTextureIndicesPerLoadRequest[i];
        if (loadRequestTexIndices.empty()) continue;

        std::vector<RprUsdCoreImage::UDIMTile> tiles;
        tiles.reserve(loadRequestTexIndices.size());
        for (auto uniqueTextureIdx : loadRequestTexIndices) {
            auto& texture = uniqueTextures[uniqueTextureIdx];
            if (!texture.data) continue;

            tiles.emplace_back(texture.udimTileId, texture.data.operator->());
        }

        auto& loadRequest = textureLoadRequests[i];
        auto coreImage = imageCache->GetImage(loadRequest->filepath, loadRequest->colorspace, loadRequest->wrapType, tiles, loadRequest->numComponentsRequired);
        loadRequest->onDidLoadTexture(coreImage);
    }
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
                fprintf(file, "          {%s: %s", param.first.GetText(), param.second.GetTypeName().c_str());
                if (param.second.IsHolding<TfToken>()) {
                    fprintf(file, "(\"%s\")", param.second.UncheckedGet<TfToken>().GetText());
                } else if (param.second.IsHolding<SdfAssetPath>()) {
                    fprintf(file, "(\"%s\")", param.second.UncheckedGet<SdfAssetPath>().GetResolvedPath().c_str());
                } else if (param.second.IsHolding<GfVec4f>()) {
                    auto& v = param.second.UncheckedGet<GfVec4f>();
                    fprintf(file, "(%g, %g, %g, %g)", v[0], v[1], v[2], v[3]);
                }
                fprintf(file, "},\n");
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

RprUsdMaterial* CreateMaterialXFromUsdShade(
    SdfPath const& materialPath,
    RprUsd_MaterialBuilderContext const& context,
    std::string* materialXStdlibPath) {
#ifdef USE_USDSHADE_MTLX
    auto terminalIt = context.materialNetwork->terminals.find(UsdShadeTokens->surface);
    if (terminalIt == context.materialNetwork->terminals.end()) {
        return nullptr;
    }
    RprUsd_MaterialNetworkConnection const& nodeConnection = terminalIt->second;

    SdfPath const& nodePath = nodeConnection.upstreamNode;
    auto nodeIt = context.materialNetwork->nodes.find(nodePath);
    if (nodeIt == context.materialNetwork->nodes.end()) {
        return nullptr;
    }

    HdMaterialNode2 const& terminalNode = nodeIt->second;

    SdrRegistry &sdrRegistry = SdrRegistry::GetInstance();
    SdrShaderNodeConstPtr const mtlxSdrNode = sdrRegistry.GetShaderNodeByIdentifierAndType(terminalNode.nodeTypeId, _tokens->mtlx);

    if (!mtlxSdrNode) {
        return nullptr;
    }

    if (materialXStdlibPath->empty()) {
        const TfType schemaBaseType = TfType::Find<UsdSchemaBase>();
        PlugPluginPtr usdPlugin = PlugRegistry::GetInstance().GetPluginForType(schemaBaseType);
        if (usdPlugin) {
            std::string usdLibPath = usdPlugin->GetPath();
            std::string usdDir = TfNormPath(TfGetPathName(usdLibPath) + "..");
            *materialXStdlibPath = usdDir;
        }
    }
    
    mx::DocumentPtr stdLibraries = mx::createDocument();

    if (!materialXStdlibPath->empty()) {
        mx::FilePathVec libraryFolders = {"libraries"};
        mx::FileSearchPath searchPath;
        searchPath.append(mx::FilePath(*materialXStdlibPath));
        mx::loadLibraries(libraryFolders, searchPath, stdLibraries);
    }

    MaterialX::StringMap textureMap;
    std::set<SdfPath> hdTextureNodes;
    mx::DocumentPtr mtlxDoc;
    try {
        mtlxDoc = HdMtlxCreateMtlxDocumentFromHdNetwork_Fixed(
            *context.materialNetwork,
            terminalNode,
            materialPath,
            stdLibraries,
            &hdTextureNodes,
            &textureMap);
    } catch (MaterialX::Exception& e) {
        TF_RUNTIME_ERROR("Failed to convert HdNetwork to MaterialX document: %s", e.what());
        return nullptr;
    }

    std::string mtlxString = mx::writeToXmlString(mtlxDoc, nullptr);
    rpr::MaterialNode* mtlxNode = RprUsd_CreateRprMtlxFromString(mtlxString, context);
    if (!mtlxNode) {
        return nullptr;
    }

    struct RprUsdMaterial_RprApiMtlx : public RprUsdMaterial {
        RprUsdMaterial_RprApiMtlx(rpr::MaterialNode* retainedNode)
            : m_retainedNode(retainedNode) {
            // TODO: fill m_uvPrimvarName
            m_surfaceNode = m_retainedNode.get();
        }

        std::unique_ptr<rpr::MaterialNode> m_retainedNode;
    };
    return new RprUsdMaterial_RprApiMtlx(mtlxNode);
#else
    return nullptr;
#endif // USE_USDSHADE_MTLX
}

} // namespace anonymous

RprUsdMaterial* RprUsdMaterialRegistry::CreateMaterial(
    SdfPath const& materialId,
    HdSceneDelegate* sceneDelegate,
    HdMaterialNetworkMap const& legacyNetworkMap,
    rpr::Context* rprContext,
    RprUsdImageCache* imageCache) {

    if (TfDebug::IsEnabled(RPR_USD_DEBUG_DUMP_MATERIALS)) {
        DumpMaterialNetwork(legacyNetworkMap);
    }

    bool isVolume = false;
    RprUsd_MaterialNetwork network;
    RprUsd_MaterialNetworkFromHdMaterialNetworkMap(legacyNetworkMap, network, &isVolume);

    // HdMaterialNetwork2ConvertFromHdMaterialNetworkMap leaves terminal's upstreamOutputName empty,
    // material graph traversing logic relies on the fact that all upstreamOutputName are valid.
    for (auto& entry : network.terminals) {
        entry.second.upstreamOutputName = entry.first;
    }

    RprUsd_MaterialBuilderContext context = {};
    context.materialNetwork = &network;
    context.rprContext = rprContext;
    context.imageCache = imageCache;
#ifdef USE_CUSTOM_MATERIALX_LOADER
    context.mtlxLoader = m_mtlxLoader.get();
#endif // USE_CUSTOM_MATERIALX_LOADER

    if (!isVolume) {
        if (auto usdShadeMtlxMaterial = CreateMaterialXFromUsdShade(materialId, context, &m_materialXStdlibPath)) {
            return usdShadeMtlxMaterial;
        }
    }

    // The simple wrapper to retain material nodes that are used to build terminal outputs
    struct RprUsdGraphBasedMaterial : public RprUsdMaterial {
        std::map<SdfPath, std::unique_ptr<RprUsd_MaterialNode>> materialNodes;

        bool Finalize(RprUsd_MaterialBuilderContext& context,
            VtValue const& surfaceOutput,
            VtValue const& displacementOutput,
            VtValue const& volumeOutput,
            const char* cryptomatteName,
            int materialId) {

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
            m_uvPrimvarName = TfToken(context.uvPrimvarName);
            m_displacementScale = std::move(context.displacementScale);

            if (m_surfaceNode) {
                if (materialId >= 0) {
                    // TODO: add C++ wrapper
                    auto apiHandle = rpr::GetRprObject(m_surfaceNode);
                    RPR_ERROR_CHECK(rprMaterialNodeSetID(apiHandle, rpr_uint(materialId)), "Failed to set material node id");
                }

                RPR_ERROR_CHECK(m_surfaceNode->SetName(cryptomatteName), "Failed to set material name");
            }

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
        context.currentNodePath = &nodePath;

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
    std::function<VtValue(RprUsd_MaterialNetworkConnection const&)> getNodeOutput =
        [&materialNodes, &network, &getNodeOutput, &visited]
        (RprUsd_MaterialNetworkConnection const& nodeConnection) -> VtValue {
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
                    auto& connections = inputConnection.second;
                    if (connections.size() != 1) {
                        if (connections.size() > 1) {
                            TF_RUNTIME_ERROR("Connected array elements are not supported. Please report this.");
                        }
                        continue;
                    }
                    RprUsd_MaterialNetworkConnection const& connection = connections[0];

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
                auto& connections = node.inputConnections.begin()->second;
                if (connections.size() != 1) {
                    if (connections.size() > 1) {
                        TF_RUNTIME_ERROR("Connected array elements are not supported. Please report this.");
                    }
                    return VtValue();
                }
                return getNodeOutput(connections[0]);
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

    auto volumeOutput = getTerminalOutput(UsdShadeTokens->volume);
    auto surfaceOutput = getTerminalOutput(UsdShadeTokens->surface);
    auto displacementOutput = getTerminalOutput(UsdShadeTokens->displacement);

    int materialRprId = sceneDelegate->GetLightParamValue(materialId, RprUsdTokens->rprMaterialId).GetWithDefault(-1);
    std::string cryptomatteName = sceneDelegate->GetLightParamValue(materialId, RprUsdTokens->rprMaterialAssetName).GetWithDefault(std::string{});
    if (cryptomatteName.empty()) {
        cryptomatteName = materialId.GetString();
    }

    if (out->Finalize(context, surfaceOutput, displacementOutput, volumeOutput, cryptomatteName.c_str(), materialRprId)) {
        return out.release();
    }

    return nullptr;
}

PXR_NAMESPACE_CLOSE_SCOPE
