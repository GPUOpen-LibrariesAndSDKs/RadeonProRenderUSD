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

#include "../materialNode.h"
#include "nodeInfo.h"

#include "pxr/imaging/rprUsd/materialRegistry.h"
#include "pxr/base/arch/attributes.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class RprUsd_RprCombineShadersNode
///
/// Convenience node to allow combination of nodes of different type.
///
/// A bit more about why we need this node.
/// In USD you can bind only one material to the mesh.
/// But what if you want apply both displacement and surface shaders on the mesh?
/// You have two options:
///   a) add `displacement` component to the all surface shaders
///   b) take outputs of `surface` node and `displacement` and combine them into one node
/// This node implements the second option
///
class RprUsd_RprCombineShadersNode : public RprUsd_MaterialNode {
public:
    RprUsd_RprCombineShadersNode() = default;
    ~RprUsd_RprCombineShadersNode() override = default;

    VtValue GetOutput(TfToken const& outputId) override {
        auto it = m_outputs.find(outputId);
        if (it != m_outputs.end()) {
            return it->second;
        }
        return VtValue();
    }

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value) override {
        if (inputId == HdMaterialTerminalTokens->volume ||
            inputId == HdMaterialTerminalTokens->surface ||
            inputId == HdMaterialTerminalTokens->displacement) {
            if (value.IsHolding<std::shared_ptr<rpr::MaterialNode>>()) {
                m_outputs[inputId] = value;
                return true;
            } else {
                TF_RUNTIME_ERROR("Invalid input for Combine Shaders node: must be of shader type, type=%s", value.GetTypeName().c_str());
            }
        } else {
            TF_RUNTIME_ERROR("Invalid input for Combine Shaders node: must be `surface` or `displacement` or `volume`, inputId=%s", inputId.GetText());
        }

        return false;
    }

    static RprUsd_RprNodeInfo* GetInfo() {
        auto ret = new RprUsd_RprNodeInfo;
        auto& nodeInfo = *ret;

        nodeInfo.name = "rpr_combine";
        nodeInfo.uiName = "RPR Combine Shaders";
        nodeInfo.uiFolder = "Shaders";

        RprUsd_RprNodeInput surfaceInput(RprUsdMaterialNodeElement::kSurfaceShader);
        surfaceInput.name = "surface";
        surfaceInput.uiName = "Surface Shader";
        nodeInfo.inputs.push_back(surfaceInput);

        RprUsd_RprNodeInput displacementInput(RprUsdMaterialNodeElement::kDisplacementShader);
        displacementInput.name = "displacement";
        displacementInput.uiName = "Displacement Shader";
        nodeInfo.inputs.push_back(displacementInput);

        RprUsd_RprNodeInput volumeInput(RprUsdMaterialNodeElement::kVolumeShader);
        volumeInput.name = "volume";
        volumeInput.uiName = "Volume Shader";
        nodeInfo.inputs.push_back(volumeInput);

        RprUsd_RprNodeOutput outputSurface(RprUsdMaterialNodeElement::kSurfaceShader);
        outputSurface.name = "surface";
        nodeInfo.outputs.push_back(outputSurface);

        RprUsd_RprNodeOutput outputDisplacement(RprUsdMaterialNodeElement::kDisplacementShader);
        outputDisplacement.name = "displacement";
        nodeInfo.outputs.push_back(outputDisplacement);

        RprUsd_RprNodeOutput outputVolume(RprUsdMaterialNodeElement::kVolumeShader);
        outputVolume.name = "volume";
        nodeInfo.outputs.push_back(outputVolume);

        return ret;
    }

private:
    std::map<TfToken, VtValue> m_outputs;
};

ARCH_CONSTRUCTOR(RprUsd_InitCombineShadersNode, 255, void) {
    auto nodeInfo = RprUsd_RprCombineShadersNode::GetInfo();
    RprUsdMaterialRegistry::GetInstance().Register(
        TfToken(nodeInfo->name, TfToken::Immortal),
        [](RprUsd_MaterialBuilderContext* context,
        std::map<TfToken, VtValue> const& parameters) {
            auto node = new RprUsd_RprCombineShadersNode();
            for (auto& entry : parameters) node->SetInput(entry.first, entry.second);
            return node;
        },
        nodeInfo);
}

PXR_NAMESPACE_CLOSE_SCOPE
