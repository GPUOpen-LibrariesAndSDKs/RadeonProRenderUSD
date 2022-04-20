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

#ifndef RPRUSD_MATERIAL_NODES_RPR_NODE_INFO_H
#define RPRUSD_MATERIAL_NODES_RPR_NODE_INFO_H

#include "pxr/imaging/rprUsd/materialRegistry.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"

namespace std {

PXR_NAMESPACE_USING_DIRECTIVE

inline std::string to_string(GfVec2f const& v) {
    return TfStringPrintf("%f,%f", v[0], v[1]);
}

inline std::string to_string(GfVec3f const& v) {
    return TfStringPrintf("%f,%f,%f", v[0], v[1], v[2]);
}

inline std::string to_string(TfToken const& v) {
    return v.GetString();
}

} // namespace std

PXR_NAMESPACE_OPEN_SCOPE

template <typename T>
struct ToRprUsdMaterialNodeInputType;

#define DEFINE_TYPE_CONVERSION(c_type, material_type) \
    template <> \
    struct ToRprUsdMaterialNodeInputType<c_type> { \
        static constexpr RprUsdMaterialNodeInput::Type value = RprUsdMaterialNodeInput::k##material_type; \
    };

DEFINE_TYPE_CONVERSION(bool, Boolean);
DEFINE_TYPE_CONVERSION(int, Integer);
DEFINE_TYPE_CONVERSION(float, Float);
DEFINE_TYPE_CONVERSION(GfVec2f, Vector2);
DEFINE_TYPE_CONVERSION(GfVec3f, Color3);
DEFINE_TYPE_CONVERSION(TfToken, Token);

struct RprUsd_RprNodeInput : public RprUsdMaterialNodeInput {
    RprUsd_RprNodeInput(RprUsdMaterialNodeInput::Type type) : RprUsdMaterialNodeInput(type) {}
    const char* GetName() const override { return GetCStr(name); }
    const char* GetUIName() const override { return GetCStr(uiName); }
    const char* GetUIMin() const override { return GetCStr(uiMin); }
    const char* GetUISoftMin() const override { return GetCStr(uiSoftMin); }
    const char* GetUIMax() const override { return GetCStr(uiMax); }
    const char* GetUISoftMax() const override { return GetCStr(uiSoftMax); }
    const char* GetUIFolder() const override { return GetCStr(uiFolder); }
    const char* GetDocString() const override { return GetCStr(docString); }
    const char* GetValueString() const override { return GetCStr(valueString); }
    std::vector<TfToken> const& GetTokenValues() const override { return tokenValues; }

    template <typename T>
    RprUsd_RprNodeInput(TfToken const& name, T defaultValue, RprUsdMaterialNodeInput::Type type = RprUsdMaterialNodeInput::kInvalid, const char* uiName = nullptr)
        : RprUsdMaterialNodeInput(type != RprUsdMaterialNodeInput::kInvalid ? type : ToRprUsdMaterialNodeInputType<T>::value)
        , name(name)
        , uiSoftMin("0")
        , uiSoftMax("1")
        , value(VtValue(defaultValue))
        , valueString(std::to_string(defaultValue)) {
        if (!uiName) {
            this->uiName = name.GetString();
            this->uiName[0] = ::toupper(this->uiName[0]);
        } else {
            this->uiName = uiName;
        }
        
    }

    TfToken name;
    std::string uiName;
    std::string uiMin;
    std::string uiSoftMin;
    std::string uiMax;
    std::string uiSoftMax;
    std::string uiFolder;
    std::string docString;
    VtValue value;
    std::string valueString;
    std::vector<TfToken> tokenValues;
};

struct RprUsd_RprNodeOutput : public RprUsdMaterialNodeElement {
    RprUsd_RprNodeOutput(RprUsdMaterialNodeElement::Type type) : RprUsdMaterialNodeElement(type) {}
    const char* GetName() const override { return !name.empty() ? name.c_str() : nullptr; };
    const char* GetUIName() const override { return !uiName.empty() ? uiName.c_str() : nullptr; };
    const char* GetDocString() const override { return !docString.empty() ? docString.c_str() : nullptr; };

    std::string name;
    std::string uiName;
    std::string docString;
};

struct RprUsd_RprNodeInfo : public RprUsdMaterialNodeInfo {
    const char* GetName() const override { return GetCStr(name); }
    const char* GetUIName() const override { return GetCStr(uiName); }
    const char* GetUIFolder() const override { return GetCStr(uiFolder); }

    size_t GetNumInputs() const override { return inputs.size(); }
    RprUsdMaterialNodeInput const* GetInput(size_t idx) const override { return &inputs[idx]; }

    size_t GetNumOutputs() const override { return outputs.size(); }
    RprUsdMaterialNodeElement const* GetOutput(size_t idx) const override { return &outputs[idx]; }

    std::string name;
    std::string uiName;
    std::string uiFolder;
    std::vector<RprUsd_RprNodeInput> inputs;
    std::vector<RprUsd_RprNodeOutput> outputs;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_NODES_RPR_NODE_INFO_H
