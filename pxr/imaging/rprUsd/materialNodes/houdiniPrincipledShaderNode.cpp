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

#include "houdiniPrincipledShaderNode.h"
#include "rpr/arithmeticNode.h"
#include "usdNode.h"

#include "pxr/imaging/rprUsd/materialRegistry.h"
#include "pxr/imaging/rprUsd/materialMappings.h"
#include "pxr/imaging/rprUsd/materialHelpers.h"
#include "pxr/base/tf/staticTokens.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(HoudiniPrincipledShaderTokens,
    (basecolor)
    (albedomult)
    (ior)
    ((roughness, "rough"))
    ((anisotropy, "aniso"))
    ((anisotropyDirection, "anisodir"))
    (metallic)
    ((reflectivity, "reflect"))
    ((reflectTint, "reflecttint"))
    (coat)
    ((coatRoughness, "coatrough"))
    (transparency)
    ((transmissionColor, "transcolor"))
    ((transmissionDistance, "transdist"))
    ((subsurface, "sss"))
    ((subsurfaceDistance, "sssdist"))
    ((subsurfaceModel, "sssmodel"))
    ((subsurfaceColor, "ssscolor"))
    ((subsurfacePhase, "sssphase"))
    (sheen)
    ((sheenTint, "sheentint"))
    ((emissionColor, "emitcolor"))
    ((emissionIntensity, "emitint"))
    ((opacity, "opac"))
    ((opacityColor, "opaccolor"))
    (baseNormal)
    ((baseNormalScale, "baseNormal_scale"))
    (coatNormal)
    ((coatNormalScale, "coatNormal_scale"))
    ((baseNormalEnable, "baseBumpAndNormal_enable"))
    ((baseNormalType, "baseBumpAndNormal_type"))
    (separateCoatNormals)
    ((doubleSided, "frontface"))
    ((displacementEnable, "dispTex_enable"))
    ((displacementTexture, "dispTex_texture"))
    ((displacementOffset, "dispTex_offset"))
    ((displacementScale, "dispTex_scale"))
    ((displacementColorSpace, "dispTex_colorSpace"))
    ((displacementChannel, "dispTex_channel"))
    ((displacementWrap, "dispTex_wrap"))
    ((displacementType, "dispTex_type"))
    ((infoSourceAsset, "info:sourceAsset"))
    ((infoImplementationSource, "info:implementationSource"))
    (sourceAsset)
    (karma)
);

namespace {

std::map<TfToken, VtValue> g_houdiniPrincipledShaderParameterDefaultValues = {
    {HoudiniPrincipledShaderTokens->basecolor, VtValue(0.2f)},
    {HoudiniPrincipledShaderTokens->ior, VtValue(1.5f)},
    {HoudiniPrincipledShaderTokens->roughness, VtValue(0.3f)},
    {HoudiniPrincipledShaderTokens->anisotropy, VtValue(0.0f)},
    {HoudiniPrincipledShaderTokens->anisotropyDirection, VtValue(0.0f)},
    {HoudiniPrincipledShaderTokens->metallic, VtValue(0.0f)},
    {HoudiniPrincipledShaderTokens->reflectivity, VtValue(1.0f)},
    {HoudiniPrincipledShaderTokens->reflectTint, VtValue(0.0f)},
    {HoudiniPrincipledShaderTokens->coat, VtValue(0.0f)},
    {HoudiniPrincipledShaderTokens->coatRoughness, VtValue(0.0f)},
    {HoudiniPrincipledShaderTokens->transparency, VtValue(0.0f)},
    {HoudiniPrincipledShaderTokens->transmissionColor, VtValue(1.0f)},
    {HoudiniPrincipledShaderTokens->transmissionDistance, VtValue(0.1f)},
    {HoudiniPrincipledShaderTokens->subsurface, VtValue(0.0f)},
    {HoudiniPrincipledShaderTokens->subsurfaceDistance, VtValue(0.1f)},
    {HoudiniPrincipledShaderTokens->subsurfaceColor, VtValue(1.0f)},
    {HoudiniPrincipledShaderTokens->sheen, VtValue(0.0f)},
    {HoudiniPrincipledShaderTokens->sheenTint, VtValue(0.0f)},
    {HoudiniPrincipledShaderTokens->emissionColor, VtValue(0.0f)},
    {HoudiniPrincipledShaderTokens->opacityColor, VtValue(1.0f)},
};

template <typename T>
T GetParameter(TfToken const& name, std::map<TfToken, VtValue> const& parameters, T defaultValue = T()) {
    auto parameterIt = parameters.find(name);
    if (parameterIt != parameters.end() &&
        parameterIt->second.IsHolding<T>()) {
        return parameterIt->second.UncheckedGet<T>();
    }

    return defaultValue;
}

TfToken ToUsdUVTextureWrapMode(std::string const& mode) {
    if (mode == "streak") {
        return RprUsd_UsdUVTextureTokens->clamp;
    } else if (mode == "decal") {
        return RprUsd_UsdUVTextureTokens->black;
    } else {
        return RprUsd_UsdUVTextureTokens->repeat;
    }
}

TfToken ToUsdUVTextureOutputId(int channel) {
    if (channel == 1) {
        return RprUsd_UsdUVTextureTokens->r;
    } else if (channel == 2) {
        return RprUsd_UsdUVTextureTokens->g;
    } else if (channel == 3) {
        return RprUsd_UsdUVTextureTokens->b;
    } else {
        return TfToken();
    }
}

} // namespace anonymous

