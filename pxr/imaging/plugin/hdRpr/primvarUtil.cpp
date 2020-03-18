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

TF_DEFINE_PUBLIC_TOKENS(HdRprPrimvarTokens, HDRPR_PRIMVAR_TOKENS);

TF_DEFINE_PRIVATE_TOKENS(HdRprRayTypeTokens,
    (primary)
    (shadow)
    (reflection)
    (refraction)
    (transparent)
    (diffuse)
    (glossyReflection)
    (glossyRefraction)
    (light)
);

uint32_t HdRpr_ParseVisibilityMask(std::string const& visibilityMask) {
    // The visibility mask is a comma-separated list of inclusive or exclusive ray visibility flags.
    // For example, "primary,shadow" means that object is visible only for primary and shadow rays;
    // "-primary,-light,-shadow" - visible for all ray types except primary, light and shadow rays.
    // Mixing inclusion and exclusion do not make sense.
    // Exclusion flag will be prioritized in case of mixing, i.e. inclusion flags ignored.

    struct RayTypeDesc {
        TfToken name;
        HdRprVisibilityFlag flag;
    };
    static RayTypeDesc rayTypeDescs[] = {
        {HdRprRayTypeTokens->primary, kVisiblePrimary},
        {HdRprRayTypeTokens->shadow, kVisibleShadow},
        {HdRprRayTypeTokens->reflection, kVisibleReflection},
        {HdRprRayTypeTokens->refraction, kVisibleRefraction},
        {HdRprRayTypeTokens->transparent, kVisibleTransparent},
        {HdRprRayTypeTokens->diffuse, kVisibleDiffuse},
        {HdRprRayTypeTokens->glossyReflection, kVisibleGlossyReflection},
        {HdRprRayTypeTokens->glossyRefraction, kVisibleGlossyRefraction},
        {HdRprRayTypeTokens->light, kVisibleLight},
    };

    if (visibilityMask == "*") {
        return kVisibleAll;
    }

    uint32_t includedMask = 0;
    uint32_t excludedMask = 0;
    for (size_t offset = 0; offset < visibilityMask.size();) {
        if (visibilityMask[offset] == ',') {
            ++offset;
        } else {
            bool excludeFlag = false;
            if (visibilityMask[offset] == '-') {
                excludeFlag = true;
                ++offset;
            }

            size_t next = visibilityMask.find(',', offset);

            for (auto& desc : rayTypeDescs) {
                if (!std::strncmp(desc.name.GetText(), visibilityMask.c_str() + offset, desc.name.size())) {
                    if (excludeFlag) {
                        excludedMask |= desc.flag;
                    } else {
                        includedMask |= desc.flag;
                    }
                    break;
                }
            }

            size_t currentLen = (next == std::string::npos ? visibilityMask.size() : next) - offset;
            offset += currentLen;
        }
    }

    if (excludedMask) {
        // Everything is included, exclude only those from excludedFlags
        return (~excludedMask) & kVisibleAll;
    } else {
        // Everything is excluded, include only those from includedFlags
        return includedMask;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
