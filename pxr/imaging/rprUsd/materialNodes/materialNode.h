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

#ifndef RPRUSD_MATERIAL_NODES_MATERIAL_NODE_H
#define RPRUSD_MATERIAL_NODES_MATERIAL_NODE_H

#include "pxr/imaging/hd/material.h"

namespace rpr { class Context; }

class RPRMtlxLoader;

PXR_NAMESPACE_OPEN_SCOPE

class RprUsdImageCache;

struct RprUsd_MaterialNetwork {
    struct Connection {
        SdfPath upstreamNode;
        TfToken upstreamOutputName;
    };

    struct Node {
        TfToken nodeTypeId;
        std::map<TfToken, VtValue> parameters;
        std::map<TfToken, Connection> inputConnections;
    };

    std::map<SdfPath, Node> nodes;
    std::map<TfToken, Connection> terminals;
};

struct RprUsd_MaterialBuilderContext {
    RprUsd_MaterialNetwork const* hdMaterialNetwork;
    SdfPath const* currentNodePath;

    rpr::Context* rprContext;
    RprUsdImageCache* imageCache;

    std::string uvPrimvarName;
    bool isShadowCatcher;
    bool isReflectionCatcher;

    VtValue displacementScale;

    RPRMtlxLoader* mtlxLoader;
};

class RprUsd_MaterialNode {
public:
    virtual ~RprUsd_MaterialNode() = default;

    virtual VtValue GetOutput(
        TfToken const& outputId) = 0;

    virtual bool SetInput(
        TfToken const& inputId,
        VtValue const& value) = 0;
};

class RprUsd_NodeError : public std::exception {
public:
    RprUsd_NodeError(std::string errorMessage) : m_msg(std::move(errorMessage)) {}
    ~RprUsd_NodeError() override = default;

    const char* what() const noexcept override { return m_msg.c_str(); }

private:
    std::string m_msg;
};

/// Thrown when node is empty.
/// There is no point to keep a node in memory when all it does is propagates inputs to outputs.
class RprUsd_NodeEmpty : public std::exception {
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_NODES_MATERIAL_NODE_H
