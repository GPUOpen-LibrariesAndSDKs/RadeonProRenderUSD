#include "materialAdapter.h"

#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/material.h"

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/usdUtils/pipeline.h"

#include <RadeonProRender.h>
#include <cfloat>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdRprMaterialTokens, HDRPR_MATERIAL_TOKENS);

TF_DEFINE_PRIVATE_TOKENS(HdRprTextureChannelToken,
    (rgba)
    (rgb)
    (r)
    (g)
    (b)
    (a)
);

GfVec4f VtValToVec4f(const VtValue val) {
    if (val.IsHolding<int>()) {
        return GfVec4f(val.Get<int>());
    } else if (val.IsHolding<GfVec3f>()) {
        GfVec3f temp = val.Get<GfVec3f>();
        return GfVec4f(temp[0], temp[1], temp[2], 1.0f);
    } if (val.IsHolding<float>()) {
        return GfVec4f(val.Get<float>());
    } else {
        return val.Get<GfVec4f>();
    }
}

bool IsColorBlack(GfVec4f color) {
    return color[0] <= FLT_EPSILON && color[1] <= FLT_EPSILON && color[2] <= FLT_EPSILON;
}

bool GetNode(const TfToken& type, const HdMaterialNetwork& materialNetwork, HdMaterialNode& out_node) {
    TF_FOR_ALL(it, materialNetwork.nodes) {
        if (it->identifier == type) {
            out_node = *it;
            return true;
        }
    }

    return false;
}

bool GetParam(const TfToken& type, const HdMaterialNode& node, VtValue& out_param) {
    out_param = VtValue();

    auto& params = node.parameters;

    auto finded = params.find(type);

    if (finded == params.end()) {
        return false;
    }

    out_param = finded->second;
    return true;
}

EColorChannel GetChannel(TfToken channel) {
    if (HdRprTextureChannelToken->rgba == channel) {
        return EColorChannel::RGBA;
    } else if (HdRprTextureChannelToken->rgb == channel) {
        return EColorChannel::RGB;
    } else if (HdRprTextureChannelToken->r == channel) {
        return EColorChannel::R;
    } else if (HdRprTextureChannelToken->g == channel) {
        return EColorChannel::G;
    } else if (HdRprTextureChannelToken->b == channel) {
        return EColorChannel::B;
    } else if (HdRprTextureChannelToken->a == channel) {
        return EColorChannel::A;
    }

    return EColorChannel::NONE;
}

EWrapMode GetWrapMode(const TfToken type, const HdMaterialNode& node) {
    VtValue param;
    GetParam(type, node, param);
    if (!param.IsHolding<TfToken>()) {
        return EWrapMode::NONE;
    }

    TfToken WrapModeType = param.Get<TfToken>();
    if (WrapModeType == HdRprMaterialTokens->black) {
        return EWrapMode::BLACK;
    } else if (WrapModeType == HdRprMaterialTokens->clamp) {
        return EWrapMode::CLAMP;
    } else if (WrapModeType == HdRprMaterialTokens->mirror) {
        return EWrapMode::MIRROR;
    } else if (WrapModeType == HdRprMaterialTokens->repeat) {
        return EWrapMode::REPEAT;
    }

    return EWrapMode::NONE;
}

void GetParameters(const  HdMaterialNetwork& materialNetwork, const HdMaterialNode& previewNode, MaterialParams& out_materialParams) {
    out_materialParams.clear();
    out_materialParams.insert(previewNode.parameters.begin(), previewNode.parameters.end());
}

