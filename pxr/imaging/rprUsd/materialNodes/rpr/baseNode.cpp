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

#include "pxr/imaging/rprUsd/materialMappings.h"
#include "pxr/imaging/rprUsd/materialHelpers.h"
#include "pxr/imaging/rprUsd/error.h"

PXR_NAMESPACE_OPEN_SCOPE

// It is possible that we need something more than just passing the parameters from USD to RPR, and need to do something with them.
// At the moment it is required in Uber only, but if we need more in the future, a class hierarchy with a child class should be implemented for any required node type.
// RprUsd_BaseRuntimeNode will hold a reference to the base class then, and
// CreateExtNode could be a static factory function in the base class returning the appropriate instance for each node type.
class UberNodeExtension {
public:
    static UberNodeExtension* CreateExtNode(
        rpr::MaterialNodeType type,
        const std::shared_ptr<rpr::MaterialNode>& parentNode,
        RprUsd_MaterialBuilderContext* ctx) {
        if (type == RPR_MATERIAL_NODE_UBERV2) {
            try {
                return new UberNodeExtension(parentNode, ctx);
            }
            catch (...) {
                return nullptr;
            }
        }
        return nullptr;
    }

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value,
        rpr::Status& status) {

        if (inputId == RprUsdMaterialNodeInputTokens->uber_emission_color) {
            status = SetRprInput(m_emissiveColorMergeNode.get(), RPR_MATERIAL_INPUT_COLOR0, value);
            return true;
        }
        else if (inputId == RprUsdMaterialNodeInputTokens->uber_emission_intensity) {
            status = SetRprInput(m_emissiveColorMergeNode.get(), RPR_MATERIAL_INPUT_COLOR1, value);
            return true;
        }
        return false;
    }

    bool SetInput(
        rpr::MaterialNodeInput input,
        VtValue const& value,
        rpr::Status& status) {

        if (input == RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR) {
            status = SetRprInput(m_emissiveColorMergeNode.get(), RPR_MATERIAL_INPUT_COLOR0, value);
            return true;
        }
        return false;
    }

protected:
    UberNodeExtension(
        const std::shared_ptr<rpr::MaterialNode>& parentNode,
        RprUsd_MaterialBuilderContext* ctx) {

        rpr::Status status;
        bool success = true;
        m_emissiveColorMergeNode.reset(ctx->rprContext->CreateMaterialNode(RPR_MATERIAL_NODE_ARITHMETIC, &status));
        success = success && m_emissiveColorMergeNode;
        success = success && m_emissiveColorMergeNode->SetInput(RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_MUL) == RPR_SUCCESS;
        success = success && parentNode->SetInput(RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR, m_emissiveColorMergeNode.get()) == RPR_SUCCESS;
        if (!success) {
            throw RprUsd_NodeError(RPR_GET_ERROR_MESSAGE(status, "Failed to create or set up material node", ctx->rprContext));
        }
    }
private:
    std::shared_ptr<rpr::MaterialNode> m_emissiveColorMergeNode;
};

RprUsd_BaseRuntimeNode::RprUsd_BaseRuntimeNode(
    rpr::MaterialNodeType type,
    RprUsd_MaterialBuilderContext* ctx)
    : m_type(type)
    , m_ctx(ctx) {

    rpr::Status status;
    m_rprNode.reset(ctx->rprContext->CreateMaterialNode(type, &status));

    if (!m_rprNode) {
        throw RprUsd_NodeError(RPR_GET_ERROR_MESSAGE(status, "Failed to create material node", ctx->rprContext));
    }

    m_extNode.reset(UberNodeExtension::CreateExtNode(type, m_rprNode, ctx));
}

// Destructor has to be non-inline to be able to use forward declaration of UberNodeExtension
RprUsd_BaseRuntimeNode::~RprUsd_BaseRuntimeNode() {
}

bool RprUsd_BaseRuntimeNode::SetInput(
    TfToken const& inputId,
    VtValue const& value) {
    rpr::Status status = RPR_SUCCESS;
    if (m_extNode && m_extNode->SetInput(inputId, value, status)) {
        return status == RPR_SUCCESS;
    }
    rpr::MaterialNodeInput rprInput;
    if (ToRpr(inputId, &rprInput)) {
        return SetInput(rprInput, value);
    }
    return false;
}

bool RprUsd_BaseRuntimeNode::SetInput(
    rpr::MaterialNodeInput input,
    VtValue const& value) {
    rpr::Status status = RPR_SUCCESS;
    if (m_extNode && m_extNode->SetInput(input, value, status)) {
        return status == RPR_SUCCESS;
    }
    status = SetRprInput(m_rprNode.get(), input, value);
    if (status == RPR_SUCCESS) {
        return true;
    }

    // XXX: Currently Hybrid does not support all UBER material parameters.
    //      Do not invalidate whole node because of it.
    return m_type == RPR_MATERIAL_NODE_UBERV2 &&
        (status == RPR_ERROR_UNSUPPORTED || status == RPR_ERROR_UNIMPLEMENTED);
}

VtValue RprUsd_BaseRuntimeNode::GetOutput(TfToken const& outputId) {
    return VtValue(m_rprNode);
}

PXR_NAMESPACE_CLOSE_SCOPE
