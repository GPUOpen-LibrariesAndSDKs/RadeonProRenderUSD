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

#include "mtlxNode.h"
#include "rpr/baseNode.h"

#include "pxr/imaging/rprUsd/material.h"
#include "pxr/imaging/rprUsd/materialMappings.h"
#include "pxr/base/gf/vec2f.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

void ParseMtlxBoolValue(std::string const& valueString, VtValue* value) {
    if (valueString == "true") {
        *value = true;
    } else if (valueString == "false") {
        *value = false;
    } else {
        TF_RUNTIME_ERROR("Invalid Mtlx boolean value: %s", valueString.c_str());
    }
}

void ParseMtlxFloatValue(std::string const& valueString, VtValue* value) {
    try {
        *value = std::stof(valueString);
    } catch (std::logic_error& e) {
        TF_RUNTIME_ERROR("Invalid Mtlx float value: %s - %s", valueString.c_str(), e.what());
    }
}

void ParseMtlxIntValue(std::string const& valueString, VtValue* value) {
    try {
        *value = std::stoi(valueString);
    } catch (std::logic_error& e) {
        TF_RUNTIME_ERROR("Invalid Mtlx int value: %s - %s", valueString.c_str(), e.what());
    }
}

template <typename VecType>
void ParseMtlxVecValue(std::string const& valueString, VtValue* value) {
    auto tokens = TfStringTokenize(valueString, ", \t");
    if (tokens.size() != VecType::dimension) {
        TF_RUNTIME_ERROR("Invalid Mtlx value: %s - expected %zu components, got %zu",
            valueString.c_str(), VecType::dimension, tokens.size());
        return;
    }

    try {
        VecType vec;
        for (size_t i = 0; i < tokens.size(); ++i) {
            vec.data()[i] = std::stof(tokens[i]);
        }
        *value = vec;
    } catch (std::logic_error& e) {
        TF_RUNTIME_ERROR("Invalid Mtlx value: %s - %s", valueString.c_str(), e.what());
    }
}

VtValue ParseMtlxValue(RprUsd_MtlxNodeElement const& input) {
    VtValue ret;
    if (auto valueString = input.GetValueString()) {
        if (input.GetType() == RprUsdMaterialNodeElement::kBoolean) {
            ParseMtlxBoolValue(valueString, &ret);
        } else if (input.GetType() == RprUsdMaterialNodeElement::kInteger) {
            ParseMtlxIntValue(valueString, &ret);
        } else if (input.GetType() == RprUsdMaterialNodeElement::kFloat ||
                   input.GetType() == RprUsdMaterialNodeElement::kAngle) {
            ParseMtlxFloatValue(valueString, &ret);
        } else if (input.GetType() == RprUsdMaterialNodeElement::kVector3 ||
                   input.GetType() == RprUsdMaterialNodeElement::kColor3) {
            ParseMtlxVecValue<GfVec3f>(valueString, &ret);
        } else if (input.GetType() == RprUsdMaterialNodeElement::kVector2) {
            ParseMtlxVecValue<GfVec2f>(valueString, &ret);
        }
    }
    return ret;
}

struct TokenParameterMapping {
    rpr::MaterialNodeInput rprInput;
    std::vector<uint32_t> values;
};

bool GetTokenParameterMapping(
    TfToken const& inputId,
    const char* defaultValue,
    TfTokenVector const& tokenValues,
    int* out_defaultIndex,
    TokenParameterMapping* out_mappings) {
    *out_defaultIndex = -1;

    bool isValid = true;

    if (ToRpr(inputId, &out_mappings->rprInput)) {
        out_mappings->values.reserve(tokenValues.size());
        for (size_t i = 0; i < tokenValues.size(); ++i) {
            uint32_t value;
            if (!ToRpr(tokenValues[i], &value)) {
                isValid = false;
                break;
            }

            out_mappings->values.push_back(value);

            if (tokenValues[i] == defaultValue) {
                *out_defaultIndex = i;
            }
        }
    } else {
        isValid = false;
    }

    if (*out_defaultIndex == -1) {
        TF_RUNTIME_ERROR("Invalid .mtlx definition: no default value");
        isValid = false;
    }

    return isValid;
}