void GetTextures(const  HdMaterialNetwork& materialNetwork, MaterialTextures& out_materialTextures) {
    out_materialTextures.clear();

    auto stToken = UsdUtilsGetPrimaryUVSetName();

    for (auto& textureRel : materialNetwork.relationships) {
        auto nodeIter = std::find_if(materialNetwork.nodes.begin(), materialNetwork.nodes.end(),
            [&textureRel](HdMaterialNode const& node) {
                return node.path == textureRel.inputId;
        });
        if (nodeIter == materialNetwork.nodes.end()) {
            TF_RUNTIME_ERROR("Invalid material network. Relationship %s does not match to any node", textureRel.outputName.GetText());
            continue;
        }
        if (nodeIter->identifier != HdRprMaterialTokens->UsdUVTexture) {
            continue;
        }

        MaterialTexture materialNode;
        VtValue param;

        // Find out which node produces UV for look up
        for (auto& stRel : materialNetwork.relationships) {
            if (stRel.outputName != stToken ||
                stRel.outputId != textureRel.inputId) {
                continue;
            }

            auto stNodeIter = std::find_if(materialNetwork.nodes.begin(), materialNetwork.nodes.end(),
                [&stRel](HdMaterialNode const& node) {
                    return node.path == stRel.inputId;
            });
            if (stNodeIter == materialNetwork.nodes.end()) {
                TF_RUNTIME_ERROR("Invalid material network. Relationship %s does not match to any node", stRel.outputName.GetText());
                continue;
            }
            // Actually some much more complex node graph could exists
            // But we support only direct UsdUvTexture<->UsdTransform2d relationship for now
            if (stNodeIter->identifier == HdRprMaterialTokens->UsdTransform2d) {
                float rotationDegrees = 0.0f;
                GfVec2f scale(1.0f);
                GfVec2f translation(0.0f);

                GetParam(HdRprMaterialTokens->rotation, *stNodeIter, param);
                if (param.IsHolding<float>()) {
                    rotationDegrees = param.UncheckedGet<float>();
                }

                GetParam(HdRprMaterialTokens->scale, *stNodeIter, param);
                if (param.IsHolding<GfVec2f>()) {
                    scale = param.UncheckedGet<GfVec2f>();
                }

                GetParam(HdRprMaterialTokens->translation, *stNodeIter, param);
                if (param.IsHolding<GfVec2f>()) {
                    translation = param.UncheckedGet<GfVec2f>();
                }

                float rotation = GfDegreesToRadians(rotationDegrees);
                float rotCos = std::cos(rotation);
                float rotSin = std::sin(rotation);

                // XXX (Houdini): Proposal of UsdPreviewSurface states that rotation is
                //  "Counter-clockwise rotation in degrees around the origin",
                //  by default, the origin is the zero point on the UV coordinate system
                //  but Houdini's Karma uses origin = (0.5, 0.5). We stick with it right now
                GfVec2f origin(0.5f);

                materialNode.uvTransform = GfMatrix3f(1.0, 0.0, -origin[0],
                                                      0.0, 1.0, -origin[1],
                                                      0.0, 0.0, 1.0);

                materialNode.uvTransform = GfMatrix3f(scale[0], 0.0, 0.0,
                                                      0.0, scale[1], 0.0,
                                                      0.0, 0.0, 1.0) * materialNode.uvTransform;
                materialNode.uvTransform = GfMatrix3f(rotCos, -rotSin, 0.0,
                                                      rotSin, rotCos, 0.0,
                                                      0.0f, 0.0f, 1.0f) * materialNode.uvTransform;
                materialNode.uvTransform[0][2] += translation[0] + origin[0];
                materialNode.uvTransform[1][2] += translation[1] + origin[1];
            }
            break;
        }

        auto& node = *nodeIter;

        GetParam(HdRprMaterialTokens->file, node, param);

        // Get image path
        if (param.IsHolding<SdfAssetPath>()) {
            auto& assetPath = param.UncheckedGet<SdfAssetPath>();
            if (assetPath.GetResolvedPath().empty()) {
                materialNode.path = ArGetResolver().Resolve(assetPath.GetAssetPath());
            } else {
                materialNode.path = assetPath.GetResolvedPath();
            }
        } else {
            continue;
        }

        // Get channel
        // The input name descripe channel(s) required 
        materialNode.channel = GetChannel(textureRel.inputName);

        // Get Wrap Modes
        materialNode.wrapS = GetWrapMode(HdRprMaterialTokens->wrapS, node);
        materialNode.wrapT = GetWrapMode(HdRprMaterialTokens->wrapT, node);

        // Get Scale
        GetParam(HdRprMaterialTokens->scale, node, param);
        if (param.IsHolding<GfVec4f>()) {
            materialNode.scale = param.Get<GfVec4f>();
        }

        // Get Bias
        GetParam(HdRprMaterialTokens->bias, node, param);
        if (param.IsHolding<GfVec4f>()) {
            materialNode.bias = param.Get<GfVec4f>();
        }

        out_materialTextures[textureRel.outputName] = materialNode;
    }
}

