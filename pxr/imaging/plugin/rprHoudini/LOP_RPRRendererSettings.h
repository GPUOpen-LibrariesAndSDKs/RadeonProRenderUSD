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

#ifndef RPR_LOP_RPRRENDERERSETTINGS_H
#define RPR_LOP_RPRRENDERERSETTINGS_H

#include "pxr/pxr.h"

#include <LOP/LOP_Node.h>

PXR_NAMESPACE_OPEN_SCOPE

/// This node exposes RPR specific render settings
class LOP_RPRRendererSettings : public LOP_Node {
public:
    LOP_RPRRendererSettings(OP_Network *net, const char *name, OP_Operator *op);
    ~LOP_RPRRendererSettings() override = default;

    static void Register(OP_OperatorTable *table);

protected:
    OP_ERROR cookMyLop(OP_Context &context) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPR_LOP_RPRRENDERERSETTINGS_H