RprUsd_HoudiniPrincipledNode::RprUsd_HoudiniPrincipledNode(
    RprUsd_MaterialBuilderContext* ctx,
    std::map<TfToken, VtValue> const& params,
    std::map<TfToken, VtValue> const* dispParamsPtr)
    : RprUsd_BaseRuntimeNode(RPR_MATERIAL_NODE_UBERV2, ctx) {

    // XXX: unused parameters:
    // reflectTint
    // reflectivity

    bool useBaseColorTextureAlpha = false;

    auto getTextureValue = [&](TfToken const& baseParameter, bool forceLinearSpace = false) -> VtValue {
        // Each parameter (e.g. basecolor) may have set of properties in the form:
        // paramName_propertyName (e.g. basecolor_texture)
        // but parameter itself may be missing in input params

        SdfAssetPath fileAsset;
        std::string wrapMode;
        float scale = 1.0f;
        TfToken textureOutputId;

        bool useTexture = false;
        for (auto it = params.lower_bound(baseParameter); it != params.end(); ++it) {
            // check that this property corresponds to our base parameter
            if (it->first.GetString().compare(0, baseParameter.size(), baseParameter.GetText())) {
                break;
            }

            // skip paramName
            auto propertyName = it->first.GetText() + baseParameter.size();

            if (*propertyName != '_') {
                continue;
            }
            ++propertyName;

            if (std::strncmp(propertyName, "texture", sizeof("texture") - 1) == 0) {
                auto texturePropertyName = propertyName + (sizeof("texture") - 1);
                if (*texturePropertyName == '\0') {
                    if (it->second.IsHolding<SdfAssetPath>()) {
                        auto& assetPath = it->second.UncheckedGet<SdfAssetPath>();

                        std::string const* filepath;
                        if (assetPath.GetResolvedPath().empty()) {
                            filepath = &assetPath.GetAssetPath();
                        } else {
                            filepath = &assetPath.GetResolvedPath();
                        }

                        if (filepath->empty()) {
                            return VtValue();
                        }

                        fileAsset = assetPath;
                    }
                } else if (std::strcmp(texturePropertyName, "Intensity") == 0) {
                    if (it->second.IsHolding<float>()) {
                        scale = it->second.UncheckedGet<float>();
                    }
                } else if (std::strcmp(texturePropertyName, "ColorSpace") == 0) {
                    if (it->second.IsHolding<std::string>()) {
                        forceLinearSpace = it->second.UncheckedGet<std::string>() == "linear";
                    }
                } else if (std::strcmp(texturePropertyName, "Wrap") == 0) {
                    if (it->second.IsHolding<std::string>()) {
                        wrapMode = it->second.UncheckedGet<std::string>();
                    }
                }
            } else if (std::strncmp(propertyName, "useTexture", sizeof("useTexture") - 1) == 0) {
                auto useTexturePropertyName = propertyName + sizeof("useTexture") - 1;
                if (*useTexturePropertyName == '\0') {
                    if (it->second.IsHolding<int>()) {
                        useTexture = static_cast<bool>(it->second.UncheckedGet<int>());
                        if (!useTexture) {
                            return VtValue();
                        }
                    }
                } else if (std::strcmp(useTexturePropertyName, "Alpha") == 0) {
                    if (it->second.IsHolding<int>() &&
                        it->second.UncheckedGet<int>() == 1) {
                        useBaseColorTextureAlpha = true;
                    }
                }
            } else if (std::strcmp(propertyName, "monoChannel") == 0) {
                if (it->second.IsHolding<int>()) {
                    textureOutputId = ToUsdUVTextureOutputId(it->second.UncheckedGet<int>());
                }
            }
        }

        if (baseParameter == HoudiniPrincipledShaderTokens->baseNormal) {
            // baseNormal's texture enabled by baseBumpAndNormal_enable parameter unlike all other textures by *_useTexture
            useTexture = true;
        }

        if (!useTexture) {
            return VtValue();
        }

        RprUsd_MaterialNode** uvTexture = nullptr;

        if (baseParameter == HoudiniPrincipledShaderTokens->basecolor ||
            baseParameter == HoudiniPrincipledShaderTokens->transmissionColor ||
            baseParameter == HoudiniPrincipledShaderTokens->subsurfaceColor ||
            baseParameter == HoudiniPrincipledShaderTokens->baseNormal ||
            baseParameter == HoudiniPrincipledShaderTokens->coatNormal) {
            if (textureOutputId.IsEmpty()) {
                textureOutputId = RprUsd_UsdUVTextureTokens->rgba;
            }
            if (baseParameter == HoudiniPrincipledShaderTokens->basecolor) {
                uvTexture = &m_baseColorNode;
            }
        }

        return GetTextureOutput(fileAsset, wrapMode, scale, 0.0f, forceLinearSpace, textureOutputId, uvTexture);
    };

    struct ParameterValue {
        VtValue value;
        bool isDefault;

        bool IsAuthored() {
            return !isDefault && !value.IsEmpty();
        }
    };
    auto getParameterValue = [&](TfToken const& paramName) -> ParameterValue {
        ParameterValue param;
        param.isDefault = false;

        param.value = getTextureValue(paramName);

        // Check for plain parameter
        if (param.value.IsEmpty()) {
            auto parameterIt = params.find(paramName);
            if (parameterIt != params.end()) {
                param.value = parameterIt->second;
            }
        }

        // Check for default parameter
        if (param.value.IsEmpty()) {
            auto parameterIt = g_houdiniPrincipledShaderParameterDefaultValues.find(paramName);
            if (parameterIt != g_houdiniPrincipledShaderParameterDefaultValues.end()) {
                param.value = parameterIt->second;
                param.isDefault = true;
            }
        }

        return param;
    };

    auto setInputs = [this](VtValue const& value, std::vector<rpr::MaterialNodeInput> const& rprInputs) {
        for (auto rprInput : rprInputs) {
            SetRprInput(m_rprNode.get(), rprInput, value);
        }
    };

    auto populateParameter = [&](TfToken const& paramName, std::vector<rpr::MaterialNodeInput> const& rprInputs) {
        auto param = getParameterValue(paramName);
        if (!param.value.IsEmpty()) {
            setInputs(param.value, rprInputs);
        }
    };

    auto basecolorParam = getParameterValue(HoudiniPrincipledShaderTokens->basecolor);
    if (!basecolorParam.value.IsEmpty()) {
        auto albedoMultiplyNode = AddAuxiliaryNode(RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_MUL, m_ctx));
        albedoMultiplyNode->SetInput(0, VtValue(GetParameter(HoudiniPrincipledShaderTokens->albedomult, params, 1.0f)));
        albedoMultiplyNode->SetInput(1, basecolorParam.value);
        setInputs(albedoMultiplyNode->GetOutput(), {RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR, RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR, RPR_MATERIAL_INPUT_UBER_COATING_COLOR, RPR_MATERIAL_INPUT_UBER_COATING_TRANSMISSION_COLOR, RPR_MATERIAL_INPUT_UBER_SHEEN});
    }

    populateParameter(HoudiniPrincipledShaderTokens->ior, {RPR_MATERIAL_INPUT_UBER_REFRACTION_IOR, RPR_MATERIAL_INPUT_UBER_COATING_IOR});
    populateParameter(HoudiniPrincipledShaderTokens->roughness, {RPR_MATERIAL_INPUT_UBER_DIFFUSE_ROUGHNESS, RPR_MATERIAL_INPUT_UBER_REFLECTION_ROUGHNESS, RPR_MATERIAL_INPUT_UBER_REFRACTION_ROUGHNESS});

    populateParameter(HoudiniPrincipledShaderTokens->anisotropy, {RPR_MATERIAL_INPUT_UBER_REFLECTION_ANISOTROPY});
    populateParameter(HoudiniPrincipledShaderTokens->anisotropyDirection, {RPR_MATERIAL_INPUT_UBER_REFLECTION_ANISOTROPY_ROTATION});

    populateParameter(HoudiniPrincipledShaderTokens->coatRoughness, {RPR_MATERIAL_INPUT_UBER_COATING_ROUGHNESS});
    auto coatParam = getParameterValue(HoudiniPrincipledShaderTokens->coat);
    if (!coatParam.value.IsEmpty()) {
        auto coatingWeightNode = AddAuxiliaryNode(RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_GREATER, m_ctx));
        coatingWeightNode->SetInput(0, coatParam.value);
        coatingWeightNode->SetInput(1, VtValue(0.0f));

        SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_COATING_WEIGHT, coatingWeightNode->GetOutput());
        SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_COATING_THICKNESS, coatParam.value);
    }

    auto subsurfaceParam = getParameterValue(HoudiniPrincipledShaderTokens->subsurface);
    if (!subsurfaceParam.value.IsEmpty()) {
        SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_SSS_WEIGHT, subsurfaceParam.value);
        SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_BACKSCATTER_WEIGHT, subsurfaceParam.value);
    }
    populateParameter(HoudiniPrincipledShaderTokens->subsurfaceDistance, {RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_DISTANCE});
    populateParameter(HoudiniPrincipledShaderTokens->subsurfaceColor, {RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_COLOR, RPR_MATERIAL_INPUT_UBER_BACKSCATTER_COLOR});

    auto subsurfaceModel = GetParameter(HoudiniPrincipledShaderTokens->subsurfaceModel, params, std::string("full"));
    if (subsurfaceModel == "full") {
        RPR_ERROR_CHECK(m_rprNode->SetInput(RPR_MATERIAL_INPUT_UBER_SSS_MULTISCATTER, 1u), "Failed to set sss multiscatter input");
        RPR_ERROR_CHECK(m_rprNode->SetInput(RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_DIRECTION, 0, 0, 0, 0), "Failed to set sss multiscatter input");
    } else {
        RPR_ERROR_CHECK(m_rprNode->SetInput(RPR_MATERIAL_INPUT_UBER_SSS_MULTISCATTER, 0u), "Failed to set sss multiscatter input");
        populateParameter(HoudiniPrincipledShaderTokens->subsurfacePhase, {RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_DIRECTION});
    }

    populateParameter(HoudiniPrincipledShaderTokens->sheen, {RPR_MATERIAL_INPUT_UBER_SHEEN_WEIGHT});
    populateParameter(HoudiniPrincipledShaderTokens->sheenTint, {RPR_MATERIAL_INPUT_UBER_SHEEN_TINT});

    auto emissionColorParam = getParameterValue(HoudiniPrincipledShaderTokens->emissionColor);
    if (!emissionColorParam.value.IsEmpty()) {
        auto emissiveWeightNode = AddAuxiliaryNode(RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_GREATER, m_ctx));
        emissiveWeightNode->SetInput(0, emissionColorParam.value);
        emissiveWeightNode->SetInput(1, VtValue(0.0f));
        SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT, emissiveWeightNode->GetOutput());

        auto emissiveIntensityNode = AddAuxiliaryNode(RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_MUL, m_ctx));
        emissiveIntensityNode->SetInput(0, emissionColorParam.value);
        emissiveIntensityNode->SetInput(1, VtValue(GetParameter(HoudiniPrincipledShaderTokens->emissionIntensity, params, 1.0f)));
        SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR, emissiveIntensityNode->GetOutput());
    }

    m_rprNode->SetInput(RPR_MATERIAL_INPUT_UBER_REFLECTION_WEIGHT, 1, 1, 1, 1);

    if (GetParameter(HoudiniPrincipledShaderTokens->baseNormalEnable, params, 0)) {
        std::vector<rpr::MaterialNodeInput> rprInputs = {RPR_MATERIAL_INPUT_UBER_DIFFUSE_NORMAL, RPR_MATERIAL_INPUT_UBER_REFLECTION_NORMAL, RPR_MATERIAL_INPUT_UBER_REFRACTION_NORMAL};

        if (GetParameter(HoudiniPrincipledShaderTokens->separateCoatNormals, params, 0)) {
            auto coatNormal = getTextureValue(HoudiniPrincipledShaderTokens->coatNormal, true);
            if (!coatNormal.IsEmpty()) {
                auto normalMapNode = AddAuxiliaryNode(std::make_unique<RprUsd_BaseRuntimeNode>(RPR_MATERIAL_NODE_NORMAL_MAP, m_ctx));
                normalMapNode->SetInput(RprUsdMaterialNodeInputTokens->color, coatNormal);
                normalMapNode->SetInput(RprUsdMaterialNodeInputTokens->scale, VtValue(GetParameter(HoudiniPrincipledShaderTokens->coatNormalScale, params, 1.0f)));
                SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_COATING_NORMAL, normalMapNode->GetOutput(TfToken()));
            }
        } else {
            rprInputs.push_back(RPR_MATERIAL_INPUT_UBER_COATING_NORMAL);
        }

        auto baseNormal = getTextureValue(HoudiniPrincipledShaderTokens->baseNormal, true);
        if (!baseNormal.IsEmpty()) {
            auto normalMapNode = AddAuxiliaryNode(std::make_unique<RprUsd_BaseRuntimeNode>(RPR_MATERIAL_NODE_NORMAL_MAP, m_ctx));
            normalMapNode->SetInput(RprUsdMaterialNodeInputTokens->color, baseNormal);
            normalMapNode->SetInput(RprUsdMaterialNodeInputTokens->scale, VtValue(GetParameter(HoudiniPrincipledShaderTokens->baseNormalScale, params, 1.0f)));
            auto normalMap = normalMapNode->GetOutput(TfToken());
            for (auto rprInput : rprInputs) {
                SetRprInput(m_rprNode.get(), rprInput, normalMap);
            }
        }
    }

    bool hasTransparency = false;

    auto transparencyParam = getParameterValue(HoudiniPrincipledShaderTokens->transparency);
    if (!transparencyParam.value.IsEmpty()) {
        if (transparencyParam.IsAuthored()) {
            hasTransparency = true;
        }

        RPR_ERROR_CHECK(m_rprNode->SetInput(RPR_MATERIAL_INPUT_UBER_REFRACTION_CAUSTICS, 1), "Failed to set caustics input");
        SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT, transparencyParam.value);

        auto diffuseWeightNode = AddAuxiliaryNode(RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_SUB, m_ctx));
        diffuseWeightNode->SetInput(0, VtValue(1.0f));
        diffuseWeightNode->SetInput(1, transparencyParam.value);
        SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT, diffuseWeightNode->GetOutput());
    }
    populateParameter(HoudiniPrincipledShaderTokens->transmissionColor, {RPR_MATERIAL_INPUT_UBER_REFRACTION_COLOR, RPR_MATERIAL_INPUT_UBER_REFRACTION_ABSORPTION_COLOR});
    populateParameter(HoudiniPrincipledShaderTokens->transmissionDistance, {RPR_MATERIAL_INPUT_UBER_REFRACTION_ABSORPTION_DISTANCE});

    if (useBaseColorTextureAlpha) {
        VtValue opacity;
        if (m_baseColorNode) {
            opacity = m_baseColorNode->GetOutput(RprUsd_UsdUVTextureTokens->a);
            hasTransparency = true;
        } else {
            auto opacityParam = getParameterValue(HoudiniPrincipledShaderTokens->opacityColor);
            if (opacityParam.IsAuthored()) {
                hasTransparency = true;
            }
            opacity = opacityParam.value;
        }

        if (!opacity.IsEmpty()) {
            auto transparencyNode = AddAuxiliaryNode(RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_MUL, m_ctx));
            transparencyNode->SetInput(0, opacity);
            transparencyNode->SetInput(1, VtValue(GetParameter(HoudiniPrincipledShaderTokens->opacity, params, 1.0f)));
            auto transparency = transparencyNode->GetOutput();

            transparencyNode = AddAuxiliaryNode(RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_SUB, m_ctx));
            transparencyNode->SetInput(0, VtValue(1.0f));
            transparencyNode->SetInput(1, transparency);
            transparency = transparencyNode->GetOutput();

            if (SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_TRANSPARENCY, transparency) == RPR_SUCCESS) {
                hasTransparency = true;
            }
        }
    }

    auto iorMode = RPR_UBER_MATERIAL_IOR_MODE_PBR;
    if (!hasTransparency) {
        iorMode = RPR_UBER_MATERIAL_IOR_MODE_METALNESS;
        populateParameter(HoudiniPrincipledShaderTokens->metallic, {RPR_MATERIAL_INPUT_UBER_REFLECTION_METALNESS, RPR_MATERIAL_INPUT_UBER_COATING_METALNESS});
    }

    RPR_ERROR_CHECK(m_rprNode->SetInput(RPR_MATERIAL_INPUT_UBER_REFLECTION_MODE, iorMode), "Failed to set ior mode input");
    RPR_ERROR_CHECK(m_rprNode->SetInput(RPR_MATERIAL_INPUT_UBER_COATING_MODE, iorMode), "Failed to set ior mode input");

    if (dispParamsPtr) {
        auto& dispParams = *dispParamsPtr;
        if (GetParameter(HoudiniPrincipledShaderTokens->displacementEnable, dispParams, 0)) {
            auto dispType = GetParameter<std::string>(HoudiniPrincipledShaderTokens->displacementType, dispParams);
            if (dispType == "vectordisp") {
                TF_RUNTIME_ERROR("Vector displacement unsupported");
            } else {
                auto dispTexturePath = GetParameter<SdfAssetPath>(HoudiniPrincipledShaderTokens->displacementTexture, dispParams);
                if (!dispTexturePath.GetResolvedPath().empty()) {
                    m_displacementOutput = GetTextureOutput(
                        dispTexturePath,
                        GetParameter<std::string>(HoudiniPrincipledShaderTokens->displacementWrap, dispParams),
                        GetParameter(HoudiniPrincipledShaderTokens->displacementScale, dispParams, 0.05f),
                        GetParameter(HoudiniPrincipledShaderTokens->displacementOffset, dispParams, -0.5f),
                        GetParameter(HoudiniPrincipledShaderTokens->displacementColorSpace, dispParams, std::string("linear")) == "linear",
                        ToUsdUVTextureOutputId(GetParameter(HoudiniPrincipledShaderTokens->displacementChannel, dispParams, 0)));
                }
            }
        }
    }
}

