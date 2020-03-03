#include "renderParam.h"

#include "pxr/base/tf/envSetting.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdRprMaterialNetworkSelectorTokens, HDRPR_MATERIAL_NETWORK_SELECTOR_TOKENS);

TF_DEFINE_ENV_SETTING(HDRPR_MATERIAL_NETWORK_SELECTOR, HDRPR_DEFAULT_MATERIAL_NETWORK_SELECTOR,
        "Material network selector to be used in hdRpr");

void HdRprRenderParam::InitializeEnvParameters() {
    m_materialNetworkSelector = TfToken(TfGetEnvSetting(HDRPR_MATERIAL_NETWORK_SELECTOR));
}

PXR_NAMESPACE_CLOSE_SCOPE
