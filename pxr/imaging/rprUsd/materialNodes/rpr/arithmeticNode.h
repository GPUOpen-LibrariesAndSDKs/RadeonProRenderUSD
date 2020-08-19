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

#ifndef RPRUSD_MATERIAL_NODES_RPR_ARITHMETIC_NODE_H
#define RPRUSD_MATERIAL_NODES_RPR_ARITHMETIC_NODE_H

#include "../materialNode.h"

#include "pxr/imaging/rprUsd/materialRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class RprUsd_RprArithmeticNode
///
/// Wrapper over RPR_MATERIAL_NODE_ARITHMETIC
class RprUsd_RprArithmeticNode : public RprUsd_MaterialNode {
public:
    static std::unique_ptr<RprUsd_RprArithmeticNode> Create(
        rpr::MaterialNodeArithmeticOperation operation,
        RprUsd_MaterialBuilderContext* ctx,
        std::map<TfToken, VtValue> const& parameters = {});

    ~RprUsd_RprArithmeticNode() override = default;

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value) override;

    VtValue GetOutput(TfToken const& outputId) override { return GetOutput(); }

    /// Arithmetic node has only one output
    VtValue GetOutput();

    /// Arithmetic node has up to four arguments (\p index in range [0; 3])
    bool SetInput(int index, VtValue const& value);

protected:
    RprUsd_RprArithmeticNode(RprUsd_MaterialBuilderContext* ctx) : m_ctx(ctx) {}

    // Provided by concrete operation node
    virtual int GetNumArguments() const = 0;
    virtual VtValue EvalOperation() const = 0;
    virtual rpr::MaterialNodeArithmeticOperation GetOp() const = 0;

protected:
    RprUsd_MaterialBuilderContext* m_ctx;
    VtValue m_args[4];
    VtValue m_output;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_NODES_RPR_ARITHMETIC_NODE_H
