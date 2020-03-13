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
