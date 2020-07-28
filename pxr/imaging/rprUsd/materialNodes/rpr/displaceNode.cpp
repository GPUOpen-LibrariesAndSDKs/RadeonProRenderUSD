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
#include "pxr/base/gf/vec2f.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(RprUsdRprDisplaceNodeTokens,
    (minscale)
    (maxscale)
    (in)
);

/// \class RprUsd_RprDisplaceNode
///
/// The node that maps RPR displacement functionality one-to-one:
///   - `in` expects rpr::MaterialNode as input and corresponds to `rprShapeSetDisplacementMaterial`
///   - `minscale` and `maxscale` inputs are expected to be of float type and correspond to `rprShapeSetDisplacementScale`
///
class RprUsd_RprDisplaceNode : public RprUsd_MaterialNode {
public:
    RprUsd_RprDisplaceNode(
        RprUsd_MaterialBuilderContext* ctx)
        : m_ctx(ctx)
        , m_displacementScale(0, 1) {

    }
    ~RprUsd_RprDisplaceNode() override = default;

    VtValue GetOutput(TfToken const& outputId) override {
        if (m_output.IsEmpty()) {
            return VtValue();
        }

        m_ctx->displacementScale = VtValue(m_displacementScale);
        return m_output;
    }

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value) override {
        if (inputId == RprUsdRprDisplaceNodeTokens->minscale) {
            if (value.IsHolding<float>()) {
                m_displacementScale[0] = value.UncheckedGet<float>();
            } else {
                TF_RUNTIME_ERROR("Input `minscale` has invalid type: %s, expected - float", value.GetTypeName().c_str());
                m_displacementScale[0] = 0.0f;
                return false;
            }
        } else if (inputId == RprUsdRprDisplaceNodeTokens->maxscale) {
            if (value.IsHolding<float>()) {
                m_displacementScale[1] = value.UncheckedGet<float>();
            } else {
                TF_RUNTIME_ERROR("Input `maxscale` has invalid type: %s, expected - float", value.GetTypeName().c_str());
                m_displacementScale[1] = 1.0f;
                return false;
            }
        } else if (inputId == RprUsdRprDisplaceNodeTokens->in) {
            if (value.IsHolding<std::shared_ptr<rpr::MaterialNode>>()) {
                m_output = value;
            } else {
                auto vec = GetRprFloat(value);
                if (!GfIsEqual(vec, GfVec4f(0.0f))) {
                    if (!m_scalarDisplaceNode) {
                        m_scalarDisplaceNode.reset(new RprUsd_BaseRuntimeNode(RPR_MATERIAL_NODE_CONSTANT_TEXTURE, m_ctx));
                    }

                    m_scalarDisplaceNode->SetInput(RPR_MATERIAL_INPUT_VALUE, value);
                    m_output = VtValue(m_scalarDisplaceNode);
                } else {
                    m_scalarDisplaceNode = nullptr;
                    m_output = VtValue();
                }
            }
        }

        return true;
    }

    static RprUsd_RprNodeInfo* GetInfo() {
        auto ret = new RprUsd_RprNodeInfo;
        auto& nodeInfo = *ret;

        nodeInfo.name = "rpr_displace";
        nodeInfo.uiName = "RPR Displace";
        nodeInfo.uiFolder = "Shaders";

        RprUsd_RprNodeInput input(RprUsdMaterialNodeElement::kFloat);
        input.uiSoftMin = "0";
        input.uiSoftMax = "1";

        input.name = "in";
        input.uiName = "Displacement";
        input.valueString = "0";
        nodeInfo.inputs.push_back(input);

        input.name = "minscale";
        input.uiName = "Minimum Scale";
        nodeInfo.inputs.push_back(input);

        input.name = "maxscale";
        input.uiName = "Maximum Scale";
        input.valueString = "1";
        nodeInfo.inputs.push_back(input);

        RprUsd_RprNodeOutput output(RprUsdMaterialNodeElement::kDisplacementShader);
        output.name = "displacement";
        nodeInfo.outputs.push_back(output);

        return ret;
    }

private:
    RprUsd_MaterialBuilderContext* m_ctx;
    GfVec2f m_displacementScale;
    std::shared_ptr<RprUsd_BaseRuntimeNode> m_scalarDisplaceNode;
    VtValue m_output;
};

ARCH_CONSTRUCTOR(RprUsd_InitDisplaceNode, 255, void) {
    auto nodeInfo = RprUsd_RprDisplaceNode::GetInfo();
    RprUsdMaterialRegistry::GetInstance().Register(
        TfToken(nodeInfo->name, TfToken::Immortal),
        [](RprUsd_MaterialBuilderContext* context,
        std::map<TfToken, VtValue> const& parameters) {
            auto node = new RprUsd_RprDisplaceNode(context);
            for (auto& entry : parameters) node->SetInput(entry.first, entry.second);
            return node;
        },
        nodeInfo);
}

PXR_NAMESPACE_CLOSE_SCOPE
