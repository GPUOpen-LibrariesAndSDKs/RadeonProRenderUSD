#include "materialAdapter.h"

#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/material.h"

#include "pxr/base/gf/vec3f.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/ar/resolver.h"

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

    for (auto& relationship : materialNetwork.relationships) {
        auto nodeIter = std::find_if(materialNetwork.nodes.begin(), materialNetwork.nodes.end(),
            [&relationship](HdMaterialNode const& node) {
                return node.path == relationship.inputId;
        });
        if (nodeIter == materialNetwork.nodes.end()) {
            TF_RUNTIME_ERROR("Invalid material network. Relationship %s does not match to any node", relationship.outputName.GetText());
            continue;
        }
        if (nodeIter->identifier != HdRprMaterialTokens->UsdUVTexture) {
            continue;
        }

        auto& node = *nodeIter;

        MaterialTexture materialNode;

        VtValue param;
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
        materialNode.channel = GetChannel(relationship.inputName);

        // Get Wrap Modes
        materialNode.wrapS = GetWrapMode(HdRprMaterialTokens->wrapS, node);
        materialNode.wrapT = GetWrapMode(HdRprMaterialTokens->wrapT, node);

        // Get Scale
        GetParam(HdRprMaterialTokens->scale, node, param);
        if (param.IsHolding<GfVec4f>()) {
            materialNode.isScaleEnabled = true;
            materialNode.scale = param.Get<GfVec4f>();
        }

        // Get Bias
        GetParam(HdRprMaterialTokens->bias, node, param);
        if (param.IsHolding<GfVec4f>()) {
            materialNode.isBiasEnabled = true;
            materialNode.bias = param.Get<GfVec4f>();
        }

        out_materialTextures[relationship.outputName] = materialNode;
    }
}

MaterialAdapter::MaterialAdapter(const EMaterialType type, const MaterialParams& params) : m_type(type) {
    switch (type) {
        case EMaterialType::COLOR: {
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_DIFFUSE_COLOR] = GfVec4f(0.18f);
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_DIFFUSE_WEIGHT] = GfVec4f(1.0f);
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_REFLECTION_WEIGHT] = GfVec4f(0.0f);
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_REFRACTION_WEIGHT] = GfVec4f(0.0f);

            for (auto& entry : params) {
                if (entry.first == HdRprMaterialTokens->color) {
                    m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_DIFFUSE_COLOR] = VtValToVec4f(entry.second);
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

MaterialAdapter::MaterialAdapter(const EMaterialType type, const HdMaterialNetwork& materialNetwork) : m_type(type) {
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
                materialParameters.emplace(name, value);
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
    m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_REFLECTION_WEIGHT] = GfVec4f(1.0f);
    m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_REFRACTION_COLOR] = GfVec4f(1.0f);

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
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_DIFFUSE_COLOR] = albedoColor;
        } else if (paramName == HdRprMaterialTokens->emissiveColor) {
            GfVec4f emmisionColor = VtValToVec4f(paramValue);
            if (!IsColorBlack(emmisionColor)) {
                m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_EMISSION_WEIGHT] = GfVec4f(1.0f);
                m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_EMISSION_COLOR] = emmisionColor;
            }
        } else if (paramName == HdRprMaterialTokens->useSpecularWorkflow) {
            useSpecular = paramValue.Get<int>();
        } else if (paramName == HdRprMaterialTokens->specularColor) {
            reflectionColor = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->metallic) {
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_REFLECTION_METALNESS] = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->roughness) {
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_DIFFUSE_ROUGHNESS] = VtValToVec4f(paramValue);
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_REFLECTION_ROUGHNESS] = VtValToVec4f(paramValue);
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_REFRACTION_ROUGHNESS] = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->clearcoat) {
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_COATING_WEIGHT] = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->clearcoatRoughness) {
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_COATING_ROUGHNESS] = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->ior) {
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_REFRACTION_IOR] = VtValToVec4f(paramValue);
        } else if (paramName == HdRprMaterialTokens->opacity) {
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_DIFFUSE_WEIGHT] = VtValToVec4f(paramValue);
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_REFRACTION_WEIGHT] = GfVec4f(1.0f) - m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_DIFFUSE_WEIGHT];
        }
    }

    for (auto textureEntry : textures) {
        auto const& paramName = textureEntry.first;
        auto& materialTexture = textureEntry.second;
        if (paramName == HdRprMaterialTokens->diffuseColor) {
            isAlbedoTexture = true;
            albedoTex = materialTexture;
            m_texRpr[RPR_UBER_MATERIAL_INPUT_DIFFUSE_COLOR] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->emissiveColor) {
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_EMISSION_WEIGHT] = GfVec4f(1.0f);
            m_texRpr[RPR_UBER_MATERIAL_INPUT_EMISSION_COLOR] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->specularColor) {
            isReflectionTexture = true;
            reflectionTex = materialTexture;
        } else if (paramName == HdRprMaterialTokens->metallic) {
            m_texRpr[RPR_UBER_MATERIAL_INPUT_REFLECTION_METALNESS] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->roughness) {
            m_texRpr[RPR_UBER_MATERIAL_INPUT_DIFFUSE_ROUGHNESS] = materialTexture;
            m_texRpr[RPR_UBER_MATERIAL_INPUT_REFLECTION_ROUGHNESS] = materialTexture;
            m_texRpr[RPR_UBER_MATERIAL_INPUT_REFRACTION_ROUGHNESS] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->clearcoat) {
            m_texRpr[RPR_UBER_MATERIAL_INPUT_COATING_WEIGHT] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->clearcoatRoughness) {
            m_texRpr[RPR_UBER_MATERIAL_INPUT_COATING_ROUGHNESS] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->ior) {
            m_texRpr[RPR_UBER_MATERIAL_INPUT_REFRACTION_IOR] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->opacity) {
            m_texRpr[RPR_UBER_MATERIAL_INPUT_DIFFUSE_WEIGHT] = materialTexture;
            materialTexture.isOneMinusSrcColor = true;
            m_texRpr[RPR_UBER_MATERIAL_INPUT_REFRACTION_WEIGHT] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->normal) {
            m_texRpr[RPR_UBER_MATERIAL_INPUT_DIFFUSE_NORMAL] = materialTexture;
            m_texRpr[RPR_UBER_MATERIAL_INPUT_REFLECTION_NORMAL] = materialTexture;
        } else if (paramName == HdRprMaterialTokens->displacement) {
            m_displacementTexture = materialTexture;
        }

    }

    if (useSpecular) {
        m_uRprParams[RPR_UBER_MATERIAL_INPUT_REFLECTION_MODE] = RPR_UBER_MATERIAL_IOR_MODE_PBR;

        if (isReflectionTexture) {
            m_texRpr[RPR_UBER_MATERIAL_INPUT_REFLECTION_COLOR] = reflectionTex;
        } else {
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_REFLECTION_COLOR] = reflectionColor;
        }

    } else {
        m_uRprParams[RPR_UBER_MATERIAL_INPUT_REFLECTION_MODE] = RPR_UBER_MATERIAL_IOR_MODE_METALNESS;

        if (isAlbedoTexture) {
            m_texRpr[RPR_UBER_MATERIAL_INPUT_REFLECTION_COLOR] = albedoTex;
        } else {
            m_vec4fRprParams[RPR_UBER_MATERIAL_INPUT_REFLECTION_COLOR] = albedoColor;
        }
    }

}

PXR_NAMESPACE_CLOSE_SCOPE
