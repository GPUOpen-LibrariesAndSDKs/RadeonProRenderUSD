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

#ifndef HDRPR_PRIMVAR_UTIL_H
#define HDRPR_PRIMVAR_UTIL_H

#include "pxr/usd/sdf/path.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

void HdRprFillPrimvarDescsPerInterpolation(
    HdSceneDelegate* sceneDelegate, SdfPath const& id,
    std::map<HdInterpolation, HdPrimvarDescriptorVector>* primvarDescsPerInterpolation);

bool HdRprIsPrimvarExists(
    TfToken const& primvarName,
    std::map<HdInterpolation, HdPrimvarDescriptorVector> const& primvarDescsPerInterpolation,
    HdInterpolation* interpolation = nullptr);

bool HdRprIsValidPrimvarSize(
    size_t primvarSize,
    HdInterpolation primvarInterpolation,
    size_t uniformInterpolationSize,
    size_t vertexInterpolationSize);

struct HdRprGeometrySettings {
    int id = -1;
    int subdivisionLevel = 0;
    uint32_t visibilityMask = 0;
    bool ignoreContour = false;
};

void HdRprParseGeometrySettings(
    HdSceneDelegate* sceneDelegate, SdfPath const& id,
    HdPrimvarDescriptorVector const& constantPrimvarDescs,
    HdRprGeometrySettings* geomSettings);

inline void HdRprParseGeometrySettings(
    HdSceneDelegate* sceneDelegate, SdfPath const& id,
    std::map<HdInterpolation, HdPrimvarDescriptorVector> const& primvarDescsPerInterpolation,
    HdRprGeometrySettings* geomSettings) {
    auto constantPrimvarDescIt = primvarDescsPerInterpolation.find(HdInterpolationConstant);
    if (constantPrimvarDescIt == primvarDescsPerInterpolation.end()) {
        return;
    }

    HdRprParseGeometrySettings(sceneDelegate, id, constantPrimvarDescIt->second, geomSettings);
}

template <typename T>
bool HdRprGetConstantPrimvar(TfToken const& name, HdSceneDelegate* sceneDelegate, SdfPath const& id, T* out_value) {
    auto value = sceneDelegate->Get(id, name);
    if (value.IsHolding<T>()) {
        *out_value = value.UncheckedGet<T>();
        return true;
    }

    auto typeName = value.GetTypeName();
    TF_WARN("[%s] %s: unexpected type. Expected %s but actual type is %s", id.GetText(), name.GetText(), typeid(T).name(), typeName.c_str());
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_PRIMVAR_UTIL_H