MaterialAdapter::MaterialAdapter(EMaterialType type, const MaterialParams& params) : m_type(type) {
    switch (type) {
        case EMaterialType::COLOR: {
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR] = GfVec4f(0.18f);
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT] = GfVec4f(1.0f);
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFLECTION_WEIGHT] = GfVec4f(0.0f);
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT] = GfVec4f(0.0f);

            for (auto& entry : params) {
                if (entry.first == HdRprMaterialTokens->color) {
                    m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR] = VtValToVec4f(entry.second);
                }
            }
            break;
        }
        case EMaterialType::EMISSIVE:
            PopulateEmissive(params);
            break;
        case EMaterialType::TRANSPERENT:
            PopulateTransparent(params);
            break;
        case EMaterialType::USD_PREVIEW_SURFACE:
            PopulateUsdPreviewSurface(params, {});
            break;
        default:
            break;
    }
}

MaterialAdapter::MaterialAdapter(EMaterialType type, const HdMaterialNetwork& materialNetwork) : m_type(type), m_doublesided(true) {
    switch (type) {
        case EMaterialType::USD_PREVIEW_SURFACE: {
            HdMaterialNode previewNode;
            if (!GetNode(HdRprMaterialTokens->UsdPreviewSurface, materialNetwork, previewNode)) {
                break;
            }

            MaterialParams materialParameters;
            GetParameters(materialNetwork, previewNode, materialParameters);

            auto setFallbackValue = [&materialParameters](TfToken const& name, VtValue value) {
                // TODO: change to try_emplace when it will be available
                if (materialParameters.count(name) == 0) {
                    materialParameters.emplace(name, value);
                }
            };
            setFallbackValue(HdRprMaterialTokens->diffuseColor, VtValue(GfVec3f(0.18f)));
            setFallbackValue(HdRprMaterialTokens->emissiveColor, VtValue(GfVec3f(0.0f)));
            setFallbackValue(HdRprMaterialTokens->useSpecularWorkflow, VtValue(0));
            setFallbackValue(HdRprMaterialTokens->specularColor, VtValue(GfVec3f(0.0f)));
            setFallbackValue(HdRprMaterialTokens->metallic, VtValue(0.0f));
            setFallbackValue(HdRprMaterialTokens->roughness, VtValue(0.5f));
            setFallbackValue(HdRprMaterialTokens->clearcoat, VtValue(0.0f));
            setFallbackValue(HdRprMaterialTokens->clearcoatRoughness, VtValue(0.01f));
            setFallbackValue(HdRprMaterialTokens->opacity, VtValue(1.0f));
            setFallbackValue(HdRprMaterialTokens->opacityThreshold, VtValue(0.0f));
            setFallbackValue(HdRprMaterialTokens->ior, VtValue(1.5f));
            setFallbackValue(HdRprMaterialTokens->opacityThreshold, VtValue(0.0f));

            MaterialTextures materialTextures;
            GetTextures(materialNetwork, materialTextures);

            PopulateUsdPreviewSurface(materialParameters, materialTextures);
            break;
        }
        case EMaterialType::HOUDINI_PRINCIPLED_SHADER:
            PopulateHoudiniPrincipledShader(materialNetwork);
            break;
        default:
            break;
    }
}

void MaterialAdapter::PopulateRprColor(const MaterialParams& params) {
    for (MaterialParams::const_iterator param = params.begin(); param != params.end(); ++param) {
        const TfToken& paramName = param->first;
        const VtValue& paramValue = param->second;

        if (paramName == HdRprMaterialTokens->color) {
            m_vec4fRprParams.insert({RPR_MATERIAL_INPUT_COLOR, VtValToVec4f(paramValue)});
        }
    }
}

void MaterialAdapter::PopulateEmissive(const MaterialParams& params) {
    PopulateRprColor(params);
}

void MaterialAdapter::PopulateTransparent(const MaterialParams& params) {
    PopulateRprColor(params);
}

