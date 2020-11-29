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

#include "materialXNode.h"
#include "baseNode.h"
#include "nodeInfo.h"

#include "pxr/usd/sdf/assetPath.h"
#include "pxr/base/arch/attributes.h"
#include "pxr/imaging/rprUsd/error.h"
#include "pxr/imaging/rprUsd/coreImage.h"

#include <fstream>

#include <rprMtlxLoader.h>
#include <MaterialXFormat/XmlIo.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(RprUsdRprMaterialXNodeTokens, RPRUSD_RPR_MATERIALX_NODE_TOKENS);

static rpr_material_node ReleaseOutputNodeOwnership(RPRMtlxLoader::Result* mtlx, RPRMtlxLoader::OutputType outputType) {
    auto idx = mtlx->rootNodeIndices[outputType];
    auto ret = mtlx->nodes[idx];
    mtlx->nodes[idx] = nullptr;
    return ret;
}

template <typename T>
static bool ReadInput(TfToken const& inputId, VtValue const& inputValue, T* dst) {
    if (inputValue.IsHolding<T>()) {
        *dst = inputValue.UncheckedGet<T>();
        return true;
    } else {
        TF_RUNTIME_ERROR("[%s] %s input should be of %s type: %s",
            RprUsdRprMaterialXNodeTokens->rpr_materialx_node.GetText(),
            inputId.GetText(), ArchGetDemangled<T>().c_str(),
            inputValue.GetTypeName().c_str());
        return false;
    }
}

class RprUsd_RprMaterialXNode : public RprUsd_MaterialNode {
public:
    RprUsd_RprMaterialXNode(RprUsd_MaterialBuilderContext* ctx)
        : m_ctx(ctx) {

    }

    ~RprUsd_RprMaterialXNode() override = default;

