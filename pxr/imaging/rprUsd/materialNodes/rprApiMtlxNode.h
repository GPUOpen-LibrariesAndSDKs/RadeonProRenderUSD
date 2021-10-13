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

#ifndef RPRUSD_MATERIAL_NODES_RPR_API_MTLX_NODE_H
#define RPRUSD_MATERIAL_NODES_RPR_API_MTLX_NODE_H

#include "pxr/pxr.h"

#include <string>

namespace rpr { class MaterialNode; }

PXR_NAMESPACE_OPEN_SCOPE

struct RprUsd_MaterialBuilderContext;

rpr::MaterialNode* RprUsd_CreateRprMtlxFromString(std::string const& mtlxString, RprUsd_MaterialBuilderContext const& context);
rpr::MaterialNode* RprUsd_CreateRprMtlxFromFile(std::string const& mtlxFile, RprUsd_MaterialBuilderContext const& context);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_NODES_RPR_API_MTLX_NODE_H
