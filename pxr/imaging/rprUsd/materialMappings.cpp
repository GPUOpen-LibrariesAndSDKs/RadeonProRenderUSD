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

#include "pxr/imaging/rprUsd/materialMappings.h"

#include <map>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(RprUsdMaterialNodeInputTokens, RPRUSD_MATERIAL_NODE_INPUT_TOKENS);
TF_DEFINE_PUBLIC_TOKENS(RprUsdMaterialNodeTypeTokens, RPRUSD_MATERIAL_NODE_TYPE_TOKENS);
TF_DEFINE_PUBLIC_TOKENS(RprUsdMaterialInputModeTokens, RPRUSD_MATERIAL_INPUT_MODE_TOKENS);

bool ToRpr(TfToken const& id, rpr::MaterialNodeType* out, bool pedantic) {
    static const std::map<TfToken, rpr::MaterialNodeType> s_mapping = {
        {RprUsdMaterialNodeTypeTokens->diffuse, RPR_MATERIAL_NODE_DIFFUSE},
        {RprUsdMaterialNodeTypeTokens->microfacet, RPR_MATERIAL_NODE_MICROFACET},
        {RprUsdMaterialNodeTypeTokens->reflection, RPR_MATERIAL_NODE_REFLECTION},
        {RprUsdMaterialNodeTypeTokens->refraction, RPR_MATERIAL_NODE_REFRACTION},
        {RprUsdMaterialNodeTypeTokens->microfacet_refraction, RPR_MATERIAL_NODE_MICROFACET_REFRACTION},
        {RprUsdMaterialNodeTypeTokens->transparent, RPR_MATERIAL_NODE_TRANSPARENT},
        {RprUsdMaterialNodeTypeTokens->emissive, RPR_MATERIAL_NODE_EMISSIVE},
        {RprUsdMaterialNodeTypeTokens->ward, RPR_MATERIAL_NODE_WARD},
        {RprUsdMaterialNodeTypeTokens->add, RPR_MATERIAL_NODE_ADD},
        {RprUsdMaterialNodeTypeTokens->blend, RPR_MATERIAL_NODE_BLEND},
        {RprUsdMaterialNodeTypeTokens->arithmetic, RPR_MATERIAL_NODE_ARITHMETIC},
        {RprUsdMaterialNodeTypeTokens->fresnel, RPR_MATERIAL_NODE_FRESNEL},
        {RprUsdMaterialNodeTypeTokens->normal_map, RPR_MATERIAL_NODE_NORMAL_MAP},
        {RprUsdMaterialNodeTypeTokens->image_texture, RPR_MATERIAL_NODE_IMAGE_TEXTURE},
        {RprUsdMaterialNodeTypeTokens->noise2d_texture, RPR_MATERIAL_NODE_NOISE2D_TEXTURE},
        {RprUsdMaterialNodeTypeTokens->dot_texture, RPR_MATERIAL_NODE_DOT_TEXTURE},
        {RprUsdMaterialNodeTypeTokens->gradient_texture, RPR_MATERIAL_NODE_GRADIENT_TEXTURE},
        {RprUsdMaterialNodeTypeTokens->checker_texture, RPR_MATERIAL_NODE_CHECKER_TEXTURE},
        {RprUsdMaterialNodeTypeTokens->constant_texture, RPR_MATERIAL_NODE_CONSTANT_TEXTURE},
        {RprUsdMaterialNodeTypeTokens->input_lookup, RPR_MATERIAL_NODE_INPUT_LOOKUP},
        {RprUsdMaterialNodeTypeTokens->blend_value, RPR_MATERIAL_NODE_BLEND_VALUE},
        {RprUsdMaterialNodeTypeTokens->passthrough, RPR_MATERIAL_NODE_PASSTHROUGH},
        {RprUsdMaterialNodeTypeTokens->orennayar, RPR_MATERIAL_NODE_ORENNAYAR},
        {RprUsdMaterialNodeTypeTokens->fresnel_schlick, RPR_MATERIAL_NODE_FRESNEL_SCHLICK},
        {RprUsdMaterialNodeTypeTokens->diffuse_refraction, RPR_MATERIAL_NODE_DIFFUSE_REFRACTION},
        {RprUsdMaterialNodeTypeTokens->bump_map, RPR_MATERIAL_NODE_BUMP_MAP},
        {RprUsdMaterialNodeTypeTokens->volume, RPR_MATERIAL_NODE_VOLUME},
        {RprUsdMaterialNodeTypeTokens->microfacet_anisotropic_reflection, RPR_MATERIAL_NODE_MICROFACET_ANISOTROPIC_REFLECTION},
        {RprUsdMaterialNodeTypeTokens->microfacet_anisotropic_refraction, RPR_MATERIAL_NODE_MICROFACET_ANISOTROPIC_REFRACTION},
        {RprUsdMaterialNodeTypeTokens->twosided, RPR_MATERIAL_NODE_TWOSIDED},
        {RprUsdMaterialNodeTypeTokens->uv_procedural, RPR_MATERIAL_NODE_UV_PROCEDURAL},
        {RprUsdMaterialNodeTypeTokens->microfacet_beckmann, RPR_MATERIAL_NODE_MICROFACET_BECKMANN},
        {RprUsdMaterialNodeTypeTokens->phong, RPR_MATERIAL_NODE_PHONG},
        {RprUsdMaterialNodeTypeTokens->buffer_sampler, RPR_MATERIAL_NODE_BUFFER_SAMPLER},
        {RprUsdMaterialNodeTypeTokens->uv_triplanar, RPR_MATERIAL_NODE_UV_TRIPLANAR},
        {RprUsdMaterialNodeTypeTokens->ao_map, RPR_MATERIAL_NODE_AO_MAP},
        {RprUsdMaterialNodeTypeTokens->user_texture_0, RPR_MATERIAL_NODE_USER_TEXTURE_0},
        {RprUsdMaterialNodeTypeTokens->user_texture_1, RPR_MATERIAL_NODE_USER_TEXTURE_1},
        {RprUsdMaterialNodeTypeTokens->user_texture_2, RPR_MATERIAL_NODE_USER_TEXTURE_2},
        {RprUsdMaterialNodeTypeTokens->user_texture_3, RPR_MATERIAL_NODE_USER_TEXTURE_3},
        {RprUsdMaterialNodeTypeTokens->uberv2, RPR_MATERIAL_NODE_UBERV2},
        {RprUsdMaterialNodeTypeTokens->transform, RPR_MATERIAL_NODE_TRANSFORM},
        {RprUsdMaterialNodeTypeTokens->rgb_to_hsv, RPR_MATERIAL_NODE_RGB_TO_HSV},
        {RprUsdMaterialNodeTypeTokens->hsv_to_rgb, RPR_MATERIAL_NODE_HSV_TO_RGB},
    };

    auto it = s_mapping.find(id);
    if (it == s_mapping.end()) {
        if (pedantic) {
            TF_CODING_ERROR("Invalid rpr::MaterialNodeType id: %s", id.GetText());
        }
        return false;
    }

    *out = it->second;
    return true;
}

