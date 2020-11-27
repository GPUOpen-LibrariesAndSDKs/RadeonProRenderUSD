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

#include "VOP_RPRMaterial.h"
#include "LOP_RPRExportHelper.h"
#include "LOP_RPRMaterialProperties.h"
#include "pxr/imaging/rprUsd/materialRegistry.h"

#include <OP/OP_OperatorTable.h>
#include <UT/UT_DSOVersion.h>

void newVopOperator(OP_OperatorTable* io_table) {
    PXR_NAMESPACE_USING_DIRECTIVE
    for (auto& nodeDesc : RprUsdMaterialRegistry::GetInstance().GetRegisteredNodes()) {
        if (!nodeDesc.info) continue;

        try {
            io_table->addOperator(VOP_RPRMaterialOperator::Create(nodeDesc.info));
        } catch (std::exception& e) {
            fprintf(stderr, "Failed to add %s VOP", nodeDesc.info->GetName());
        }
    }
}

void newLopOperator(OP_OperatorTable *table) {
    PXR_NAMESPACE_USING_DIRECTIVE
    LOP_RPRExportHelper::Register(table);
    LOP_RPRMaterialProperties::Register(table);
}
