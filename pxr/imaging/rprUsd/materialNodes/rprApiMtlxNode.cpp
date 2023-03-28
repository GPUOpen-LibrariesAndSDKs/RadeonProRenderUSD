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

#include "rprApiMtlxNode.h"

#include "pxr/imaging/rprUsd/material.h"
#include "pxr/imaging/rprUsd/error.h"
#include "pxr/base/arch/fileSystem.h"
#include "rpr/baseNode.h"

#include <RadeonProRender_MaterialX.h>

PXR_NAMESPACE_OPEN_SCOPE

rpr::MaterialNode* RprUsd_CreateRprMtlxFromString(std::string const& mtlxString, RprUsd_MaterialBuilderContext const& context) {
    rpr::Status status;
    std::unique_ptr<rpr::MaterialNode> matxNode(context.rprContext->CreateMaterialNode(RPR_MATERIAL_NODE_MATX, &status));
    if (!matxNode) {
        RPR_ERROR_CHECK(status, "Failed to create matx node");
        return nullptr;
    }

    rpr_material_node matxNodeHandle = rpr::GetRprObject(matxNode.get());

    status = rprMaterialXSetFileAsBuffer(matxNodeHandle, mtlxString.c_str(), mtlxString.size());
    
    if (status != RPR_SUCCESS) {
        RPR_ERROR_CHECK(status, "Failed to set matx node file from buffer");
        return nullptr;
    }

    return matxNode.release();
}

rpr::MaterialNode* RprUsd_CreateRprMtlxFromFile(std::string const& mtlxFile, RprUsd_MaterialBuilderContext const& context) {
    rpr::Status status;
    std::unique_ptr<rpr::MaterialNode> matxNode(context.rprContext->CreateMaterialNode(RPR_MATERIAL_NODE_MATX, &status));
    if (!matxNode) {
        RPR_ERROR_CHECK(status, "Failed to create matx node");
        return nullptr;
    }

    try {
        rpr_material_node matxNodeHandle = rpr::GetRprObject(matxNode.get());
        status = rprMaterialXSetFile(matxNodeHandle, mtlxFile.c_str());
        if (status != RPR_SUCCESS) {
            RPR_ERROR_CHECK(status, "Failed to set matx node file");
            return nullptr;
        }
    } catch (...) {
        // C API that leaks C++ exceptions and leaves an object in undetermined state? Classic.
        // We have to leak a memory here to prevent a crash.
        matxNode.release();
        return nullptr;
    }

    return matxNode.release();
}

PXR_NAMESPACE_CLOSE_SCOPE
