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

#include "LOP_RPRExportHelper.h"

#include <OP/OP_OperatorTable.h>
#include <OP/OP_Operator.h>

#include <PRM/PRM_Include.h>

#include <LOP/LOP_Error.h>
#include <HUSD/HUSD_Utils.h>
#include <HUSD/HUSD_TimeCode.h>
#include <HUSD/XUSD_Data.h>
#include <HUSD/XUSD_Utils.h>

#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdRender/settings.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (rprExportPath)
    (rprExportAsSingleFile)
    (rprExportUseImageCache)
);

static PRM_Name g_exportPathName("exportPath", "Export Path");
static PRM_Name g_exportAsSingleFileName("exportAsSingleFile", "Export As Single File");
static PRM_Name g_exportUseImageCacheName("exportUseImageCache", "Use Image Cache");
static PRM_Name g_renderSettingsName("renderSettings", "Render Settings");

static PRM_Template g_templateList[] = {
    PRM_Template(PRM_FILE, 1, &g_exportPathName),
    PRM_Template(PRM_TOGGLE_E, 1, &g_exportAsSingleFileName),
    PRM_Template(PRM_TOGGLE_E, 1, &g_exportUseImageCacheName),
    PRM_Template(PRM_STRING_E, 1, &g_renderSettingsName),
    PRM_Template(),
};

void LOP_RPRExportHelper::Register(OP_OperatorTable* table) {
    auto opOperator = new OP_Operator(
        "rpr_lop_rprExportHelper",
        "RPR Export Helper",
        [](OP_Network *net, const char *name, OP_Operator *op) -> OP_Node* {
            return new LOP_RPRExportHelper(net, name, op);
        },
        g_templateList,
        0u,
        (unsigned)1);
    opOperator->setIconName("RPR");

    table->addOperator(opOperator);
}

LOP_RPRExportHelper::LOP_RPRExportHelper(OP_Network *net, const char *name, OP_Operator *op)
    : LOP_Node(net, name, op) {

}

template <typename T>
bool LOP_RPRExportHelper::SetRenderSetting(UsdPrim* prim, TfToken const& name, SdfValueTypeName const& sdfType, T const& value, bool timeDependent) {
    if (auto exportPathAttr = prim->CreateAttribute(name, sdfType, true)) {
        auto timeCode = timeDependent ? HUSDgetCurrentUsdTimeCode() : UsdTimeCode::Default();
        if (exportPathAttr.Set(value, timeCode)) {
            return true;
        }

        addError(LOP_MESSAGE, TfStringPrintf("Failed to set %s:%s", prim->GetPath().GetText(), name.GetText()).c_str());
    } else {
        addError(LOP_MESSAGE, TfStringPrintf("Failed to create %s attribute", name.GetText()).c_str());
    }

    return false;
}

OP_ERROR LOP_RPRExportHelper::cookMyLop(OP_Context &context) {
    if (cookModifyInput(context) >= UT_ERROR_FATAL) {
        return error();
    }

    auto getParm = [&](const UT_StringHolder &parmname) -> PRM_Parm* {
        int index = getParmList()->getParmIndex(parmname);
        if (index == -1) {
            return nullptr;
        }
        return getParmList()->getParmPtr(index);
    };

    auto exportPathParm = getParm(g_exportPathName.getToken());

    UT_String exportPath;
    exportPathParm->getValue(context.getTime(), exportPath, 0, true, context.getThread());

    if (!exportPath.isstring()) {
        return error();
    }

    if (!exportPath.endsWith(".rpr")) {
        addWarning(LOP_MESSAGE, "Export path must end with .rpr");
        exportPath += ".rpr";
    }

    bool exportAsSingleFile = bool(evalInt(g_exportAsSingleFileName.getToken(), 0, context.getTime()));
    bool exportUseImageCache = bool(evalInt(g_exportUseImageCacheName.getToken(), 0, context.getTime()));

    UT_String renderSettingsPath;
    evalString(renderSettingsPath, g_renderSettingsName.getToken(), 0, context.getTime());

    HUSD_AutoWriteLock writelock(editableDataHandle());
    HUSD_AutoLayerLock layerlock(writelock);

    UsdStageRefPtr stage = writelock.data()->stage();

    // Insert export file into each UsdRenderSetting primitive,
    // or if no UsdRenderSetting primitives exist create new one
    auto modifyRenderSettings = [&](UsdRenderSettings& renderSettings) {
        auto prim = renderSettings.GetPrim();

        return SetRenderSetting(&prim, _tokens->rprExportPath, SdfValueTypeNames->String, exportPath.toStdString(), exportPathParm->isTimeDependent()) &&
               SetRenderSetting(&prim, _tokens->rprExportAsSingleFile, SdfValueTypeNames->Bool, exportAsSingleFile, false) &&
               SetRenderSetting(&prim, _tokens->rprExportUseImageCache, SdfValueTypeNames->Bool, exportUseImageCache, false);
    };

    // Use explicitly specified render settings primitive if any
    //
    UsdRenderSettings renderSettings;
    if (renderSettingsPath.isstring()) {
        renderSettings = UsdRenderSettings(stage->GetPrimAtPath(HUSDgetSdfPath(renderSettingsPath)));
    }

    if (renderSettings) {
        modifyRenderSettings(renderSettings);
    } else {
        // If no valid render settings primitive was specified,
        // modify all available render settings on the stage
        // because we don't know which one will be selected implicitly
        //
        bool hasAnyRenderSettingsPrims = false;
        if (auto renderPrim = stage->GetPrimAtPath(SdfPath("/Render"))) {
            for (auto const& prim : renderPrim.GetDescendants()) {
                if (auto renderSettings = UsdRenderSettings(prim)) {
                    if (modifyRenderSettings(renderSettings)) {
                        hasAnyRenderSettingsPrims = true;
                    }
                }
            }
        }

        // But if there are no render settings primitives, create a new one
        // 
        if (!hasAnyRenderSettingsPrims) {
            SdfPath renderScopePath("/Render");
            if (auto renderScope = UsdGeomScope::Define(stage, renderScopePath)) {
                auto renderSettingsPath = renderScopePath.AppendElementString("rprExportRenderSettings");
                if (auto renderSettings = UsdRenderSettings::Define(stage, renderSettingsPath)) {
                    modifyRenderSettings(renderSettings);
                }
            }
        }
    }

    return error();
}

PXR_NAMESPACE_CLOSE_SCOPE