bool ToRpr(TfToken const& id, rpr::MaterialNodeInput* out, bool pedantic) {
    static const std::map<TfToken, rpr::MaterialNodeInput> s_mapping = {
        {RprUsdMaterialNodeInputTokens->color, RPR_MATERIAL_INPUT_COLOR},
        {RprUsdMaterialNodeInputTokens->color0, RPR_MATERIAL_INPUT_COLOR0},
        {RprUsdMaterialNodeInputTokens->color1, RPR_MATERIAL_INPUT_COLOR1},
        {RprUsdMaterialNodeInputTokens->normal, RPR_MATERIAL_INPUT_NORMAL},
        {RprUsdMaterialNodeInputTokens->uv, RPR_MATERIAL_INPUT_UV},
        {RprUsdMaterialNodeInputTokens->data, RPR_MATERIAL_INPUT_DATA},
        {RprUsdMaterialNodeInputTokens->roughness, RPR_MATERIAL_INPUT_ROUGHNESS},
        {RprUsdMaterialNodeInputTokens->ior, RPR_MATERIAL_INPUT_IOR},
        {RprUsdMaterialNodeInputTokens->roughness_x, RPR_MATERIAL_INPUT_ROUGHNESS_X},
        {RprUsdMaterialNodeInputTokens->roughness_y, RPR_MATERIAL_INPUT_ROUGHNESS_Y},
        {RprUsdMaterialNodeInputTokens->rotation, RPR_MATERIAL_INPUT_ROTATION},
        {RprUsdMaterialNodeInputTokens->weight, RPR_MATERIAL_INPUT_WEIGHT},
        {RprUsdMaterialNodeInputTokens->op, RPR_MATERIAL_INPUT_OP},
        {RprUsdMaterialNodeInputTokens->invec, RPR_MATERIAL_INPUT_INVEC},
        {RprUsdMaterialNodeInputTokens->uv_scale, RPR_MATERIAL_INPUT_UV_SCALE},
        {RprUsdMaterialNodeInputTokens->value, RPR_MATERIAL_INPUT_VALUE},
        {RprUsdMaterialNodeInputTokens->reflectance, RPR_MATERIAL_INPUT_REFLECTANCE},
        {RprUsdMaterialNodeInputTokens->scale, RPR_MATERIAL_INPUT_SCALE},
        {RprUsdMaterialNodeInputTokens->scattering, RPR_MATERIAL_INPUT_SCATTERING},
        {RprUsdMaterialNodeInputTokens->absorption, RPR_MATERIAL_INPUT_ABSORBTION},
        {RprUsdMaterialNodeInputTokens->emission, RPR_MATERIAL_INPUT_EMISSION},
        {RprUsdMaterialNodeInputTokens->g, RPR_MATERIAL_INPUT_G},
        {RprUsdMaterialNodeInputTokens->multiscatter, RPR_MATERIAL_INPUT_MULTISCATTER},
        {RprUsdMaterialNodeInputTokens->color2, RPR_MATERIAL_INPUT_COLOR2},
        {RprUsdMaterialNodeInputTokens->color3, RPR_MATERIAL_INPUT_COLOR3},
        {RprUsdMaterialNodeInputTokens->anisotropic, RPR_MATERIAL_INPUT_ANISOTROPIC},
        {RprUsdMaterialNodeInputTokens->frontface, RPR_MATERIAL_INPUT_FRONTFACE},
        {RprUsdMaterialNodeInputTokens->backface, RPR_MATERIAL_INPUT_BACKFACE},
        {RprUsdMaterialNodeInputTokens->origin, RPR_MATERIAL_INPUT_ORIGIN},
        {RprUsdMaterialNodeInputTokens->zaxis, RPR_MATERIAL_INPUT_ZAXIS},
        {RprUsdMaterialNodeInputTokens->xaxis, RPR_MATERIAL_INPUT_XAXIS},
        {RprUsdMaterialNodeInputTokens->threshold, RPR_MATERIAL_INPUT_THRESHOLD},
        {RprUsdMaterialNodeInputTokens->offset, RPR_MATERIAL_INPUT_OFFSET},
        {RprUsdMaterialNodeInputTokens->uv_type, RPR_MATERIAL_INPUT_UV_TYPE},
        {RprUsdMaterialNodeInputTokens->radius, RPR_MATERIAL_INPUT_RADIUS},
        {RprUsdMaterialNodeInputTokens->side, RPR_MATERIAL_INPUT_SIDE},
        {RprUsdMaterialNodeInputTokens->caustics, RPR_MATERIAL_INPUT_CAUSTICS},
        {RprUsdMaterialNodeInputTokens->transmission_color, RPR_MATERIAL_INPUT_TRANSMISSION_COLOR},
        {RprUsdMaterialNodeInputTokens->thickness, RPR_MATERIAL_INPUT_THICKNESS},
        {RprUsdMaterialNodeInputTokens->input0, RPR_MATERIAL_INPUT_0},
        {RprUsdMaterialNodeInputTokens->input1, RPR_MATERIAL_INPUT_1},
        {RprUsdMaterialNodeInputTokens->input2, RPR_MATERIAL_INPUT_2},
        {RprUsdMaterialNodeInputTokens->input3, RPR_MATERIAL_INPUT_3},
        {RprUsdMaterialNodeInputTokens->input4, RPR_MATERIAL_INPUT_4},
        {RprUsdMaterialNodeInputTokens->schlick_approximation, RPR_MATERIAL_INPUT_SCHLICK_APPROXIMATION},
        {RprUsdMaterialNodeInputTokens->applysurface, RPR_MATERIAL_INPUT_APPLYSURFACE},
        {RprUsdMaterialNodeInputTokens->uber_diffuse_color, RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR},
        {RprUsdMaterialNodeInputTokens->uber_diffuse_weight, RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT},
        {RprUsdMaterialNodeInputTokens->uber_diffuse_roughness, RPR_MATERIAL_INPUT_UBER_DIFFUSE_ROUGHNESS},
        {RprUsdMaterialNodeInputTokens->uber_diffuse_normal, RPR_MATERIAL_INPUT_UBER_DIFFUSE_NORMAL},
        {RprUsdMaterialNodeInputTokens->uber_reflection_color, RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR},
        {RprUsdMaterialNodeInputTokens->uber_reflection_weight, RPR_MATERIAL_INPUT_UBER_REFLECTION_WEIGHT},
        {RprUsdMaterialNodeInputTokens->uber_reflection_roughness, RPR_MATERIAL_INPUT_UBER_REFLECTION_ROUGHNESS},
        {RprUsdMaterialNodeInputTokens->uber_reflection_anisotropy, RPR_MATERIAL_INPUT_UBER_REFLECTION_ANISOTROPY},
        {RprUsdMaterialNodeInputTokens->uber_reflection_anisotropy_rotation, RPR_MATERIAL_INPUT_UBER_REFLECTION_ANISOTROPY_ROTATION},
        {RprUsdMaterialNodeInputTokens->uber_reflection_mode, RPR_MATERIAL_INPUT_UBER_REFLECTION_MODE},
        {RprUsdMaterialNodeInputTokens->uber_reflection_ior, RPR_MATERIAL_INPUT_UBER_REFLECTION_IOR},
        {RprUsdMaterialNodeInputTokens->uber_reflection_metalness, RPR_MATERIAL_INPUT_UBER_REFLECTION_METALNESS},
        {RprUsdMaterialNodeInputTokens->uber_reflection_normal, RPR_MATERIAL_INPUT_UBER_REFLECTION_NORMAL},
        {RprUsdMaterialNodeInputTokens->uber_refraction_color, RPR_MATERIAL_INPUT_UBER_REFRACTION_COLOR},
        {RprUsdMaterialNodeInputTokens->uber_refraction_weight, RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT},
        {RprUsdMaterialNodeInputTokens->uber_refraction_roughness, RPR_MATERIAL_INPUT_UBER_REFRACTION_ROUGHNESS},
        {RprUsdMaterialNodeInputTokens->uber_refraction_ior, RPR_MATERIAL_INPUT_UBER_REFRACTION_IOR},
        {RprUsdMaterialNodeInputTokens->uber_refraction_normal, RPR_MATERIAL_INPUT_UBER_REFRACTION_NORMAL},
        {RprUsdMaterialNodeInputTokens->uber_refraction_thin_surface, RPR_MATERIAL_INPUT_UBER_REFRACTION_THIN_SURFACE},
        {RprUsdMaterialNodeInputTokens->uber_refraction_absorption_color, RPR_MATERIAL_INPUT_UBER_REFRACTION_ABSORPTION_COLOR},
        {RprUsdMaterialNodeInputTokens->uber_refraction_absorption_distance, RPR_MATERIAL_INPUT_UBER_REFRACTION_ABSORPTION_DISTANCE},
        {RprUsdMaterialNodeInputTokens->uber_refraction_caustics, RPR_MATERIAL_INPUT_UBER_REFRACTION_CAUSTICS},
        {RprUsdMaterialNodeInputTokens->uber_coating_color, RPR_MATERIAL_INPUT_UBER_COATING_COLOR},
        {RprUsdMaterialNodeInputTokens->uber_coating_weight, RPR_MATERIAL_INPUT_UBER_COATING_WEIGHT},
        {RprUsdMaterialNodeInputTokens->uber_coating_roughness, RPR_MATERIAL_INPUT_UBER_COATING_ROUGHNESS},
        {RprUsdMaterialNodeInputTokens->uber_coating_mode, RPR_MATERIAL_INPUT_UBER_COATING_MODE},
        {RprUsdMaterialNodeInputTokens->uber_coating_ior, RPR_MATERIAL_INPUT_UBER_COATING_IOR},
        {RprUsdMaterialNodeInputTokens->uber_coating_metalness, RPR_MATERIAL_INPUT_UBER_COATING_METALNESS},
        {RprUsdMaterialNodeInputTokens->uber_coating_normal, RPR_MATERIAL_INPUT_UBER_COATING_NORMAL},
        {RprUsdMaterialNodeInputTokens->uber_coating_transmission_color, RPR_MATERIAL_INPUT_UBER_COATING_TRANSMISSION_COLOR},
        {RprUsdMaterialNodeInputTokens->uber_coating_thickness, RPR_MATERIAL_INPUT_UBER_COATING_THICKNESS},
        {RprUsdMaterialNodeInputTokens->uber_sheen, RPR_MATERIAL_INPUT_UBER_SHEEN},
        {RprUsdMaterialNodeInputTokens->uber_sheen_tint, RPR_MATERIAL_INPUT_UBER_SHEEN_TINT},
        {RprUsdMaterialNodeInputTokens->uber_sheen_weight, RPR_MATERIAL_INPUT_UBER_SHEEN_WEIGHT},
        {RprUsdMaterialNodeInputTokens->uber_emission_color, RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR},
        {RprUsdMaterialNodeInputTokens->uber_emission_weight, RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT},
        {RprUsdMaterialNodeInputTokens->uber_emission_mode, RPR_MATERIAL_INPUT_UBER_EMISSION_MODE},
        {RprUsdMaterialNodeInputTokens->uber_transparency, RPR_MATERIAL_INPUT_UBER_TRANSPARENCY},
        {RprUsdMaterialNodeInputTokens->uber_sss_scatter_color, RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_COLOR},
        {RprUsdMaterialNodeInputTokens->uber_sss_scatter_distance, RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_DISTANCE},
        {RprUsdMaterialNodeInputTokens->uber_sss_scatter_direction, RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_DIRECTION},
        {RprUsdMaterialNodeInputTokens->uber_sss_weight, RPR_MATERIAL_INPUT_UBER_SSS_WEIGHT},
        {RprUsdMaterialNodeInputTokens->uber_sss_multiscatter, RPR_MATERIAL_INPUT_UBER_SSS_MULTISCATTER},
        {RprUsdMaterialNodeInputTokens->uber_backscatter_weight, RPR_MATERIAL_INPUT_UBER_BACKSCATTER_WEIGHT},
        {RprUsdMaterialNodeInputTokens->uber_backscatter_color, RPR_MATERIAL_INPUT_UBER_BACKSCATTER_COLOR},
        {RprUsdMaterialNodeInputTokens->uber_fresnel_schlick_approximation, RPR_MATERIAL_INPUT_UBER_FRESNEL_SCHLICK_APPROXIMATION},
    };

    auto it = s_mapping.find(id);
    if (it == s_mapping.end()) {
        if (pedantic) {
            TF_CODING_ERROR("Invalid rpr::MaterialNodeInput id: %s", id.GetText());
        }
        return false;
    }

    *out = it->second;
    return true;
}

