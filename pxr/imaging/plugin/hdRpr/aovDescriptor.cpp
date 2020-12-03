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
    const auto rprAovMax = RPR_AOV_CRYPTOMATTE_OBJ2 + 1;
    const GfVec4f idClearValue(255.0f, 255.0f, 255.0f, 0.0f);

    m_aovDescriptors.resize(rprAovMax);

    // multisampled vec4f AOVs
    for (auto rprAovId : {
            RPR_AOV_COLOR,
            RPR_AOV_DIFFUSE_ALBEDO, // XXX: RPR's albedo can be noisy in some cases, so we left it as multisampled
            RPR_AOV_VARIANCE,
            RPR_AOV_OPACITY,
            RPR_AOV_EMISSION,
            RPR_AOV_DIRECT_ILLUMINATION,
            RPR_AOV_INDIRECT_ILLUMINATION,
            RPR_AOV_AO,
            RPR_AOV_DIRECT_DIFFUSE,
            RPR_AOV_DIRECT_REFLECT,
            RPR_AOV_INDIRECT_DIFFUSE,
            RPR_AOV_INDIRECT_REFLECT,
            RPR_AOV_REFRACT,
            RPR_AOV_VOLUME,
            RPR_AOV_LIGHT_GROUP0,
            RPR_AOV_LIGHT_GROUP1,
            RPR_AOV_LIGHT_GROUP2,
            RPR_AOV_LIGHT_GROUP3,
            RPR_AOV_COLOR_RIGHT,
            RPR_AOV_SHADOW_CATCHER,
            RPR_AOV_REFLECTION_CATCHER,
            RPR_AOV_LPE_0,
            RPR_AOV_LPE_1,
            RPR_AOV_LPE_2,
            RPR_AOV_LPE_3,
            RPR_AOV_LPE_4,
            RPR_AOV_LPE_5,
            RPR_AOV_LPE_6,
            RPR_AOV_LPE_7,
            RPR_AOV_LPE_8,
            RPR_AOV_CRYPTOMATTE_MAT0,
            RPR_AOV_CRYPTOMATTE_MAT1,
            RPR_AOV_CRYPTOMATTE_MAT2,
            RPR_AOV_CRYPTOMATTE_OBJ0,
            RPR_AOV_CRYPTOMATTE_OBJ1,
            RPR_AOV_CRYPTOMATTE_OBJ2,
        }) {
        m_aovDescriptors[rprAovId] = HdRprAovDescriptor(rprAovId);
    }

    // singlesampled AOVs
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
    m_aovDescriptors[RPR_AOV_CAMERA_NORMAL] = HdRprAovDescriptor(RPR_AOV_CAMERA_NORMAL, false);

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
    addAovNameLookup(HdRprAovTokens->lpe0, m_aovDescriptors[RPR_AOV_LPE_0]);
    addAovNameLookup(HdRprAovTokens->lpe1, m_aovDescriptors[RPR_AOV_LPE_1]);
    addAovNameLookup(HdRprAovTokens->lpe2, m_aovDescriptors[RPR_AOV_LPE_2]);
    addAovNameLookup(HdRprAovTokens->lpe3, m_aovDescriptors[RPR_AOV_LPE_3]);
    addAovNameLookup(HdRprAovTokens->lpe4, m_aovDescriptors[RPR_AOV_LPE_4]);
    addAovNameLookup(HdRprAovTokens->lpe5, m_aovDescriptors[RPR_AOV_LPE_5]);
    addAovNameLookup(HdRprAovTokens->lpe6, m_aovDescriptors[RPR_AOV_LPE_6]);
    addAovNameLookup(HdRprAovTokens->lpe7, m_aovDescriptors[RPR_AOV_LPE_7]);
    addAovNameLookup(HdRprAovTokens->lpe8, m_aovDescriptors[RPR_AOV_LPE_8]);
    addAovNameLookup(HdRprAovTokens->cameraNormal, m_aovDescriptors[RPR_AOV_CAMERA_NORMAL]);
    addAovNameLookup(HdRprAovTokens->cryptomatteMat0, m_aovDescriptors[RPR_AOV_CRYPTOMATTE_MAT0]);
    addAovNameLookup(HdRprAovTokens->cryptomatteMat1, m_aovDescriptors[RPR_AOV_CRYPTOMATTE_MAT1]);
    addAovNameLookup(HdRprAovTokens->cryptomatteMat2, m_aovDescriptors[RPR_AOV_CRYPTOMATTE_MAT2]);
    addAovNameLookup(HdRprAovTokens->cryptomatteObj0, m_aovDescriptors[RPR_AOV_CRYPTOMATTE_OBJ0]);
    addAovNameLookup(HdRprAovTokens->cryptomatteObj1, m_aovDescriptors[RPR_AOV_CRYPTOMATTE_OBJ1]);
    addAovNameLookup(HdRprAovTokens->cryptomatteObj2, m_aovDescriptors[RPR_AOV_CRYPTOMATTE_OBJ2]);
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
