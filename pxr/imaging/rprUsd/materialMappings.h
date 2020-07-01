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

#ifndef RPRUSD_MATERIAL_MAPPINGS_H
#define RPRUSD_MATERIAL_MAPPINGS_H

#include "pxr/base/tf/staticTokens.h"

#include <RadeonProRender.hpp>

PXR_NAMESPACE_OPEN_SCOPE

/// \p pedantic controls whether function should trigger TF errors
bool ToRpr(TfToken const& id, rpr::MaterialNodeType* out, bool pedantic = true);
bool ToRpr(TfToken const& id, rpr::MaterialNodeInput* out, bool pedantic = true);
bool ToRpr(TfToken const& id, uint32_t* out, bool pedantic = true);

#define RPRUSD_MATERIAL_NODE_INPUT_TOKENS \
    (color) \
    (color0) \
    (color1) \
    (normal) \
    (uv) \
    (data) \
    (roughness) \
    (ior) \
    (roughness_x) \
    (roughness_y) \
    (rotation) \
    (weight) \
    (op) \
    (invec) \
    (uv_scale) \
    (value) \
    (reflectance) \
    (scale) \
    (scattering) \
    (absorption) \
    (emission) \
    (g) \
    (multiscatter) \
    (color2) \
    (color3) \
    (anisotropic) \
    (frontface) \
    (backface) \
    (origin) \
    (zaxis) \
    (xaxis) \
    (threshold) \
    (offset) \
    (uv_type) \
    (radius) \
    (side) \
    (caustics) \
    (transmission_color) \
    (thickness) \
    ((input0, "0")) \
    ((input1, "1")) \
    ((input2, "2")) \
    ((input3, "3")) \
    ((input4, "4")) \
    (schlick_approximation) \
    (applysurface) \
    (uber_diffuse_color) \
    (uber_diffuse_weight) \
    (uber_diffuse_roughness) \
    (uber_diffuse_normal) \
    (uber_reflection_color) \
    (uber_reflection_weight) \
    (uber_reflection_roughness) \
    (uber_reflection_anisotropy) \
    (uber_reflection_anisotropy_rotation) \
    (uber_reflection_mode) \
    (uber_reflection_ior) \
    (uber_reflection_metalness) \
    (uber_reflection_normal) \
    (uber_refraction_color) \
    (uber_refraction_weight) \
    (uber_refraction_roughness) \
    (uber_refraction_ior) \
    (uber_refraction_normal) \
    (uber_refraction_thin_surface) \
    (uber_refraction_absorption_color) \
    (uber_refraction_absorption_distance) \
    (uber_refraction_caustics) \
    (uber_coating_color) \
    (uber_coating_weight) \
    (uber_coating_roughness) \
    (uber_coating_mode) \
    (uber_coating_ior) \
    (uber_coating_metalness) \
    (uber_coating_normal) \
    (uber_coating_transmission_color) \
    (uber_coating_thickness) \
    (uber_sheen) \
    (uber_sheen_tint) \
    (uber_sheen_weight) \
    (uber_emission_color) \
    (uber_emission_weight) \
    (uber_emission_mode) \
    (uber_transparency) \
    (uber_sss_scatter_color) \
    (uber_sss_scatter_distance) \
    (uber_sss_scatter_direction) \
    (uber_sss_weight) \
    (uber_sss_multiscatter) \
    (uber_backscatter_weight) \
    (uber_backscatter_color) \
    (uber_fresnel_schlick_approximation)

#define RPRUSD_MATERIAL_NODE_TYPE_TOKENS \
    (diffuse) \
    (microfacet) \
    (reflection) \
    (refraction) \
    (microfacet_refraction) \
    (transparent) \
    (emissive) \
    (ward) \
    (add) \
    (blend) \
    (arithmetic) \
    (fresnel) \
    (normal_map) \
    (image_texture) \
    (noise2d_texture) \
    (dot_texture) \
    (gradient_texture) \
    (checker_texture) \
    (constant_texture) \
    (input_lookup) \
    (blend_value) \
    (passthrough) \
    (orennayar) \
    (fresnel_schlick) \
    (diffuse_refraction) \
    (bump_map) \
    (volume) \
    (microfacet_anisotropic_reflection) \
    (microfacet_anisotropic_refraction) \
    (twosided) \
    (uv_procedural) \
    (microfacet_beckmann) \
    (phong) \
    (buffer_sampler) \
    (uv_triplanar) \
    (ao_map) \
    (user_texture_0) \
    (user_texture_1) \
    (user_texture_2) \
    (user_texture_3) \
    (uberv2) \
    (transform) \
    (rgb_to_hsv) \
    (hsv_to_rgb)

#define RPRUSD_MATERIAL_INPUT_MODE_TOKENS \
    /* IOR mode */ \
    ((pbr, "PBR")) \
    ((metalness, "Metalness")) \
    /* Emission mode */ \
    ((singlesided, "Singlesided")) \
    ((doublesided, "Doublesided")) \
    /* Lookup value */ \
    ((uv, "UV")) \
    ((normal, "Normal")) \
    ((position, "Position")) \
    ((invec, "Incoming ray direction")) \
    ((outvec, "Outgoing ray direction")) \
    ((localPosition, "Local Position")) \
    ((shapeRandomColor, "Shape Random Color")) \
    ((objectId, "Object Id")) \
    /* UV Type */ \
    ((planar, "Flat Plane")) \
    ((cylindical, "Cylinder (in xy direction)")) \
    ((spherical, "Spherical")) \
    ((project, "Projected from a camera position")) \

TF_DECLARE_PUBLIC_TOKENS(RprUsdMaterialNodeInputTokens, RPRUSD_MATERIAL_NODE_INPUT_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(RprUsdMaterialNodeTypeTokens, RPRUSD_MATERIAL_NODE_TYPE_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(RprUsdMaterialInputModeTokens, RPRUSD_MATERIAL_INPUT_MODE_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_MAPPINGS_H