void MaterialAdapter::PopulateUsdPreviewSurface(const MaterialParams& params, const MaterialTextures& textures) {
    // initial params
    m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFLECTION_WEIGHT] = GfVec4f(1.0f);
    m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFRACTION_COLOR] = GfVec4f(1.0f);

    int useSpecular = 0;
    GfVec4f albedoColor = GfVec4f(1.0f);
    MaterialTexture albedoTex;
    bool isAlbedoTexture = false;

    GfVec4f reflectionColor = GfVec4f(1.0f);
    MaterialTexture reflectionTex;
    bool isReflectionTexture = false;

    for (MaterialParams::const_iterator param = params.begin(); param != params.end(); ++param) {
        const TfToken& paramName = param->first;
        const VtValue& paramValue = param->second;

        if (paramName == HdRprMaterialTokens->diffuseColor) {
            albedoColor = VtValToVec4f(paramValue);
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR] = albedoColor;
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFRACTION_COLOR] = albedoColor;
        } else if (paramName == HdRprMaterialTokens->emissiveColor) {
            GfVec4f emmisionColor = VtValToVec4f(paramValue);
            if (!IsColorBlack(emmisionColor)) {
                m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT] = GfVec4f(1.0f);
                m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR] = emmisionColor;
            }
        } else if (paramName == HdRprMaterialTokens->useSpecularWorkflow) {
            useSpecular = paramValue.Get<int>();
        } else if (paramName == HdRprMaterialTokens->specularColor) {
            reflectionColor = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->metallic) {
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFLECTION_METALNESS] = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->roughness) {
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_DIFFUSE_ROUGHNESS] = VtValToVec4f(paramValue);
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFLECTION_ROUGHNESS] = VtValToVec4f(paramValue);
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFRACTION_ROUGHNESS] = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->clearcoat) {
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_COATING_WEIGHT] = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->clearcoatRoughness) {
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_COATING_ROUGHNESS] = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->ior) {
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFRACTION_IOR] = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->opacity) {
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT] = VtValToVec4f(paramValue);
            auto refractionWeight = GfVec4f(1.0f) - m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT];
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT] = refractionWeight;

            if (refractionWeight[0] != 0.0f || refractionWeight[1] != 0.0f || refractionWeight[2] != 0.0f) {
                m_doublesided = false;
            }
        }
    }

    for (auto textureEntry : textures) {
        auto const& paramName = textureEntry.first;
        auto& materialTexture = textureEntry.second;
        if (paramName == HdRprMaterialTokens->diffuseColor) {
            isAlbedoTexture = true;
            albedoTex = materialTexture;
            m_texRpr[RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->emissiveColor) {
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT] = GfVec4f(1.0f);
            m_texRpr[RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->specularColor) {
            isReflectionTexture = true;
            reflectionTex = materialTexture;
        } else if (paramName == HdRprMaterialTokens->metallic) {
            m_texRpr[RPR_MATERIAL_INPUT_UBER_REFLECTION_METALNESS] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->roughness) {
            m_texRpr[RPR_MATERIAL_INPUT_UBER_DIFFUSE_ROUGHNESS] = materialTexture;
            m_texRpr[RPR_MATERIAL_INPUT_UBER_REFLECTION_ROUGHNESS] = materialTexture;
            m_texRpr[RPR_MATERIAL_INPUT_UBER_REFRACTION_ROUGHNESS] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->clearcoat) {
            m_texRpr[RPR_MATERIAL_INPUT_UBER_COATING_WEIGHT] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->clearcoatRoughness) {
            m_texRpr[RPR_MATERIAL_INPUT_UBER_COATING_ROUGHNESS] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->ior) {
            m_texRpr[RPR_MATERIAL_INPUT_UBER_REFRACTION_IOR] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->opacity) {
            m_texRpr[RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT] = materialTexture;

            // refractionWeight == 1 - diffuseWeight
            // UsdUvTexture has scale and bias: color = scale * textureValue + bias
            // We use it to inverse diffuse weight, so refractionWeight = 1 - diffuseWeightColor = 1 - (scale * textureValue + bias) = 
            //  = (1 - bias) + (-1 * scale) * textureValue, where (1 - bias) = newBias, (-1 * scale) = newScale
            materialTexture.bias = GfVec4f(1.0f) - materialTexture.bias;
            materialTexture.scale *= -1.0f;
            m_texRpr[RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT] = materialTexture;

            m_doublesided = false;
        } else if (paramName == HdRprMaterialTokens->normal) {
            NormalMapParam param;
            param.texture = materialTexture;
            m_normalMapParams.emplace_back(std::vector<rpr_material_node_input>{RPR_MATERIAL_INPUT_UBER_DIFFUSE_NORMAL, RPR_MATERIAL_INPUT_UBER_REFLECTION_NORMAL}, param);
        } else if (paramName == HdRprMaterialTokens->displacement) {
            m_displacementTexture = materialTexture;
        }

    }

    if (useSpecular) {
        m_uRprParams[RPR_MATERIAL_INPUT_UBER_REFLECTION_MODE] = RPR_UBER_MATERIAL_IOR_MODE_PBR;

        if (isReflectionTexture) {
            m_texRpr[RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR] = reflectionTex;
        } else {
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR] = reflectionColor;
        }

    } else {
        m_uRprParams[RPR_MATERIAL_INPUT_UBER_REFLECTION_MODE] = RPR_UBER_MATERIAL_IOR_MODE_METALNESS;

        if (isAlbedoTexture) {
            m_texRpr[RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR] = albedoTex;
        } else {
            m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR] = albedoColor;
        }
    }
}

