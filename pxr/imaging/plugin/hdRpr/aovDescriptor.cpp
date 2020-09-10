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

#include "aovDescriptor.h"

#include "pxr/base/tf/instantiateSingleton.h"

#include "pxr/imaging/hd/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

const HdRprAovDescriptor kInvalidDesc;

TF_INSTANTIATE_SINGLETON(HdRprAovRegistry);
TF_DEFINE_PUBLIC_TOKENS(HdRprAovTokens, HDRPR_AOV_TOKENS);

HdRprAovRegistry::HdRprAovRegistry() {
    const auto rprAovMax = RPR_AOV_COLOR_RIGHT + 1;
    const GfVec4f idClearValue(255.0f, 255.0f, 255.0f, 0.0f);

    m_aovDescriptors.resize(rprAovMax);
    m_aovDescriptors[RPR_AOV_COLOR] = HdRprAovDescriptor(RPR_AOV_COLOR);
    m_aovDescriptors[RPR_AOV_DIFFUSE_ALBEDO] = HdRprAovDescriptor(RPR_AOV_DIFFUSE_ALBEDO); // XXX: RPR's albedo can be noisy in some cases, so we left it as multisampled
    m_aovDescriptors[RPR_AOV_VARIANCE] = HdRprAovDescriptor(RPR_AOV_VARIANCE);
    m_aovDescriptors[RPR_AOV_OPACITY] = HdRprAovDescriptor(RPR_AOV_OPACITY);
    m_aovDescriptors[RPR_AOV_EMISSION] = HdRprAovDescriptor(RPR_AOV_EMISSION);
    m_aovDescriptors[RPR_AOV_DIRECT_ILLUMINATION] = HdRprAovDescriptor(RPR_AOV_DIRECT_ILLUMINATION);
    m_aovDescriptors[RPR_AOV_INDIRECT_ILLUMINATION] = HdRprAovDescriptor(RPR_AOV_INDIRECT_ILLUMINATION);
    m_aovDescriptors[RPR_AOV_AO] = HdRprAovDescriptor(RPR_AOV_AO);
    m_aovDescriptors[RPR_AOV_DIRECT_DIFFUSE] = HdRprAovDescriptor(RPR_AOV_DIRECT_DIFFUSE);
    m_aovDescriptors[RPR_AOV_DIRECT_REFLECT] = HdRprAovDescriptor(RPR_AOV_DIRECT_REFLECT);
    m_aovDescriptors[RPR_AOV_INDIRECT_DIFFUSE] = HdRprAovDescriptor(RPR_AOV_INDIRECT_DIFFUSE);
    m_aovDescriptors[RPR_AOV_INDIRECT_REFLECT] = HdRprAovDescriptor(RPR_AOV_INDIRECT_REFLECT);
    m_aovDescriptors[RPR_AOV_REFRACT] = HdRprAovDescriptor(RPR_AOV_REFRACT);
    m_aovDescriptors[RPR_AOV_VOLUME] = HdRprAovDescriptor(RPR_AOV_VOLUME);
    m_aovDescriptors[RPR_AOV_LIGHT_GROUP0] = HdRprAovDescriptor(RPR_AOV_LIGHT_GROUP0);
    m_aovDescriptors[RPR_AOV_LIGHT_GROUP1] = HdRprAovDescriptor(RPR_AOV_LIGHT_GROUP1);
    m_aovDescriptors[RPR_AOV_LIGHT_GROUP2] = HdRprAovDescriptor(RPR_AOV_LIGHT_GROUP2);
    m_aovDescriptors[RPR_AOV_LIGHT_GROUP3] = HdRprAovDescriptor(RPR_AOV_LIGHT_GROUP3);
    m_aovDescriptors[RPR_AOV_COLOR_RIGHT] = HdRprAovDescriptor(RPR_AOV_COLOR_RIGHT);
    m_aovDescriptors[RPR_AOV_SHADOW_CATCHER] = HdRprAovDescriptor(RPR_AOV_SHADOW_CATCHER);
    m_aovDescriptors[RPR_AOV_REFLECTION_CATCHER] = HdRprAovDescriptor(RPR_AOV_REFLECTION_CATCHER);

    m_aovDescriptors[RPR_AOV_DEPTH] = HdRprAovDescriptor(RPR_AOV_DEPTH, false, HdFormatFloat32, GfVec4f(std::numeric_limits<float>::infinity()));
    m_aovDescriptors[RPR_AOV_UV] = HdRprAovDescriptor(RPR_AOV_UV, false, HdFormatFloat32Vec3);
    m_aovDescriptors[RPR_AOV_SHADING_NORMAL] = HdRprAovDescriptor(RPR_AOV_SHADING_NORMAL, false, HdFormatFloat32Vec3);
    m_aovDescriptors[RPR_AOV_GEOMETRIC_NORMAL] = HdRprAovDescriptor(RPR_AOV_GEOMETRIC_NORMAL, false);
    m_aovDescriptors[RPR_AOV_OBJECT_ID] = HdRprAovDescriptor(RPR_AOV_OBJECT_ID, false, HdFormatInt32, idClearValue);
    m_aovDescriptors[RPR_AOV_MATERIAL_ID] = HdRprAovDescriptor(RPR_AOV_MATERIAL_ID, false, HdFormatInt32, idClearValue);
    m_aovDescriptors[RPR_AOV_OBJECT_GROUP_ID] = HdRprAovDescriptor(RPR_AOV_OBJECT_GROUP_ID, false, HdFormatInt32, idClearValue);
    m_aovDescriptors[RPR_AOV_WORLD_COORDINATE] = HdRprAovDescriptor(RPR_AOV_WORLD_COORDINATE, false);
    m_aovDescriptors[RPR_AOV_BACKGROUND] = HdRprAovDescriptor(RPR_AOV_BACKGROUND, false);
    m_aovDescriptors[RPR_AOV_VELOCITY] = HdRprAovDescriptor(RPR_AOV_VELOCITY, false);
    m_aovDescriptors[RPR_AOV_VIEW_SHADING_NORMAL] = HdRprAovDescriptor(RPR_AOV_VIEW_SHADING_NORMAL, false);

    m_computedAovDescriptors.resize(kComputedAovsCount);
    m_computedAovDescriptors[kNdcDepth] = HdRprAovDescriptor(kNdcDepth, false, HdFormatFloat32, GfVec4f(std::numeric_limits<float>::infinity()), true);
    m_computedAovDescriptors[kColorAlpha] = HdRprAovDescriptor(kColorAlpha, true, HdFormatFloat32Vec4, GfVec4f(0.0f), true);

    auto addAovNameLookup = [this](TfToken const& name, HdRprAovDescriptor const& descriptor) {
        auto status = m_aovNameLookup.emplace(name, AovNameLookupValue(descriptor.id, descriptor.computed));
        if (!status.second) {
            TF_CODING_ERROR("AOV lookup name should be unique");
        }
    };

    addAovNameLookup(HdAovTokens->color, m_computedAovDescriptors[kColorAlpha]);
    addAovNameLookup(HdAovTokens->normal, m_aovDescriptors[RPR_AOV_SHADING_NORMAL]);
    addAovNameLookup(HdAovTokens->primId, m_aovDescriptors[RPR_AOV_OBJECT_ID]);
    addAovNameLookup(HdAovTokens->Neye, m_aovDescriptors[RPR_AOV_VIEW_SHADING_NORMAL]);
    addAovNameLookup(HdAovTokens->depth, m_computedAovDescriptors[kNdcDepth]);
    addAovNameLookup(HdRprGetCameraDepthAovName(), m_aovDescriptors[RPR_AOV_DEPTH]);

    addAovNameLookup(HdRprAovTokens->rawColor, m_aovDescriptors[RPR_AOV_COLOR]);
    addAovNameLookup(HdRprAovTokens->albedo, m_aovDescriptors[RPR_AOV_DIFFUSE_ALBEDO]);
    addAovNameLookup(HdRprAovTokens->variance, m_aovDescriptors[RPR_AOV_VARIANCE]);
    addAovNameLookup(HdRprAovTokens->opacity, m_aovDescriptors[RPR_AOV_OPACITY]);
    addAovNameLookup(HdRprAovTokens->emission, m_aovDescriptors[RPR_AOV_EMISSION]);
    addAovNameLookup(HdRprAovTokens->directIllumination, m_aovDescriptors[RPR_AOV_DIRECT_ILLUMINATION]);
    addAovNameLookup(HdRprAovTokens->indirectIllumination, m_aovDescriptors[RPR_AOV_INDIRECT_ILLUMINATION]);
    addAovNameLookup(HdRprAovTokens->ao, m_aovDescriptors[RPR_AOV_AO]);
    addAovNameLookup(HdRprAovTokens->directDiffuse, m_aovDescriptors[RPR_AOV_DIRECT_DIFFUSE]);
    addAovNameLookup(HdRprAovTokens->directReflect, m_aovDescriptors[RPR_AOV_DIRECT_REFLECT]);
    addAovNameLookup(HdRprAovTokens->indirectDiffuse, m_aovDescriptors[RPR_AOV_INDIRECT_DIFFUSE]);
    addAovNameLookup(HdRprAovTokens->indirectReflect, m_aovDescriptors[RPR_AOV_INDIRECT_REFLECT]);
    addAovNameLookup(HdRprAovTokens->refract, m_aovDescriptors[RPR_AOV_REFRACT]);
    addAovNameLookup(HdRprAovTokens->volume, m_aovDescriptors[RPR_AOV_VOLUME]);
    addAovNameLookup(HdRprAovTokens->lightGroup0, m_aovDescriptors[RPR_AOV_LIGHT_GROUP0]);
    addAovNameLookup(HdRprAovTokens->lightGroup1, m_aovDescriptors[RPR_AOV_LIGHT_GROUP1]);
    addAovNameLookup(HdRprAovTokens->lightGroup2, m_aovDescriptors[RPR_AOV_LIGHT_GROUP2]);
    addAovNameLookup(HdRprAovTokens->lightGroup3, m_aovDescriptors[RPR_AOV_LIGHT_GROUP3]);
    addAovNameLookup(HdRprAovTokens->colorRight, m_aovDescriptors[RPR_AOV_COLOR_RIGHT]);
    addAovNameLookup(HdRprAovTokens->materialId, m_aovDescriptors[RPR_AOV_MATERIAL_ID]);
    addAovNameLookup(HdRprAovTokens->objectGroupId, m_aovDescriptors[RPR_AOV_OBJECT_GROUP_ID]);
    addAovNameLookup(HdRprAovTokens->geometricNormal, m_aovDescriptors[RPR_AOV_GEOMETRIC_NORMAL]);
    addAovNameLookup(HdRprAovTokens->worldCoordinate, m_aovDescriptors[RPR_AOV_WORLD_COORDINATE]);
    addAovNameLookup(HdRprAovTokens->primvarsSt, m_aovDescriptors[RPR_AOV_UV]);
    addAovNameLookup(HdRprAovTokens->shadowCatcher, m_aovDescriptors[RPR_AOV_SHADOW_CATCHER]);
    addAovNameLookup(HdRprAovTokens->reflectionCatcher, m_aovDescriptors[RPR_AOV_REFLECTION_CATCHER]);
    addAovNameLookup(HdRprAovTokens->background, m_aovDescriptors[RPR_AOV_BACKGROUND]);
    addAovNameLookup(HdRprAovTokens->velocity, m_aovDescriptors[RPR_AOV_VELOCITY]);
    addAovNameLookup(HdRprAovTokens->viewShadingNormal, m_aovDescriptors[RPR_AOV_VIEW_SHADING_NORMAL]);
}

HdRprAovDescriptor const& HdRprAovRegistry::GetAovDesc(TfToken const& name) {
    auto it = m_aovNameLookup.find(name);
    if (it == m_aovNameLookup.end()) {
        return kInvalidDesc;
    }

    return GetAovDesc(it->second.id, it->second.isComputed);
}

HdRprAovDescriptor const& HdRprAovRegistry::GetAovDesc(uint32_t id, bool computed) {
    size_t descsSize = computed ? m_computedAovDescriptors.size() : m_aovDescriptors.size();
    if (id >= descsSize) {
        TF_RUNTIME_ERROR("Invalid arguments: %#x (computed=%d)", id, int(computed));
        return kInvalidDesc;
    }

    if (computed) {
        return m_computedAovDescriptors[id];
    } else {
        return m_aovDescriptors[id];
    }
}

TfToken const& HdRprGetCameraDepthAovName() {
#if PXR_VERSION < 2002
    return HdAovTokens->linearDepth;
#else
    return HdAovTokens->cameraDepth;
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE
