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

#include "pxr/imaging/rprUsd/materialHelpers.h"
#include "pxr/base/arch/attributes.h"
#include "pxr/base/gf/vec3f.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (color)
    (roughness)
    (normal)
    (shadowTint)
    (midLevel)
    (midLevelMix)
    (midTint)
    (highlightLevel)
    (highlightLevelMix)
    (highlightTint)
    (interpolationMode)
    (Linear)
    (None)
);

template <typename ExpectedType, typename SmartPtr>
bool ProcessInput(TfToken const& inputId, VtValue const& inputValue, SmartPtr const& rprNode, rpr::MaterialNodeInput rprInput) {
    if (inputValue.IsHolding<ExpectedType>() ||
        inputValue.IsHolding<RprMaterialNodePtr>()) {
        return SetRprInput(rprNode.get(), rprInput, inputValue) == RPR_SUCCESS;
    }
    TF_RUNTIME_ERROR("Input `%s` has invalid type: %s, expected - %s", inputId.GetText(), inputValue.GetTypeName().c_str(), TfType::Find<ExpectedType>().GetTypeName().c_str());
    return false;
}

/// \class RprUsd_RprToonNode
///
/// The node that wraps RPR nodes required to setup correct RPR toon shader.
///
class RprUsd_RprToonNode : public RprUsd_MaterialNode {
public:
    RprUsd_RprToonNode(RprUsd_MaterialBuilderContext* ctx) {

        rpr::Status status;
        m_toonClosureNode.reset(ctx->rprContext->CreateMaterialNode(RPR_MATERIAL_NODE_TOON_CLOSURE, &status));
        if (!m_toonClosureNode) {
            throw RprUsd_NodeError(RPR_GET_ERROR_MESSAGE(status, "Failed to create toon closure node", ctx->rprContext));
        }
        m_rampNode.reset(ctx->rprContext->CreateMaterialNode(RPR_MATERIAL_NODE_TOON_RAMP, &status));
        if (!m_rampNode) {
            throw RprUsd_NodeError(RPR_GET_ERROR_MESSAGE(status, "Failed to create toon ramp node", ctx->rprContext));
        }
        status = m_toonClosureNode->SetInput(RPR_MATERIAL_INPUT_DIFFUSE_RAMP, m_rampNode.get());
        if (status != RPR_SUCCESS) {
            throw RprUsd_NodeError(RPR_GET_ERROR_MESSAGE(status, "Failed to set ramp node input of closure node", ctx->rprContext));
        }
    }
    ~RprUsd_RprToonNode() override = default;

    VtValue GetOutput(TfToken const& outputId) override {
        return VtValue(m_toonClosureNode);
    }

    bool SetInput(
        TfToken const& id,
        VtValue const& value) override {
        if (id == _tokens->shadowTint) {
            return ProcessInput<GfVec3f>(id, value, m_rampNode, RPR_MATERIAL_INPUT_SHADOW);
        } else if (id == _tokens->midLevel) {
            return ProcessInput<float>(id, value, m_rampNode, RPR_MATERIAL_INPUT_POSITION1);
        } else if (id == _tokens->midLevelMix) {
            return ProcessInput<float>(id, value, m_rampNode, RPR_MATERIAL_INPUT_RANGE1);
        } else if (id == _tokens->midTint) {
            return ProcessInput<GfVec3f>(id, value, m_rampNode, RPR_MATERIAL_INPUT_MID);
        } else if (id == _tokens->highlightLevel) {
            return ProcessInput<float>(id, value, m_rampNode, RPR_MATERIAL_INPUT_POSITION2);
        } else if (id == _tokens->highlightLevelMix) {
            return ProcessInput<float>(id, value, m_rampNode, RPR_MATERIAL_INPUT_RANGE2);
        } else if (id == _tokens->highlightTint) {
            return ProcessInput<GfVec3f>(id, value, m_rampNode, RPR_MATERIAL_INPUT_HIGHLIGHT);
        } else if (id == _tokens->interpolationMode) {
            if (value.IsHolding<int>()) {
                int interpolationModeInt = value.UncheckedGet<int>();
                auto interpolationMode =  !interpolationModeInt ? RPR_INTERPOLATION_MODE_NONE : RPR_INTERPOLATION_MODE_LINEAR;
                return m_rampNode->SetInput(RPR_MATERIAL_INPUT_INTERPOLATION, interpolationMode) == RPR_SUCCESS;
            }
            TF_RUNTIME_ERROR("Input `%s` has invalid type: %s, expected - `Token`", id.GetText(), value.GetTypeName().c_str());
            return false;
        } else if (id == _tokens->color) {
            return ProcessInput<GfVec3f>(id, value, m_toonClosureNode, RPR_MATERIAL_INPUT_COLOR);
        } else if (id == _tokens->roughness) {
            return ProcessInput<float>(id, value, m_toonClosureNode, RPR_MATERIAL_INPUT_ROUGHNESS);
        } else if (id == _tokens->normal) {
            if (value.IsHolding<GfVec3f>() &&
                value.UncheckedGet<GfVec3f>() == GfVec3f(0.0f)) {
                return m_toonClosureNode->SetInput(RPR_MATERIAL_INPUT_NORMAL, (rpr::MaterialNode*)nullptr) == RPR_SUCCESS;
            }

            return ProcessInput<GfVec3f>(id, value, m_toonClosureNode, RPR_MATERIAL_INPUT_NORMAL);
        }

        TF_RUNTIME_ERROR("Unknown input `%s` for RPR Toon node", id.GetText());
        return false;
    }