TF_DEFINE_PRIVATE_TOKENS(HoudiniPrincipledShaderTokens,
    (basecolor) \
    (albedomult) \
    (ior) \
    ((roughness, "rough")) \
    ((anisotropy, "aniso")) \
    ((anisotropyDirection, "anisodir")) \
    (metallic) \
    ((reflectivity, "reflect")) \
    ((reflectTint, "reflecttint")) \
    (coat) \
    ((coatRoughness, "coatrough")) \
    (transparency) \
    ((transmissionColor, "transcolor")) \
    ((transmissionDistance, "transdist")) \
    ((subsurface, "sss")) \
    ((subsurfaceDistance, "sssdist")) \
    ((subsurfaceModel, "sssmodel")) \
    ((subsurfaceColor, "ssscolor")) \
    ((subsurfacePhase, "sssphase")) \
    (sheen) \
    ((sheenTint, "sheentint")) \
    ((emissionColor, "emitcolor")) \
    ((emissionIntensity, "emitint")) \
    ((opacity, "opac")) \
    ((opacityColor, "opaccolor")) \
    (baseNormal) \
    ((baseNormalScale, "baseNormal_scale")) \
    (coatNormal) \
    ((coatNormalScale, "coatNormal_scale")) \
    ((baseNormalEnable, "baseBumpAndNormal_enable")) \
    ((baseNormalType, "baseBumpAndNormal_type")) \
    (separateCoatNormals)
);

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
T GetParameter(TfToken const& name, std::map<TfToken, VtValue> const& parameters, T defaultValue) {
    auto parameterIt = parameters.find(name);
    if (parameterIt != parameters.end() &&
        parameterIt->second.IsHolding<T>()) {
        return parameterIt->second.UncheckedGet<T>();
    }

    return defaultValue;
}