VtValue RemapTokenInput(VtValue const& input, TokenParameterMapping const& mapping) {
    if (!input.IsHolding<int>()) {
        return VtValue();
    }

    int idx = input.UncheckedGet<int>();
    if (idx < 0 || size_t(idx) >= mapping.values.size()) {
        return VtValue();
    }

    return VtValue(idx);
}

} // namespace anonymous

//------------------------------------------------------------------------------
// RprUsd_MtlxNodeInfo
//------------------------------------------------------------------------------

namespace {

RprUsdMaterialNodeInput::Type RprUsd_GetMaterialNodeElementType(MaterialX::TypedElementPtr const& element) {
    static std::map<std::string, RprUsdMaterialNodeInput::Type> s_mapping = {
        {"boolean", RprUsdMaterialNodeElement::kBoolean},
        {"color3", RprUsdMaterialNodeElement::kColor3},
        {"float", RprUsdMaterialNodeElement::kFloat},
        {"angle", RprUsdMaterialNodeElement::kAngle},
        {"integer", RprUsdMaterialNodeElement::kInteger},
        {"volumeshader", RprUsdMaterialNodeElement::kVolumeShader},
        {"surfaceshader", RprUsdMaterialNodeElement::kSurfaceShader},
        {"displacementshader", RprUsdMaterialNodeElement::kDisplacementShader},
        {"vector3", RprUsdMaterialNodeElement::kVector3},
        {"vector2", RprUsdMaterialNodeElement::kVector2},
        {"string", RprUsdMaterialNodeElement::kString},
    };

    auto it = s_mapping.find(element->getType());
    if (it == s_mapping.end()) return RprUsdMaterialNodeElement::kInvalid;

    if (it->second == RprUsdMaterialNodeElement::kString) {
        // If enum attribute is specified, we have kToken type
        auto& enumAttr = element->getAttribute(MaterialX::ValueElement::ENUM_ATTRIBUTE);
        if (!enumAttr.empty()) return RprUsdMaterialNodeElement::kToken;
    }

    return it->second;
}

RprUsdMaterialNodeInput::Type RprUsd_GetMaterialNodeElementType(MaterialX::InputPtr const& input) {
    if (input->getDefaultGeomPropString() == "Nworld") {
        return RprUsdMaterialNodeElement::kNormal;
    } else {
        return RprUsd_GetMaterialNodeElementType(MaterialX::TypedElementPtr(input));
    }
}

} // namespace anonymous

RprUsd_MtlxNodeInfo::RprUsd_MtlxNodeInfo(
    MaterialX::DocumentPtr const& mtlxDoc,
    MaterialX::NodeDefPtr const& mtlxNodeDef,
    std::string const& uiFolder)
    : m_uiFolder(uiFolder)
    , m_mtlxDoc(mtlxDoc)
    , m_mtlxNodeDef(mtlxNodeDef) {

    auto mtlxInputs = m_mtlxNodeDef->getInputs();
    m_mtlxInputs.reserve(mtlxInputs.size());
    for (auto& input : mtlxInputs) {
        auto inputType = RprUsd_GetMaterialNodeElementType(input);
        if (inputType != RprUsdMaterialNodeElement::kInvalid) {
            m_mtlxInputs.emplace_back(std::move(input), inputType);
        }
    }

    auto mtlxOutputs = m_mtlxNodeDef->getOutputs();
    m_mtlxOutputs.reserve(mtlxOutputs.size());
    for (auto& output : mtlxOutputs) {
        auto outputType = RprUsd_GetMaterialNodeElementType(output);
        if (outputType != RprUsdMaterialNodeElement::kInvalid) {
            m_mtlxOutputs.emplace_back(std::move(output), outputType);
        }
    }
}

