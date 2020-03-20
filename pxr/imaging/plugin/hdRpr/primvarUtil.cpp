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

#include "primvarUtil.h"
#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(HdRprGeometryPrimvarTokens,
    ((subdivisionLevel, "rpr:subdivisionLevel"))
    ((visibilityPrimary, "rpr:visibilityPrimary"))
    ((visibilityShadow, "rpr:visibilityShadow"))
    ((visibilityReflection, "rpr:visibilityReflection"))
    ((visibilityGlossyReflection, "rpr:visibilityGlossyReflection"))
    ((visibilityRefraction, "rpr:visibilityRefraction"))
    ((visibilityGlossyRefraction, "rpr:visibilityGlossyRefraction"))
    ((visibilityDiffuse, "rpr:visibilityDiffuse"))
    ((visibilityTransparent, "rpr:visibilityTransparent"))
    ((visibilityLight, "rpr:visibilityLight"))
);

void HdRpr_ParseGeometrySettings(
    HdSceneDelegate* sceneDelegate, SdfPath const& id,
    HdPrimvarDescriptorVector const& constantPrimvarDescs,
    HdRprGeometrySettings* geomSettings) {

    auto setVisibilityFlag = [&sceneDelegate, &id, &geomSettings]
        (TfToken const& primvarName, HdRprVisibilityFlag flag) {
        bool toggle;
        if (!HdRpr_GetConstantPrimvar(primvarName, sceneDelegate, id, &toggle)) {
            return;
        }

        if (toggle) {
            geomSettings->visibilityMask |= flag;
        } else {
            geomSettings->visibilityMask &= ~flag;
        }
    };

    for (auto& desc : constantPrimvarDescs) {
        if (desc.name == HdRprGeometryPrimvarTokens->subdivisionLevel) {
            int subdivisionLevel;
            if (HdRpr_GetConstantPrimvar(HdRprGeometryPrimvarTokens->subdivisionLevel, sceneDelegate, id, &subdivisionLevel)) {
                geomSettings->subdivisionLevel = std::max(0, std::min(subdivisionLevel, 7));
            }
        } else if (desc.name == HdRprGeometryPrimvarTokens->visibilityPrimary) {
            setVisibilityFlag(HdRprGeometryPrimvarTokens->visibilityPrimary, kVisiblePrimary);
        } else if (desc.name == HdRprGeometryPrimvarTokens->visibilityShadow) {
            setVisibilityFlag(HdRprGeometryPrimvarTokens->visibilityShadow, kVisibleShadow);
        } else if (desc.name == HdRprGeometryPrimvarTokens->visibilityReflection) {
            setVisibilityFlag(HdRprGeometryPrimvarTokens->visibilityReflection, kVisibleReflection);
        } else if (desc.name == HdRprGeometryPrimvarTokens->visibilityGlossyReflection) {
            setVisibilityFlag(HdRprGeometryPrimvarTokens->visibilityGlossyReflection, kVisibleGlossyReflection);
        } else if (desc.name == HdRprGeometryPrimvarTokens->visibilityRefraction) {
            setVisibilityFlag(HdRprGeometryPrimvarTokens->visibilityRefraction, kVisibleRefraction);
        } else if (desc.name == HdRprGeometryPrimvarTokens->visibilityGlossyRefraction) {
            setVisibilityFlag(HdRprGeometryPrimvarTokens->visibilityGlossyRefraction, kVisibleGlossyRefraction);
        } else if (desc.name == HdRprGeometryPrimvarTokens->visibilityDiffuse) {
            setVisibilityFlag(HdRprGeometryPrimvarTokens->visibilityDiffuse, kVisibleDiffuse);
        } else if (desc.name == HdRprGeometryPrimvarTokens->visibilityTransparent) {
            setVisibilityFlag(HdRprGeometryPrimvarTokens->visibilityTransparent, kVisibleTransparent);
        } else if (desc.name == HdRprGeometryPrimvarTokens->visibilityLight) {
            setVisibilityFlag(HdRprGeometryPrimvarTokens->visibilityLight, kVisibleLight);
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