    VtValue GetOutput(TfToken const& outputId) override {
        if (m_isDirty) {
            m_isDirty = false;

            UpdateNodeOutput();
        }

        if (outputId == HdMaterialTerminalTokens->surface) {
            if (m_surfaceNode) {
                return VtValue(m_surfaceNode);
            }
        } else if (outputId == HdMaterialTerminalTokens->displacement) {
            if (m_displacementNode) {
                return VtValue(m_displacementNode);
            }
        }

        return VtValue();
    }

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value) override {
        if (inputId == RprUsdRprMaterialXNodeTokens->file) {
            if (value.IsHolding<SdfAssetPath>()) {
                auto& assetPath = value.UncheckedGet<SdfAssetPath>();

                m_mtlxFilepath = assetPath.GetResolvedPath();
                ResetNodeOutput();

                return true;
            } else {
                TF_RUNTIME_ERROR("[%s] file input should be of SdfAssetPath type: %s",
                    RprUsdRprMaterialXNodeTokens->rpr_materialx_node.GetText(), value.GetTypeName().c_str());
                return false;
            }
        } else if (inputId == RprUsdRprMaterialXNodeTokens->string) {
            bool ret = ReadInput(inputId, value, &m_mtlxString);
            if (ret) { ResetNodeOutput(); }
            return ret;
        } else if (inputId == RprUsdRprMaterialXNodeTokens->basePath) {
            bool ret = ReadInput(inputId, value, &m_mtlxBasePath);
            if (ret) { ResetNodeOutput(); }
            return ret;
        } else if (inputId == RprUsdRprMaterialXNodeTokens->surfaceElement) {
            return SetRenderElement(RPRMtlxLoader::kOutputSurface, value);
        } else if (inputId == RprUsdRprMaterialXNodeTokens->displacementElement) {
            return SetRenderElement(RPRMtlxLoader::kOutputDisplacement, value);
        } else if (inputId == RprUsdRprMaterialXNodeTokens->stPrimvarName) {
            return ReadInput(inputId, value, &m_ctx->uvPrimvarName);
        }

        TF_RUNTIME_ERROR("[%s] Unknown input %s",
            RprUsdRprMaterialXNodeTokens->rpr_materialx_node.GetText(), inputId.GetText());
        return false;
    }

    bool SetRenderElement(RPRMtlxLoader::OutputType outputType, VtValue const& value) {
        if (!value.IsHolding<std::string>()) {
            TF_RUNTIME_ERROR("[rpr_materialx_node] Invalid type of render element: %s",
                value.GetTypeName().c_str());
            return false;
        }

        auto& namePath = value.UncheckedGet<std::string>();
        if (m_selectedRenderElements[outputType] != namePath) {
            m_selectedRenderElements[outputType] = namePath;

            ResetNodeOutput();
        }

        return true;
    }

    void ResetNodeOutput() {
        m_isDirty = true;
        m_surfaceNode.reset();
        m_displacementNode.reset();
    }

    bool UpdateNodeOutput() {
        auto basePath = !m_mtlxBasePath.empty() ? m_mtlxBasePath : TfGetPathName(m_mtlxFilepath);
        if (basePath.empty()) {
            TF_WARN("[rpr_materialx_node] no base path specified, image loading might be broken");
        }

        if (m_ctx->mtlxLoader) {
            RPRMtlxLoader::Result mtlx;
            try {
                auto mtlxDoc = MaterialX::createDocument();
                if (!m_mtlxFilepath.empty()) {
                    MaterialX::readFromXmlFile(mtlxDoc, m_mtlxFilepath);
                }
                if (!m_mtlxString.empty()) {
                    MaterialX::readFromXmlString(mtlxDoc, m_mtlxString);
                }
                mtlxDoc->importLibrary(m_ctx->mtlxLoader->GetStdlib());

                rpr_material_system matSys;
                if (RPR_ERROR_CHECK(m_ctx->rprContext->GetInfo(RPR_CONTEXT_LIST_CREATED_MATERIALSYSTEM, sizeof(matSys), &matSys, nullptr), "Failed to get rpr material system")) {
                    return false;
                }

                bool hasAnySelectedElement = std::any_of(std::begin(m_selectedRenderElements), std::end(m_selectedRenderElements),
                    [](std::string const& elem) {
                        return !elem.empty();
                    }
                );
                std::string* selectedElements = hasAnySelectedElement ? m_selectedRenderElements : nullptr;

                MaterialX::FileSearchPath searchPath(basePath);
                mtlx = m_ctx->mtlxLoader->Load(mtlxDoc.get(), selectedElements, searchPath, matSys);
            } catch (MaterialX::ExceptionParseError& e) {
                fprintf(stderr, "Failed to parse %s: %s\n", m_mtlxFilepath.c_str(), e.what());
            } catch (MaterialX::ExceptionFileMissing& e) {
                fprintf(stderr, "Failed to parse %s: no such file - %s\n", m_mtlxFilepath.c_str(), e.what());
            }

            if (!mtlx.nodes) {
                return false;
            }

            // Check if mtlx has more than one output
            //
            int numOutputs = 0;
            for (auto index : mtlx.rootNodeIndices) {
                if (index != RPRMtlxLoader::Result::kInvalidRootNodeIndex) {
                    numOutputs++;
                    if (numOutputs > 1) {
                        break;
                    }
                }
            }

            using RetainedImages = std::vector<std::shared_ptr<RprUsdCoreImage>>;
            RetainedImages* retainedImagesPtr;
            auto mtlxPtr = &mtlx;

            if (numOutputs > 1) {
                // Share mtlx and retained images between all output nodes
                //
                struct SharedData {
                    RPRMtlxLoader::Result mtlx;
                    RetainedImages retainedImages;

                    ~SharedData() {
                        RPRMtlxLoader::Release(&mtlx);
                    }
                };
                auto sharedData = std::make_shared<SharedData>();
                sharedData->mtlx = mtlx;
                retainedImagesPtr = &sharedData->retainedImages;
                mtlxPtr = &sharedData->mtlx;

                class OutputWrapNode : public rpr::MaterialNode {
                public:
                    OutputWrapNode(rpr::Context& ctx, std::shared_ptr<SharedData> sharedData, RPRMtlxLoader::OutputType output)
                        : rpr::MaterialNode(ctx, ReleaseOutputNodeOwnership(&sharedData->mtlx, output))
                        , _sharedData(std::move(sharedData)) {}
                    ~OutputWrapNode() override = default;

                private:
                    std::shared_ptr<SharedData> _sharedData;
                };

                auto createOutputWrapNode = [&sharedData, this](RPRMtlxLoader::OutputType outputType) -> std::unique_ptr<OutputWrapNode> {
                    if (sharedData->mtlx.rootNodeIndices[outputType] == RPRMtlxLoader::Result::kInvalidRootNodeIndex) {
                        return nullptr;
                    }
                    return std::make_unique<OutputWrapNode>(*m_ctx->rprContext, sharedData, outputType);
                };
                m_surfaceNode = createOutputWrapNode(RPRMtlxLoader::kOutputSurface);
                m_displacementNode = createOutputWrapNode(RPRMtlxLoader::kOutputDisplacement);

            } else {
                // Find the only existing output
                RPRMtlxLoader::OutputType outputType = RPRMtlxLoader::kOutputNone;
                for (int i = 0; i < RPRMtlxLoader::kOutputsTotal; ++i) {
                    if (mtlx.rootNodeIndices[i] != RPRMtlxLoader::Result::kInvalidRootNodeIndex) {
                        outputType = RPRMtlxLoader::OutputType(i);
                        break;
                    }
                }

                struct OutputWrapNode : public rpr::MaterialNode {
                    OutputWrapNode(rpr::Context& ctx, RPRMtlxLoader::Result mtlx, RPRMtlxLoader::OutputType output)
                        : rpr::MaterialNode(ctx, ReleaseOutputNodeOwnership(&mtlx, output))
                        , mtlx(mtlx) {}
                    ~OutputWrapNode() override {
                        RPRMtlxLoader::Release(&mtlx);
                    }

                    RPRMtlxLoader::Result mtlx;
                    std::vector<std::shared_ptr<RprUsdCoreImage>> retainedImages;
                };
                auto wrapNode = std::make_unique<OutputWrapNode>(*m_ctx->rprContext, mtlx, outputType);
                retainedImagesPtr = &wrapNode->retainedImages;
                mtlxPtr = &wrapNode->mtlx;

                if (outputType == RPRMtlxLoader::kOutputSurface) {
                    m_surfaceNode = std::move(wrapNode);
                } else if (outputType == RPRMtlxLoader::kOutputDisplacement) {
                    m_displacementNode = std::move(wrapNode);
                }
            }

            // Commit all textures
            //
            if (mtlxPtr->imageNodes && (m_surfaceNode || m_displacementNode)) {
                RprUsdMaterialRegistry::TextureCommit textureCommit = {};
                for (size_t i = 0; i < mtlxPtr->numImageNodes; ++i) {
                    auto& mtlxImageNode = mtlxPtr->imageNodes[i];

                    textureCommit.filepath = std::move(mtlxImageNode.file);

                    std::string& addressmode = !mtlxImageNode.uaddressmode.empty() ? mtlxImageNode.uaddressmode : mtlxImageNode.vaddressmode;
                    if (!addressmode.empty()) {
                        if (mtlxImageNode.uaddressmode != mtlxImageNode.vaddressmode) {
                            TF_WARN("RPR does not support different address modes on an image. Using %s for %s image",
                                    addressmode.c_str(), textureCommit.filepath.c_str());
                        }

                        textureCommit.wrapType = RPR_IMAGE_WRAP_TYPE_REPEAT;
                        if (addressmode == "constant") {
                            TF_WARN("The constant uv address mode is not supported. Falling back to periodic.");
                        } else if (addressmode == "clamp") {
                            textureCommit.wrapType = RPR_IMAGE_WRAP_TYPE_CLAMP_TO_EDGE;
                        } else if (addressmode == "mirror") {
                            textureCommit.wrapType = RPR_IMAGE_WRAP_TYPE_MIRRORED_REPEAT;
                        }
                    }

                    if (mtlxImageNode.type == "float") {
                        textureCommit.numComponentsRequired = 1;
                    } else if (mtlxImageNode.type == "vector2" || mtlxImageNode.type == "color2") {
                        textureCommit.numComponentsRequired = 2;
                    } else if (mtlxImageNode.type == "vector3" || mtlxImageNode.type == "color3") {
                        textureCommit.numComponentsRequired = 3;
                    } else if (mtlxImageNode.type == "vector4" || mtlxImageNode.type == "color4") {
                        textureCommit.numComponentsRequired = 4;
                    } else {
                        TF_WARN("Invalid image materialX type: %s", mtlxImageNode.type.c_str());
                    }

                    rpr_material_node rprImageNode = mtlxImageNode.rprNode;
                    textureCommit.setTextureCallback = [retainedImagesPtr, rprImageNode](std::shared_ptr<RprUsdCoreImage> const& image) {
                        if (!image) return;

                        auto imageData = rpr::GetRprObject(image->GetRootImage());
                        if (!RPR_ERROR_CHECK(rprMaterialNodeSetInputImageDataByKey(rprImageNode, RPR_MATERIAL_INPUT_DATA, imageData), "Failed to set material node image data input")) {
                            retainedImagesPtr->push_back(image);
                        }
                    };

                    RprUsdMaterialRegistry::GetInstance().CommitTexture(std::move(textureCommit));
                }

                delete[] mtlxPtr->imageNodes;
                mtlxPtr->imageNodes = nullptr;
                mtlxPtr->numImageNodes = 0;
            }

            return m_surfaceNode || m_displacementNode;
        }

        rpr::Status status;
        if (!m_mtlxString.empty()) {
            m_surfaceNode.reset(m_ctx->rprContext->CreateMaterialXNode(m_mtlxString.c_str(), basePath.c_str(), 0, nullptr, nullptr, &status));
        } else {
            std::ifstream mtlxFile(m_mtlxFilepath);
            if (!mtlxFile.good()) {
                TF_RUNTIME_ERROR("Failed to open \"%s\" file", m_mtlxFilepath.c_str());
                return false;
            }

            mtlxFile.seekg(0, std::ios::end);
            auto fileSize = mtlxFile.tellg();
            if (fileSize == 0) {
                TF_RUNTIME_ERROR("Empty file: \"%s\"", m_mtlxFilepath.c_str());
                return false;
            }

            auto xmlData = std::make_unique<char[]>(fileSize);
            mtlxFile.seekg(0);
            mtlxFile.read(&xmlData[0], fileSize);

            m_surfaceNode.reset(m_ctx->rprContext->CreateMaterialXNode(xmlData.get(), basePath.c_str(), 0, nullptr, nullptr, &status));
        }

        if (!m_surfaceNode) {
            RPR_ERROR_CHECK(status, "Failed to create materialX node", m_ctx->rprContext);
        }
        return m_surfaceNode != nullptr;
    }

    static RprUsd_RprNodeInfo* GetInfo() {
        auto ret = new RprUsd_RprNodeInfo;
        auto& nodeInfo = *ret;

        nodeInfo.name = RprUsdRprMaterialXNodeTokens->rpr_materialx_node.GetText();
        nodeInfo.uiName = "RPR MaterialX";
        nodeInfo.uiFolder = "Shaders";

        RprUsd_RprNodeInput fileInput(RprUsdMaterialNodeElement::kFilepath);
        fileInput.name = RprUsdRprMaterialXNodeTokens->file.GetText();
        fileInput.uiName = "File";
        nodeInfo.inputs.push_back(fileInput);

        RprUsd_RprNodeInput stringInput(RprUsdMaterialNodeElement::kString);
        stringInput.name = RprUsdRprMaterialXNodeTokens->string.GetText();
        stringInput.uiName = ""; // hide from UI
        nodeInfo.inputs.push_back(stringInput);

        RprUsd_RprNodeInput basePathInput(RprUsdMaterialNodeElement::kString);
        basePathInput.name = RprUsdRprMaterialXNodeTokens->basePath.GetText();
        basePathInput.uiName = ""; // hide from UI
        nodeInfo.inputs.push_back(basePathInput);

        RprUsd_RprNodeInput stPrimvarNameInput(RprUsdMaterialNodeElement::kString);
        stPrimvarNameInput.name = RprUsdRprMaterialXNodeTokens->stPrimvarName.GetText();
        stPrimvarNameInput.uiName = "UV Primvar Name";
        stPrimvarNameInput.valueString = "st";
        nodeInfo.inputs.push_back(stPrimvarNameInput);

        RprUsd_RprNodeInput surfaceElementInput(RprUsdMaterialNodeElement::kString);
        surfaceElementInput.name = RprUsdRprMaterialXNodeTokens->surfaceElement.GetText();
        surfaceElementInput.uiName = "Surface Element";
        nodeInfo.inputs.push_back(surfaceElementInput);

        RprUsd_RprNodeInput displacementElementInput(RprUsdMaterialNodeElement::kString);
        displacementElementInput.name = RprUsdRprMaterialXNodeTokens->displacementElement.GetText();
        displacementElementInput.uiName = "Displacement Element";
        nodeInfo.inputs.push_back(displacementElementInput);

        RprUsd_RprNodeOutput surfaceOutput(RprUsdMaterialNodeElement::kSurfaceShader);
        surfaceOutput.name = "surface";
        nodeInfo.outputs.push_back(surfaceOutput);

        RprUsd_RprNodeOutput displacementOutput(RprUsdMaterialNodeElement::kDisplacementShader);
        displacementOutput.name = "displacement";
        nodeInfo.outputs.push_back(displacementOutput);

        return ret;
    }

private:
    RprUsd_MaterialBuilderContext* m_ctx;
    std::string m_mtlxFilepath;
    std::string m_mtlxString;
    std::string m_mtlxBasePath;
    std::string m_selectedRenderElements[RPRMtlxLoader::kOutputsTotal];

    bool m_isDirty = true;
    std::shared_ptr<rpr::MaterialNode> m_surfaceNode;
    std::shared_ptr<rpr::MaterialNode> m_displacementNode;
};

ARCH_CONSTRUCTOR(RprUsd_InitMaterialXNode, 255, void) {
    auto nodeInfo = RprUsd_RprMaterialXNode::GetInfo();
    RprUsdMaterialRegistry::GetInstance().Register(
        RprUsdRprMaterialXNodeTokens->rpr_materialx_node,
        [](RprUsd_MaterialBuilderContext* context,
        std::map<TfToken, VtValue> const& parameters) {
            auto node = new RprUsd_RprMaterialXNode(context);
            for (auto& entry : parameters) node->SetInput(entry.first, entry.second);
            return node;
        },
        nodeInfo);
}

PXR_NAMESPACE_CLOSE_SCOPE