    static RprUsd_RprNodeInfo* GetInfo() {
        static RprUsd_RprNodeInfo* ret = nullptr;
        if (ret) {
            return ret;
        }

        ret = new RprUsd_RprNodeInfo;
        auto& nodeInfo = *ret;

        nodeInfo.name = "rpr_toon";
        nodeInfo.uiName = "RPR Toon";
        nodeInfo.uiFolder = "Shaders";

        nodeInfo.inputs.emplace_back(_tokens->color, GfVec3f(1.0f));
        nodeInfo.inputs.emplace_back(_tokens->shadowTint, GfVec3f(0.0f));
        nodeInfo.inputs.emplace_back(_tokens->midLevel, 0.5f);
        nodeInfo.inputs.emplace_back(_tokens->midLevelMix, 0.05f);
        nodeInfo.inputs.emplace_back(_tokens->midTint, GfVec3f(0.4f));
        nodeInfo.inputs.emplace_back(_tokens->highlightLevel, 0.8f);
        nodeInfo.inputs.emplace_back(_tokens->highlightLevelMix, 0.05f);
        nodeInfo.inputs.emplace_back(_tokens->highlightTint, GfVec3f(0.8f));

        RprUsd_RprNodeInput interpolationModeInput(_tokens->interpolationMode, _tokens->None);
        interpolationModeInput.value = VtValue(0);
        interpolationModeInput.tokenValues = {_tokens->None, _tokens->Linear};
        nodeInfo.inputs.push_back(interpolationModeInput);

        nodeInfo.inputs.emplace_back(_tokens->roughness, 1.0f);
        nodeInfo.inputs.emplace_back(_tokens->normal, GfVec3f(0.0f), RprUsdMaterialNodeElement::kVector3, "");

        RprUsd_RprNodeOutput output(RprUsdMaterialNodeElement::kSurfaceShader);
        output.name = "surface";
        nodeInfo.outputs.push_back(output);

        return ret;
    }

private:
    std::unique_ptr<rpr::MaterialNode> m_rampNode;
    std::shared_ptr<rpr::MaterialNode> m_toonClosureNode;
};

ARCH_CONSTRUCTOR(RprUsd_InitDisplaceNode, 255, void) {
    auto nodeInfo = RprUsd_RprToonNode::GetInfo();
    RprUsdMaterialRegistry::GetInstance().Register(
        TfToken(nodeInfo->name, TfToken::Immortal),
        [](RprUsd_MaterialBuilderContext* context, std::map<TfToken, VtValue> const& parameters) {
            auto node = new RprUsd_RprToonNode(context);
            for (auto& input : RprUsd_RprToonNode::GetInfo()->inputs) {
                auto it = parameters.find(input.name);
                if (it == parameters.end()) {
                    node->SetInput(input.name, input.value);
                } else {
                    node->SetInput(input.name, it->second);
                }
            }
            return node;
        },
        nodeInfo);
}

PXR_NAMESPACE_CLOSE_SCOPE