RprUsdMaterialNodeFactoryFnc RprUsd_MtlxNodeInfo::GetFactory() const {
    static const std::string kRprPrefix("rpr_");

    // Check if node definition matches to the one of rpr native nodes
    auto& nodeDefName = m_mtlxNodeDef->getNodeString();
    if (TfStringStartsWith(nodeDefName, kRprPrefix)) {
        TfToken rprNodeId(nodeDefName.substr(kRprPrefix.size()));

        rpr::MaterialNodeType rprNodeType;
        if (ToRpr(rprNodeId, &rprNodeType, false)) {
            std::vector<std::pair<TfToken, VtValue>> rprNodeDefaultParameters;

            // Token parameter has strict list of possible values.
            // Houdini converts such paramaters to int type.
            // We build lookup table for such parameters to workaround this behavior
            std::map<TfToken, TokenParameterMapping> tokenParamMappings;
            for (auto& input : m_mtlxInputs) {
                if (input.GetType() == RprUsdMaterialNodeElement::kToken) {
                    TfToken inputId(input.GetName());

                    int defaultIndex;
                    TokenParameterMapping mapping;
                    if (GetTokenParameterMapping(inputId, input.GetValueString(), input.GetTokenValues(),
                        &defaultIndex, &mapping)) {

                        tokenParamMappings[inputId] = mapping;
                        rprNodeDefaultParameters.emplace_back(inputId, VtValue(defaultIndex));
                    }
                } else {
                    auto value = ParseMtlxValue(input);
                    if (!value.IsEmpty()) {
                        rprNodeDefaultParameters.emplace_back(TfToken(input.GetName()), std::move(value));
                    }
                }
            }

            return [rprNodeType, rprNodeDefaultParameters, tokenParamMappings](
                RprUsd_MaterialBuilderContext* context,
                std::map<TfToken, VtValue> const& parameters) -> RprUsd_MaterialNode* {

                class RprUsd_MtlxNode : public RprUsd_BaseRuntimeNode {
                public:
                    RprUsd_MtlxNode(
                        rpr::MaterialNodeType type,
                        RprUsd_MaterialBuilderContext* ctx,
                        std::map<TfToken, TokenParameterMapping> const& tokenParamMappings)
                        : RprUsd_BaseRuntimeNode(type, ctx)
                        , m_tokenParamMappings(tokenParamMappings) {

                    }

                    bool SetInput(
                        TfToken const& inputId,
                        VtValue const& value) override {
                        auto tokenParamIt = m_tokenParamMappings.find(inputId);
                        if (tokenParamIt != m_tokenParamMappings.end()) {
                            auto& mapping = tokenParamIt->second;

                            auto remappedValue = RemapTokenInput(value, mapping);
                            if (remappedValue.IsEmpty()) {
                                TF_RUNTIME_ERROR("Failed to remap token parameter %s - %s", inputId.GetText(), value.GetTypeName().c_str());
                                return false;
                            }

                            return RprUsd_BaseRuntimeNode::SetInput(mapping.rprInput, remappedValue);
                        }

                        return RprUsd_BaseRuntimeNode::SetInput(inputId, value);
                    }

                private:
                    std::map<TfToken, TokenParameterMapping> const& m_tokenParamMappings;
                };

                auto rprNode = new RprUsd_MtlxNode(rprNodeType, context, tokenParamMappings);

                bool validInput = true;
                for (auto& entry : rprNodeDefaultParameters) {
                    auto it = parameters.find(entry.first);
                    if (it != parameters.end()) {
                        validInput = rprNode->SetInput(entry.first, it->second);
                    } else {
                        validInput = rprNode->SetInput(entry.first, entry.second);
                    }

                    if (!validInput) {
                        break;
                    }
                }

                if (!validInput) {
                    delete rprNode;
                    return nullptr;
                }

                return rprNode;
            };
        }
    }

    TF_WARN("Nodes with custom implementation are not supported (yet)");
    return nullptr;
}

RprUsd_MtlxNodeElement::RprUsd_MtlxNodeElement(
    MaterialX::ValueElementPtr element,
    RprUsdMaterialNodeElement::Type type)
    : RprUsdMaterialNodeInput(type)
    , m_mtlx(std::move(element)) {
    if (m_type == kToken) {
        auto& enumValues = m_mtlx->getAttribute(MaterialX::ValueElement::ENUM_ATTRIBUTE);
        auto values = TfStringTokenize(enumValues, ",");
        for (auto& value : values) {
            m_tokenValues.emplace_back(value, TfToken::Immortal);
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
