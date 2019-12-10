#include "debugCodes.h"

#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/registryManager.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug) {
    TF_DEBUG_ENVIRONMENT_SYMBOL(HD_RPR_DEBUG_CONTEXT_CREATION, "hdRpr context creation");
}

PXR_NAMESPACE_CLOSE_SCOPE
