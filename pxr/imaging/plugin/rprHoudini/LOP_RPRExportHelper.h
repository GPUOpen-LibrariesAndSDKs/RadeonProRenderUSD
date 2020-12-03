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

#ifndef RPR_LOP_RPREXPORTHELPER_H
#define RPR_LOP_RPREXPORTHELPER_H

#include "pxr/pxr.h"
#include "pxr/usd/usd/prim.h"

#include <LOP/LOP_Node.h>

PXR_NAMESPACE_OPEN_SCOPE

/// This node sets rprExportPath render settings to a particular UsdRenderSettings primitive.
/// It's impossible to implement needed functionality in a custom HDA, that's why this node was implemented.
class LOP_RPRExportHelper : public LOP_Node {
public:
    LOP_RPRExportHelper(OP_Network *net, const char *name, OP_Operator *op);
    ~LOP_RPRExportHelper() override = default;

    static void Register(OP_OperatorTable *table);

protected:
    OP_ERROR cookMyLop(OP_Context &context) override;

private:
    template <typename T>
    bool SetRenderSetting(UsdPrim* prim, TfToken const& name, SdfValueTypeName const& sdfType, T const& value, bool timeDependent);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPR_LOP_RPREXPORTHELPER_H
