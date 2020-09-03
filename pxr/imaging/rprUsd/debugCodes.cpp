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

#include "pxr/imaging/rprUsd/debugCodes.h"

#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/registryManager.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug) {
    TF_DEBUG_ENVIRONMENT_SYMBOL(RPR_USD_DEBUG_CORE_UNSUPPORTED_ERROR, "signal about unsupported errors");
    TF_DEBUG_ENVIRONMENT_SYMBOL(RPR_USD_DEBUG_DUMP_MATERIALS, "Dump material networks to the files in the current working directory")
    TF_DEBUG_ENVIRONMENT_SYMBOL(RPR_USD_DEBUG_LEAKS, "signal about rpr_context leaks");
}

bool RprUsdIsLeakCheckEnabled() {
    static bool forceLeakCheck = false;
    return forceLeakCheck || TfDebug::IsEnabled(RPR_USD_DEBUG_LEAKS);
}

PXR_NAMESPACE_CLOSE_SCOPE
