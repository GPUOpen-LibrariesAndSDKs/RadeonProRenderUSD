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

#define HDRPR_PRIMVAR_TOKENS \
    ((visibilityMask, "rpr:visibilityMask"))

TF_DECLARE_PUBLIC_TOKENS(HdRprPrimvarTokens, HDRPR_PRIMVAR_TOKENS);

uint32_t HdRpr_ParseVisibilityMask(std::string const& visibilityMask);

template <typename T>
bool HdRpr_GetConstantPrimvar(TfToken const& name, HdSceneDelegate* sceneDelegate, SdfPath const& id, T* out_value) {
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