void MaterialAdapter::PopulateHoudiniPrincipledShader(HdMaterialNetwork const& materialNetwork) {
    auto& node = materialNetwork.nodes[0];
    auto& params = node.parameters;

    //auto albedomult = GetParameter(HoudiniPrincipledShaderTokens->albedomult, params, 1.0f);

    // Unused properties:
    // albedomult
    // reflectTint
    // reflectivity

    auto opacity = GetParameter(HoudiniPrincipledShaderTokens->opacity, params, 1.0f);
    auto emissionIntensity = GetParameter(HoudiniPrincipledShaderTokens->emissionIntensity, params, 1.0f);
    auto subsurfaceModel = GetParameter(HoudiniPrincipledShaderTokens->subsurfaceModel, params, std::string("full"));
    auto iorMode = RPR_UBER_MATERIAL_IOR_MODE_PBR;

    auto getMaterialTexture = [&](TfToken const& baseParameter) -> MaterialTexture {
        MaterialTexture texture;

        // Each parameter (e.g. basecolor) may have set of properties in the form:
        // paramName_propertyName (e.g. basecolor_texture)
        // but property itself may be missing in input params

        bool useTexture = false;
        for (auto it = params.lower_bound(baseParameter); it != params.end(); ++it) {
            // check that the current parameter is the property of our base parameter
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
                        auto assetPath = it->second.UncheckedGet<SdfAssetPath>();
                        texture.path = assetPath.GetResolvedPath();
                    }
                } else if (std::strcmp(texturePropertyName, "Intensity") == 0) {
                    if (it->second.IsHolding<float>()) {
                        texture.scale = GfVec4f(it->second.UncheckedGet<float>());
                    }
                }
            } else if (std::strncmp(propertyName, "useTexture", sizeof("useTexture") - 1) == 0) {
                auto useTexturePropertyName = propertyName + sizeof("useTexture") - 1;
                if (*useTexturePropertyName == '\0') {
                    if (it->second.IsHolding<int>()) {
                        useTexture = static_cast<bool>(it->second.UncheckedGet<int>());
                        if (!useTexture) {
                            return MaterialTexture{};
                        }
                    }
                } else if (std::strcmp(useTexturePropertyName, "Alpha") == 0) {
                    if (it->second.IsHolding<int>() &&
                        it->second.UncheckedGet<int>()) {
                        // TODO: useTextureAlpha support for basecolor parameter
                    }
                }
            } else if (std::strcmp(propertyName, "monoChannel") == 0) {
                if (it->second.IsHolding<int>()) {
                    int channel = it->second.UncheckedGet<int>();
                    if (channel == 1) {
                        texture.channel = EColorChannel::R;
                    } else if (channel == 2) {
                        texture.channel = EColorChannel::G;
                    } else if (channel == 3) {
                        texture.channel = EColorChannel::B;
                    }
                }
            }
        }

        if (baseParameter == HoudiniPrincipledShaderTokens->baseNormal) {
            // baseNormal's texture enabled by baseBumpAndNormal_enable parameter unlike all other textures by *_useTexture
            useTexture = true;
        }

        if (useTexture) {
            if (baseParameter != HoudiniPrincipledShaderTokens->basecolor &&
                baseParameter != HoudiniPrincipledShaderTokens->transmissionColor &&
                baseParameter != HoudiniPrincipledShaderTokens->subsurfaceColor &&
                texture.channel == EColorChannel::NONE) {
                texture.channel = EColorChannel::LUMINANCE;
            }

            return texture;
        }

        return MaterialTexture();
    };

    auto populateRprParameter = [&](std::vector<rpr_material_node_input> rprInputs, TfToken const& paramName) {
        VtValue value;

        auto parameterIt = params.find(paramName);
        if (parameterIt != params.end()) {
            value = parameterIt->second;
        } else {
            parameterIt = g_houdiniPrincipledShaderParameterDefaultValues.find(paramName);
            if (parameterIt == g_houdiniPrincipledShaderParameterDefaultValues.end()) {
                return;
            }

            value = parameterIt->second;
        }

        auto texture = getMaterialTexture(paramName);
        if (!texture.path.empty()) {
            for (auto rprInput : rprInputs) {
                if (rprInput == RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR) {
                    m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT] = GfVec4f(1.0f);

                    auto emissionTexture = texture;
                    emissionTexture.scale = GfVec4f(emissionIntensity);
                    m_texRpr[rprInput] = emissionTexture;
                } else if (rprInput == RPR_MATERIAL_INPUT_UBER_TRANSPARENCY) {
                    auto transparencyTex = texture;
                    transparencyTex.bias = GfVec4f(1.0f) - transparencyTex.bias;
                    transparencyTex.scale *= -1.0f * opacity;
                    m_texRpr[rprInput] = transparencyTex;
                    m_doublesided = false;
                } else if (rprInput == RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT) {
                    m_texRpr[RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT] = texture;
                    m_uRprParams[RPR_MATERIAL_INPUT_UBER_REFRACTION_CAUSTICS] = 1;

                    auto diffuseWeightTex = texture;
                    // Inverse logic of UsdPreviewSurface opacity texture
                    diffuseWeightTex.bias = GfVec4f(1.0f) - diffuseWeightTex.bias;
                    diffuseWeightTex.scale *= -1.0f;
                    m_texRpr[RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT] = diffuseWeightTex;

                    m_doublesided = false;
                } else {
                    if (rprInput == RPR_MATERIAL_INPUT_UBER_REFLECTION_METALNESS) {
                        iorMode = RPR_UBER_MATERIAL_IOR_MODE_METALNESS;
                    } else if (rprInput == RPR_MATERIAL_INPUT_UBER_COATING_THICKNESS) {
                        m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_COATING_WEIGHT] = GfVec4f(1.0f);
                    } else if (rprInput == RPR_MATERIAL_INPUT_UBER_SSS_WEIGHT) {
                        m_texRpr[RPR_MATERIAL_INPUT_UBER_BACKSCATTER_WEIGHT] = texture;
                    }

                    m_texRpr[rprInput] = texture;
                }
            }

            return;
        }

        auto vec = VtValToVec4f(value);
        for (auto rprInput : rprInputs) {
            if (rprInput == RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR) {
                if (IsColorBlack(vec * emissionIntensity)) {
                    continue;
                }

                m_vec4fRprParams[rprInput] = vec * emissionIntensity;
                m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT] = GfVec4f(1.0f);
            } else if (rprInput == RPR_MATERIAL_INPUT_UBER_TRANSPARENCY) {
                auto transparency = GfVec4f(1.0f) - vec * opacity;
                m_vec4fRprParams[rprInput] = transparency;
            } else if (rprInput == RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT) {
                m_vec4fRprParams[rprInput] = vec;
                m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT] = GfVec4f(1.0f) - vec;

                if (!IsColorBlack(vec)) {
                    m_uRprParams[RPR_MATERIAL_INPUT_UBER_REFRACTION_CAUSTICS] = 1;
                    m_doublesided = false;
                }
            } else {
                if (rprInput == RPR_MATERIAL_INPUT_UBER_REFLECTION_METALNESS &&
                    !IsColorBlack(vec)) {
                    iorMode = RPR_UBER_MATERIAL_IOR_MODE_METALNESS;
                } else if (rprInput == RPR_MATERIAL_INPUT_UBER_COATING_THICKNESS &&
                    !IsColorBlack(vec)) {
                    m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_COATING_WEIGHT] = GfVec4f(1.0f);
                } else if (rprInput == RPR_MATERIAL_INPUT_UBER_SSS_WEIGHT) {
                    m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_BACKSCATTER_WEIGHT] = vec;
                }

                m_vec4fRprParams[rprInput] = vec;
            }
        }
    };


    populateRprParameter({RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR, RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR, RPR_MATERIAL_INPUT_UBER_COATING_COLOR, RPR_MATERIAL_INPUT_UBER_COATING_TRANSMISSION_COLOR, RPR_MATERIAL_INPUT_UBER_SHEEN}, HoudiniPrincipledShaderTokens->basecolor);
    populateRprParameter({RPR_MATERIAL_INPUT_UBER_REFRACTION_IOR, RPR_MATERIAL_INPUT_UBER_COATING_IOR}, HoudiniPrincipledShaderTokens->ior);
    populateRprParameter({RPR_MATERIAL_INPUT_UBER_DIFFUSE_ROUGHNESS, RPR_MATERIAL_INPUT_UBER_REFLECTION_ROUGHNESS, RPR_MATERIAL_INPUT_UBER_REFRACTION_ROUGHNESS}, HoudiniPrincipledShaderTokens->roughness);

    populateRprParameter({RPR_MATERIAL_INPUT_UBER_REFLECTION_ANISOTROPY}, HoudiniPrincipledShaderTokens->anisotropy);
    populateRprParameter({RPR_MATERIAL_INPUT_UBER_REFLECTION_ANISOTROPY_ROTATION}, HoudiniPrincipledShaderTokens->anisotropyDirection);

    populateRprParameter({RPR_MATERIAL_INPUT_UBER_REFLECTION_METALNESS, RPR_MATERIAL_INPUT_UBER_COATING_METALNESS}, HoudiniPrincipledShaderTokens->metallic);

    populateRprParameter({RPR_MATERIAL_INPUT_UBER_COATING_THICKNESS}, HoudiniPrincipledShaderTokens->coat);
    populateRprParameter({RPR_MATERIAL_INPUT_UBER_COATING_ROUGHNESS}, HoudiniPrincipledShaderTokens->coatRoughness);

    populateRprParameter({RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT}, HoudiniPrincipledShaderTokens->transparency);
    populateRprParameter({RPR_MATERIAL_INPUT_UBER_REFRACTION_COLOR, RPR_MATERIAL_INPUT_UBER_REFRACTION_ABSORPTION_COLOR}, HoudiniPrincipledShaderTokens->transmissionColor);
    populateRprParameter({RPR_MATERIAL_INPUT_UBER_REFRACTION_ABSORPTION_DISTANCE}, HoudiniPrincipledShaderTokens->transmissionDistance);

    populateRprParameter({RPR_MATERIAL_INPUT_UBER_SSS_WEIGHT}, HoudiniPrincipledShaderTokens->subsurface);
    populateRprParameter({RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_DISTANCE}, HoudiniPrincipledShaderTokens->subsurfaceDistance);
    populateRprParameter({RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_COLOR, RPR_MATERIAL_INPUT_UBER_BACKSCATTER_COLOR}, HoudiniPrincipledShaderTokens->subsurfaceColor);
    if (subsurfaceModel == "full") {
        m_uRprParams[RPR_MATERIAL_INPUT_UBER_SSS_MULTISCATTER] = 1;
        m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_DIRECTION] = GfVec4f(0.0);
    } else {
        m_uRprParams[RPR_MATERIAL_INPUT_UBER_SSS_MULTISCATTER] = 0;
        populateRprParameter({RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_DIRECTION}, HoudiniPrincipledShaderTokens->subsurfacePhase);
    }

    populateRprParameter({RPR_MATERIAL_INPUT_UBER_SHEEN_WEIGHT}, HoudiniPrincipledShaderTokens->sheen);
    populateRprParameter({RPR_MATERIAL_INPUT_UBER_SHEEN_TINT}, HoudiniPrincipledShaderTokens->sheenTint);

    populateRprParameter({RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR}, HoudiniPrincipledShaderTokens->emissionColor);

    populateRprParameter({RPR_MATERIAL_INPUT_UBER_TRANSPARENCY}, HoudiniPrincipledShaderTokens->opacityColor);

    if (GetParameter(HoudiniPrincipledShaderTokens->baseNormalEnable, params, 0)) {
        NormalMapParam baseNormalMapParam;
        baseNormalMapParam.texture = getMaterialTexture(HoudiniPrincipledShaderTokens->baseNormal);

        std::vector<rpr_material_node_input> rprInputs = {RPR_MATERIAL_INPUT_UBER_DIFFUSE_NORMAL, RPR_MATERIAL_INPUT_UBER_REFLECTION_NORMAL, RPR_MATERIAL_INPUT_UBER_REFRACTION_NORMAL};

        if (GetParameter(HoudiniPrincipledShaderTokens->separateCoatNormals, params, 0)) {
            NormalMapParam coatNormalMapParam;
            coatNormalMapParam.texture = getMaterialTexture(HoudiniPrincipledShaderTokens->coatNormal);
            if (!coatNormalMapParam.texture.path.empty()) {
                coatNormalMapParam.effectScale = GetParameter(HoudiniPrincipledShaderTokens->coatNormalScale, params, 1.0f);
                m_normalMapParams.emplace_back(std::vector<rpr_material_node_input>{RPR_MATERIAL_INPUT_UBER_COATING_NORMAL}, coatNormalMapParam);
            }
        } else {
            rprInputs.push_back(RPR_MATERIAL_INPUT_UBER_COATING_NORMAL);
        }

        if (!baseNormalMapParam.texture.path.empty()) {
            baseNormalMapParam.effectScale = GetParameter(HoudiniPrincipledShaderTokens->baseNormalScale, params, 1.0f);
            m_normalMapParams.emplace_back(rprInputs, baseNormalMapParam);
        }
    }

    m_vec4fRprParams[RPR_MATERIAL_INPUT_UBER_REFLECTION_WEIGHT] = GfVec4f(1.0f);

    m_uRprParams[RPR_MATERIAL_INPUT_UBER_REFLECTION_MODE] = iorMode;
}

PXR_NAMESPACE_CLOSE_SCOPE