VtValue RprUsd_HoudiniPrincipledNode::GetOutput(TfToken const& outputId) {
    if (outputId == HdMaterialTerminalTokens->displacement) {
        return m_displacementOutput;
    } else {
        return RprUsd_BaseRuntimeNode::GetOutput(outputId);
    }
}

VtValue RprUsd_HoudiniPrincipledNode::GetTextureOutput(
    SdfAssetPath const& path,
    std::string const& wrapMode,
    float scale, float bias,
    bool forceLinearSpace,
    TfToken const& outputId,
    RprUsd_MaterialNode** out_uvTextureNode) {
    if (path.GetResolvedPath().empty() &&
        path.GetAssetPath().empty()) {
        return VtValue();
    }

    std::map<TfToken, VtValue> uvTextureParams = {
        {RprUsd_UsdUVTextureTokens->file, VtValue(path)},
        {RprUsd_UsdUVTextureTokens->wrapS, VtValue(ToUsdUVTextureWrapMode(wrapMode))},
    };

    if (forceLinearSpace) {
        uvTextureParams.emplace(RprUsd_UsdUVTextureTokens->sourceColorSpace, RprUsd_UsdUVTextureTokens->srgblinear);
    }

    if (scale != 1.0f) {
        uvTextureParams.emplace(RprUsd_UsdUVTextureTokens->scale, VtValue(GfVec4f(scale)));
    }

    if (bias != 0.0f) {
        uvTextureParams.emplace(RprUsd_UsdUVTextureTokens->bias, VtValue(GfVec4f(bias)));
    }

    RprUsd_MaterialNode* uvTexture;
    try {
        uvTexture = AddAuxiliaryNode(std::make_unique<RprUsd_UsdUVTexture>(m_ctx, uvTextureParams));
    } catch (RprUsd_NodeError& e) {
        TF_RUNTIME_ERROR("Failed to create texture node: %s", e.what());
        return VtValue();
    }

    if (out_uvTextureNode) {
        *out_uvTextureNode = uvTexture;
    }

    if (outputId.IsEmpty()) {
        // Output luminance
        auto output = uvTexture->GetOutput(RprUsd_UsdUVTextureTokens->rgba);

        auto luminanceNode = AddAuxiliaryNode(RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_DOT3, m_ctx));
        luminanceNode->SetInput(0, output);
        luminanceNode->SetInput(1, VtValue(GfVec4f(0.2126, 0.7152, 0.0722, 0.0)));
        return luminanceNode->GetOutput();
    } else {
        return uvTexture->GetOutput(outputId);
    }
}

