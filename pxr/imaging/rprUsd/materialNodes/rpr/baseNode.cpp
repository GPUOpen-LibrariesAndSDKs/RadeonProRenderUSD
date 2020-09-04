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
}

bool RprUsd_BaseRuntimeNode::SetInput(
    TfToken const& inputId,
    VtValue const& value) {
    rpr::MaterialNodeInput rprInput;
    if (ToRpr(inputId, &rprInput)) {
        return SetInput(rprInput, value);
    }
    return false;
}

bool RprUsd_BaseRuntimeNode::SetInput(
    rpr::MaterialNodeInput input,
    VtValue const& value) {
    rpr::Status status = SetRprInput(m_rprNode.get(), input, value);
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
