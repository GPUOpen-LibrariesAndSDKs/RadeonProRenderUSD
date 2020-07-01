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

#ifndef RPRUSD_MATERIAL_NODES_HOUDINI_PRINCIPLED_SHADER_NODE_H
#define RPRUSD_MATERIAL_NODES_HOUDINI_PRINCIPLED_SHADER_NODE_H

#include "rpr/baseNode.h"

#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class RprUsd_HoudiniPrincipledNode
///
/// The node that implements Houdini's principled node
/// (https://www.sidefx.com/docs/houdini/nodes/vop/principledshader.html)
///
/// Before expecting this node to work you need to make sure that
/// `RPRUSD_MATERIAL_NETWORK_SELECTOR` environment variable is set to `karma`.
/// If it's not set to `karma`, Hydra will ignore houdini's principled node and
/// its data will not be present in `HdMaterialNetwork` passed to hdRpr.
///
/// Support of the principled shader is limited to the node without any connected nodes.
/// This is due to the fact that the principled shader node input parameters listed as
/// HdMaterialNode::parameters only if there are no connected nodes. If principled shader
/// node has any node connected to it, Houdini will automatically convert its
/// implementation to VEX code that is not supported.
///
class RprUsd_HoudiniPrincipledNode : public RprUsd_BaseRuntimeNode {
public:
    RprUsd_HoudiniPrincipledNode(
        RprUsd_MaterialBuilderContext* ctx,
        std::map<TfToken, VtValue> const& surfaceParameters,
        std::map<TfToken, VtValue> const* displacementParameters);
    ~RprUsd_HoudiniPrincipledNode() override = default;

    VtValue GetOutput(TfToken const& outputId) override;

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value) override {
        return false;
    }

private:
    template <typename T>
    T* AddAuxiliaryNode(std::unique_ptr<T> node) {
        T* ret = node.get();
        m_auxiliaryNodes.push_back(std::move(node));
        return ret;
    }

    VtValue GetTextureOutput(
        SdfAssetPath const& path,
        std::string const& wrapMode,
        float scale, float bias,
        bool forceLinearSpace,
        TfToken const& outputId,
        RprUsd_MaterialNode** uvTextureNode = nullptr);

private:
    std::vector<std::unique_ptr<RprUsd_MaterialNode>> m_auxiliaryNodes;
    RprUsd_MaterialNode* m_baseColorNode = nullptr;
    VtValue m_displacementOutput;
};

bool IsHoudiniPrincipledShaderHydraNode(HdSceneDelegate* delegate, SdfPath const& nodePath, bool* isSurface);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_NODES_HOUDINI_PRINCIPLED_SHADER_NODE_H