bool IsHoudiniPrincipledShaderHydraNode(HdSceneDelegate* delegate, SdfPath const& nodePath, bool* isSurface) {
    // Houdini's principled shader is hidden under `karma` namespace.
    // If HdRprRenderDelegate uses some other network selector that is not
    // a `karma` one then Hydra will never feed us a definition of the principled shader.
    if (RprUsdMaterialRegistry::GetInstance().GetMaterialNetworkSelector() != HoudiniPrincipledShaderTokens->karma) {
        return false;
    }

    auto implementationSource = delegate->Get(nodePath, HoudiniPrincipledShaderTokens->infoImplementationSource);
    if (implementationSource.IsHolding<TfToken>() &&
        implementationSource.UncheckedGet<TfToken>() == HoudiniPrincipledShaderTokens->sourceAsset) {
        auto nodeAsset = delegate->Get(nodePath, HoudiniPrincipledShaderTokens->infoSourceAsset);
        if (nodeAsset.IsHolding<SdfAssetPath>()) {
            auto& asset = nodeAsset.UncheckedGet<SdfAssetPath>();
            if (!asset.GetAssetPath().empty()) {
                std::string principledShaderDef = "opdef:/Vop/principledshader::2.0?";
                if (asset.GetAssetPath().compare(0, principledShaderDef.size(), principledShaderDef.c_str()) == 0) {
                    auto vexCodeType = asset.GetAssetPath().substr(principledShaderDef.size());
                    if (vexCodeType == "SurfaceVexCode") {
                        *isSurface = true;
                    } else if (vexCodeType == "DisplacementVexCode") {
                        *isSurface = false;
                    } else {
                        return false;
                    }
                    return true;
                }
            }
        }
    }

    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
