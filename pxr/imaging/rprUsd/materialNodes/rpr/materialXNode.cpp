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

#include "baseNode.h"
#include "nodeInfo.h"

#include "pxr/usd/sdf/assetPath.h"
#include "pxr/base/arch/attributes.h"
#include "pxr/imaging/rprUsd/error.h"

#include <fstream>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(RprUsdRprMaterialXNodeTokens,
    (rpr_materialx_node)
    (file)
);

class RprUsd_RprMaterialXNode : public RprUsd_MaterialNode {
public:
    RprUsd_RprMaterialXNode(RprUsd_MaterialBuilderContext* ctx)
        : m_ctx(ctx) {

    }

    ~RprUsd_RprMaterialXNode() override = default;

    VtValue GetOutput(TfToken const& outputId) override {
        return VtValue(m_materialNode);
    }

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value) override {
        if (inputId == RprUsdRprMaterialXNodeTokens->file) {
            if (value.IsHolding<SdfAssetPath>()) {
                auto& assetPath = value.UncheckedGet<SdfAssetPath>();
                auto& path = assetPath.GetResolvedPath();

                std::ifstream mtlxFile(path);
                if (!mtlxFile.good()) {
                    TF_RUNTIME_ERROR("Failed to open \"%s\" file", path.c_str());
                    return false;
                }

                mtlxFile.seekg(0, std::ios::end);
                auto fileSize = mtlxFile.tellg();
                if (fileSize == 0) {
                    TF_RUNTIME_ERROR("Empty file: \"%s\"", path.c_str());
                    return false;
                }

                auto xmlData = std::make_unique<char[]>(fileSize);
                mtlxFile.seekg(0);
                mtlxFile.read(&xmlData[0], fileSize);

                auto basePath = TfGetPathName(path);

                rpr::Status status;
                m_materialNode.reset(m_ctx->rprContext->CreateMaterialXNode(xmlData.get(), basePath.c_str(), 0, nullptr, nullptr, &status));

                if (!m_materialNode) {
                    RPR_ERROR_CHECK(status, "Failed to create materialX node", m_ctx->rprContext);
                }
                return m_materialNode != nullptr;
            } else {
                TF_RUNTIME_ERROR("[%s] file input should be of SdfAssetPath type: %s",
                    RprUsdRprMaterialXNodeTokens->rpr_materialx_node.GetText(), value.GetTypeName().c_str());
                return false;
            }
        }

        TF_RUNTIME_ERROR("[%s] Unknown input %s",
            RprUsdRprMaterialXNodeTokens->rpr_materialx_node.GetText(), inputId.GetText());
        return false;
    }

    static RprUsd_RprNodeInfo* GetInfo() {
        auto ret = new RprUsd_RprNodeInfo;
        auto& nodeInfo = *ret;

        nodeInfo.name = RprUsdRprMaterialXNodeTokens->rpr_materialx_node.GetText();
        nodeInfo.uiName = "RPR MaterialX";
        nodeInfo.uiFolder = "Shaders";

        RprUsd_RprNodeInput fileInput(RprUsdMaterialNodeElement::kFilepath);
        fileInput.name = RprUsdRprMaterialXNodeTokens->file.GetText();
        fileInput.uiName = "MaterialX File";
        nodeInfo.inputs.push_back(fileInput);

        RprUsd_RprNodeOutput output(RprUsdMaterialNodeElement::kSurfaceShader);
        output.name = "surface";
        nodeInfo.outputs.push_back(output);

        return ret;
    }

private:
    RprUsd_MaterialBuilderContext* m_ctx;
    std::shared_ptr<rpr::MaterialNode> m_materialNode;
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
