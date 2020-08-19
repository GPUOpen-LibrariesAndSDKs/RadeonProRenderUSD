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
#include "pxr/base/gf/vec2f.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(RprUsdRprCatcherNodeTokens,
    (in)
    (enable)
);

/// \class RprUsd_RprCatcherNode
///
/// The node that allows the user to enable either shadow catcher or reflection
///   catcher (depending on how this node is constructed).
///
/// This node has two inputs:
///   1) boolean input that enables or disables catcher mode.
///   2) surface shader input that is simply transmitted to the surface output.
///        This automatically means that this node can not be created without
///        existing material that outputs the real surface shader.
class RprUsd_RprCatcherNode : public RprUsd_MaterialNode {
public:
    RprUsd_RprCatcherNode(bool* catcherToggle)
        : m_catcherToggle(catcherToggle) {
        // true by default
        *m_catcherToggle = true;
    }
    ~RprUsd_RprCatcherNode() override = default;

    VtValue GetOutput(TfToken const& outputId) override { return m_output; }

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value) override {
        if (inputId == RprUsdRprCatcherNodeTokens->in) {
            m_output = value;
            return true;
        } else if (inputId == RprUsdRprCatcherNodeTokens->enable) {
            if (value.IsHolding<int>()) {
                *m_catcherToggle = value.UncheckedGet<int>() != 0;
                return true;
            }
        }

        return false;
    }

    static RprUsd_RprNodeInfo* GetInfo(std::string catcherType) {
        auto ret = new RprUsd_RprNodeInfo;
        auto& nodeInfo = *ret;

        nodeInfo.uiName =  "RPR " + catcherType + " Catcher";
        nodeInfo.uiFolder = "Shaders";
        catcherType[0] = std::tolower(catcherType[0], std::locale());
        nodeInfo.name = "rpr_" + catcherType + "_catcher";

        RprUsd_RprNodeInput in(RprUsdMaterialNodeElement::kSurfaceShader);
        in.name = "in";
        nodeInfo.inputs.push_back(in);

        RprUsd_RprNodeInput enable(RprUsdMaterialNodeElement::kBoolean);
        enable.name = "enable";
        enable.uiName = "Enable";
        enable.valueString = "true";
        nodeInfo.inputs.push_back(enable);

        RprUsd_RprNodeOutput surface(RprUsdMaterialNodeElement::kSurfaceShader);
        surface.name = "surface";
        nodeInfo.outputs.push_back(surface);

        return ret;
    }

private:
    bool* m_catcherToggle;
    VtValue m_output;
};

#define REGISTER_CATCHER_NODE(CATCHER_TYPE)                                                  \
ARCH_CONSTRUCTOR(RprUsd_Init ## CATCHER_TYPE ## CatcherNode, 255, void) {                    \
    auto nodeInfo = RprUsd_RprCatcherNode::GetInfo(#CATCHER_TYPE);                           \
    RprUsdMaterialRegistry::GetInstance().Register(                                          \
        TfToken(nodeInfo->name, TfToken::Immortal),                                          \
        [](RprUsd_MaterialBuilderContext* context,                                           \
        std::map<TfToken, VtValue> const& parameters) {                                      \
            auto node = new RprUsd_RprCatcherNode(&context->is ## CATCHER_TYPE ## Catcher);  \
            for (auto& entry : parameters) node->SetInput(entry.first, entry.second);        \
            return node;                                                                     \
        },                                                                                   \
        nodeInfo);                                                                           \
}

REGISTER_CATCHER_NODE(Shadow);
REGISTER_CATCHER_NODE(Reflection);

PXR_NAMESPACE_CLOSE_SCOPE
