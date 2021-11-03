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

#include "LOP_RPRMaterialProperties.h"

#include <OP/OP_OperatorTable.h>
#include <OP/OP_Operator.h>

#include <PRM/PRM_Include.h>

#include <LOP/LOP_PRMShared.h>
#include <LOP/LOP_Error.h>
#include <HUSD/HUSD_Utils.h>
#include <HUSD/XUSD_Data.h>
#include <HUSD/XUSD_Utils.h>

#include <pxr/usd/usd/stage.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/imaging/rprUsd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

static PRM_Name g_materialPath("materialPath", "Material Path");
static PRM_Name g_id("id", "ID");
static PRM_Name g_cryptomatteName("cryptomatteName", "Cryptomatte Name");

static PRM_Template g_templateList[] = {
    PRM_Template(PRM_STRING_E, 1, &g_materialPath),
    PRM_Template(PRM_INT, 1, &g_id, nullptr, nullptr, nullptr, nullptr, nullptr, 1,
        "The ID that corresponds to ID on materialId AOV."),
    PRM_Template(PRM_STRING_E, 1, &g_cryptomatteName, nullptr, nullptr, nullptr, nullptr, nullptr, 1,
        "String used to generate cryptomatte ID. If not specified, the path to a primitive used."),
    PRM_Template()
};

void LOP_RPRMaterialProperties::Register(OP_OperatorTable* table) {
    auto opOperator = new OP_Operator(
        "rpr_LOP_RPRMaterialProperties",
        "RPR Material Properties",
        [](OP_Network *net, const char *name, OP_Operator *op) -> OP_Node* {
            return new LOP_RPRMaterialProperties(net, name, op);
        },
        g_templateList,
        0u,
        (unsigned)1);
    opOperator->setIconName("RPR");

    table->addOperator(opOperator);
}

LOP_RPRMaterialProperties::LOP_RPRMaterialProperties(OP_Network *net, const char *name, OP_Operator *op)
    : LOP_Node(net, name, op) {

}

OP_ERROR LOP_RPRMaterialProperties::cookMyLop(OP_Context &context) {
    if (cookModifyInput(context) >= UT_ERROR_FATAL) {
        return error();
    }

    UT_String materialPath;
    evalString(materialPath, g_materialPath.getToken(), 0, context.getTime());
    HUSDmakeValidUsdPath(materialPath, true);

    if (!materialPath.isstring()) {
        return error();
    }
    SdfPath materialSdfPath(HUSDgetSdfPath(materialPath));

    int id = evalInt(g_id.getToken(), 0, context.getTime());

    UT_String cryptomatteName;
    evalString(cryptomatteName, g_cryptomatteName.getToken(), 0, context.getTime());

    HUSD_AutoWriteLock writelock(editableDataHandle());
    HUSD_AutoLayerLock layerlock(writelock);

    auto data = writelock.data();
    if (!data) {
        // This might be the case when there are errors in the current graph
        return error();
    }

    UsdStageRefPtr stage = writelock.data()->stage();

    UsdPrim material = stage->GetPrimAtPath(materialSdfPath);
    if (!material) {
        addError(LOP_MESSAGE, TfStringPrintf("Material with %s path does not exist", materialPath.c_str()).c_str());
        return error();
    }

    material.CreateAttribute(RprUsdTokens->rprMaterialId, SdfValueTypeNames->Int).Set(id);
    material.CreateAttribute(RprUsdTokens->rprMaterialAssetName, SdfValueTypeNames->String).Set(VtValue(std::string(cryptomatteName)));

    return error();
}

PXR_NAMESPACE_CLOSE_SCOPE
