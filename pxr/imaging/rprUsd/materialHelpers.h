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

#ifndef RPRUSD_MATERIAL_HELPERS_H
#define RPRUSD_MATERIAL_HELPERS_H

#include "pxr/base/vt/value.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/imaging/rprUsd/error.h"

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

using RprMaterialNodePtr = std::shared_ptr<rpr::MaterialNode>;

inline rpr::Status SetRprInput(rpr::MaterialNode* node, rpr::MaterialNodeInput input, VtValue const& value) {
    rpr::Status status;
    if (value.IsHolding<uint32_t>()) {
        status = node->SetInput(input, value.UncheckedGet<uint32_t>());
    } else if (value.IsHolding<int>()) {
        status = node->SetInput(input, rpr_uint(value.UncheckedGet<int>()));
    } else if (value.IsHolding<bool>()) {
        rpr_uint v = value.UncheckedGet<bool>() ? 1 : 0;
        status = node->SetInput(input, v);
    } else if (value.IsHolding<float>()) {
        auto v = value.UncheckedGet<float>();
        status = node->SetInput(input, v, v, v, v);
    } else if (value.IsHolding<GfVec3f>()) {
        auto& v = value.UncheckedGet<GfVec3f>();
        status = node->SetInput(input, v[0], v[1], v[2], 1.0f);
    } else if (value.IsHolding<GfVec2f>()) {
        auto& v = value.UncheckedGet<GfVec2f>();
        status = node->SetInput(input, v[0], v[1], 1.0f, 1.0f);
    } else if (value.IsHolding<GfVec4f>()) {
        auto& v = value.UncheckedGet<GfVec4f>();
        status = node->SetInput(input, v[0], v[1], v[2], v[3]);
    } else if (value.IsHolding<RprMaterialNodePtr>()) {
        status = node->SetInput(input, value.UncheckedGet<RprMaterialNodePtr>().get());
    } else {
        TF_RUNTIME_ERROR("Failed to set material input %d: unsupported VtValue type - %s", input, value.GetTypeName().c_str());
        return RPR_ERROR_INVALID_PARAMETER_TYPE;
    }

    if (status != RPR_SUCCESS) {
        auto errMsg = TfStringPrintf("Failed to set material input %d(%s)", input, value.GetTypeName().c_str());
        RPR_ERROR_CHECK(status, errMsg.c_str());
    }

    return status;
}

inline GfVec4f GetRprFloat(VtValue const& value) {
    if (value.IsHolding<int>()) {
        return GfVec4f(value.Get<int>());
    } else if (value.IsHolding<GfVec3f>()) {
        GfVec3f v = value.Get<GfVec3f>();
        return GfVec4f(v[0], v[1], v[2], 1.0f);
    } if (value.IsHolding<float>()) {
        return GfVec4f(value.Get<float>());
    } else {
        return value.Get<GfVec4f>();
    }
}

inline bool GfIsEqual(GfVec4f const& v1, GfVec4f const& v2, float tolerance = 1e-5f) {
    return std::abs(v1[0] - v2[0]) <= tolerance &&
        std::abs(v1[1] - v2[1]) <= tolerance &&
        std::abs(v1[2] - v2[2]) <= tolerance &&
        std::abs(v1[3] - v2[3]) <= tolerance;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_HELPERS_H