bool ToRpr(TfToken const& id, uint32_t* out, bool pedantic) {
    static const std::map<TfToken, uint32_t> s_mapping = {
        {RprUsdMaterialInputModeTokens->pbr, RPR_UBER_MATERIAL_IOR_MODE_PBR},
        {RprUsdMaterialInputModeTokens->metalness, RPR_UBER_MATERIAL_IOR_MODE_METALNESS},
        {RprUsdMaterialInputModeTokens->singlesided, RPR_UBER_MATERIAL_EMISSION_MODE_SINGLESIDED},
        {RprUsdMaterialInputModeTokens->doublesided, RPR_UBER_MATERIAL_EMISSION_MODE_DOUBLESIDED},
        {RprUsdMaterialInputModeTokens->uv, RPR_MATERIAL_NODE_LOOKUP_UV},
        {RprUsdMaterialInputModeTokens->normal, RPR_MATERIAL_NODE_LOOKUP_N},
        {RprUsdMaterialInputModeTokens->position, RPR_MATERIAL_NODE_LOOKUP_P},
        {RprUsdMaterialInputModeTokens->invec, RPR_MATERIAL_NODE_LOOKUP_INVEC},
        {RprUsdMaterialInputModeTokens->outvec, RPR_MATERIAL_NODE_LOOKUP_OUTVEC},
        {RprUsdMaterialInputModeTokens->localPosition, RPR_MATERIAL_NODE_LOOKUP_P_LOCAL},
        {RprUsdMaterialInputModeTokens->shapeRandomColor, RPR_MATERIAL_NODE_LOOKUP_SHAPE_RANDOM_COLOR},
        {RprUsdMaterialInputModeTokens->objectId, RPR_MATERIAL_NODE_LOOKUP_OBJECT_ID},
        {RprUsdMaterialInputModeTokens->planar, RPR_MATERIAL_NODE_UVTYPE_PLANAR},
        {RprUsdMaterialInputModeTokens->cylindical, RPR_MATERIAL_NODE_UVTYPE_CYLINDICAL},
        {RprUsdMaterialInputModeTokens->spherical, RPR_MATERIAL_NODE_UVTYPE_SPHERICAL},
        {RprUsdMaterialInputModeTokens->project, RPR_MATERIAL_NODE_UVTYPE_PROJECT},
    };

    auto it = s_mapping.find(id);
    if (it == s_mapping.end()) {
        if (pedantic) {
            TF_CODING_ERROR("Invalid rpr mode id: %s", id.GetText());
        }
        return false;
    }

    *out = it->second;
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
