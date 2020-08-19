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

#include "usdNode.h"
#include "rpr/arithmeticNode.h"

#include "pxr/imaging/rprUsd/materialHelpers.h"
#include "pxr/imaging/rprUsd/materialMappings.h"
#include "pxr/imaging/rprUsd/imageCache.h"
#include "pxr/imaging/rprUsd/coreImage.h"
#include "pxr/imaging/rprUsd/error.h"
#include "pxr/base/arch/attributes.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/base/gf/matrix3f.h"
#include "pxr/base/gf/vec2f.h"

PXR_NAMESPACE_OPEN_SCOPE

//------------------------------------------------------------------------------
// RprUsd_UsdPreviewSurface
//------------------------------------------------------------------------------

TF_DEFINE_PRIVATE_TOKENS(UsdPreviewSurfaceTokens,
    (diffuseColor)
    (emissiveColor)
    (useSpecularWorkflow)
    (specularColor)
    (metallic)
    (roughness)
    (clearcoat)
    (clearcoatRoughness)
    (opacity)
    (opacityThreshold)
    (ior)
    (displacement)
    (normal)
);

RprUsd_UsdPreviewSurface::RprUsd_UsdPreviewSurface(
    RprUsd_MaterialBuilderContext* ctx,
    std::map<TfToken, VtValue> const& hydraParameters)
    : RprUsd_BaseRuntimeNode(RPR_MATERIAL_NODE_UBERV2, ctx) {

    m_albedo = VtValue(GfVec4f(1.0f));
    m_reflection = VtValue(GfVec4f(1.0f));

    auto setInput = [&hydraParameters, this](TfToken const& id, VtValue defaultValue) {
        auto it = hydraParameters.find(id);
        if (it == hydraParameters.end()) {
            SetInput(id, defaultValue);
        } else {
            SetInput(id, it->second);
        }
    };
    setInput(UsdPreviewSurfaceTokens->diffuseColor, VtValue(GfVec3f(0.18f)));
    setInput(UsdPreviewSurfaceTokens->emissiveColor, VtValue(GfVec3f(0.0f)));
    setInput(UsdPreviewSurfaceTokens->useSpecularWorkflow, VtValue(0));
    setInput(UsdPreviewSurfaceTokens->specularColor, VtValue(GfVec3f(0.0f)));
    setInput(UsdPreviewSurfaceTokens->metallic, VtValue(0.0f));
    setInput(UsdPreviewSurfaceTokens->roughness, VtValue(0.5f));
    setInput(UsdPreviewSurfaceTokens->clearcoat, VtValue(0.0f));
    setInput(UsdPreviewSurfaceTokens->clearcoatRoughness, VtValue(0.01f));
    setInput(UsdPreviewSurfaceTokens->opacity, VtValue(1.0f));
    setInput(UsdPreviewSurfaceTokens->opacityThreshold, VtValue(0.0f));
    setInput(UsdPreviewSurfaceTokens->ior, VtValue(1.5f));
    setInput(UsdPreviewSurfaceTokens->displacement, VtValue(0.0f));

    m_rprNode->SetInput(RPR_MATERIAL_INPUT_UBER_REFLECTION_WEIGHT, 1.0f, 1.0f, 1.0f, 1.0f);
}

bool RprUsd_UsdPreviewSurface::SetInput(
    TfToken const& inputId,
    VtValue const& value) {
    if (UsdPreviewSurfaceTokens->diffuseColor == inputId) {
        m_albedo = value;
        return SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR, value) &&
               SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_REFRACTION_COLOR, value);
    } else if (UsdPreviewSurfaceTokens->emissiveColor == inputId) {
        if (!m_emissiveWeightNode) {
            m_emissiveWeightNode = RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_GREATER, m_ctx);
        }
        m_emissiveWeightNode->SetInput(0, value);
        m_emissiveWeightNode->SetInput(1, VtValue(GfVec4f(0.0f)));
        auto emissionWeight = m_emissiveWeightNode->GetOutput();

        return SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT, emissionWeight) &&
               SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR, value);
    } else if (UsdPreviewSurfaceTokens->useSpecularWorkflow == inputId) {
        m_useSpecular = value.Get<int>();
    } else if (UsdPreviewSurfaceTokens->specularColor == inputId) {
        m_reflection = value;
    } else if (UsdPreviewSurfaceTokens->metallic == inputId) {
        return SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_REFLECTION_METALNESS, value);
    } else if (UsdPreviewSurfaceTokens->roughness == inputId) {
        return SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_DIFFUSE_ROUGHNESS, value) &&
               SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_REFLECTION_ROUGHNESS, value) &&
               SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_REFRACTION_ROUGHNESS, value);
    } else if (UsdPreviewSurfaceTokens->clearcoat == inputId) {
        return SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_COATING_WEIGHT, value);
    } else if (UsdPreviewSurfaceTokens->clearcoatRoughness == inputId) {
        return SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_COATING_ROUGHNESS, value);
    } else if (UsdPreviewSurfaceTokens->opacity == inputId) {
        if (!m_refractionWeightNode) {
            m_refractionWeightNode = RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_SUB, m_ctx);
        }
        m_refractionWeightNode->SetInput(0, VtValue(GfVec4f(1.0f)));
        m_refractionWeightNode->SetInput(1, value);
        auto refractionWeight = m_refractionWeightNode->GetOutput();

        return SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT, value) &&
               SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT, refractionWeight);
    } else if (UsdPreviewSurfaceTokens->opacityThreshold == inputId) {

    } else if (UsdPreviewSurfaceTokens->ior == inputId) {
        return SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_REFRACTION_IOR, value);
    } else if (UsdPreviewSurfaceTokens->displacement == inputId) {
        if (value.IsHolding<RprMaterialNodePtr>()) {
            m_displacementOutput = value;
        } else {
            auto vec = GetRprFloat(value);
            if (!GfIsEqual(vec, GfVec4f(0.0f))) {
                if (!m_displaceNode) {
                    m_displaceNode.reset(new RprUsd_BaseRuntimeNode(RPR_MATERIAL_NODE_CONSTANT_TEXTURE, m_ctx));
                }

                m_displaceNode->SetInput(RPR_MATERIAL_INPUT_VALUE, value);
                m_displacementOutput = VtValue(m_displaceNode);
            } else {
                m_displaceNode = nullptr;
                m_displacementOutput = VtValue();
            }
        }
    } else if (UsdPreviewSurfaceTokens->normal == inputId) {
        if (value.IsHolding<RprMaterialNodePtr>()) {
            if (!m_normalMapNode) {
                m_normalMapNode.reset(new RprUsd_BaseRuntimeNode(RPR_MATERIAL_NODE_NORMAL_MAP, m_ctx));
            }
            m_normalMapNode->SetInput(RPR_MATERIAL_INPUT_COLOR, value);

            auto normalMapOutput = m_normalMapNode->GetOutput(TfToken());
            return SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_DIFFUSE_NORMAL, normalMapOutput) &&
                   SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_REFLECTION_NORMAL, normalMapOutput);
        } else {
            TF_RUNTIME_ERROR("`normal` input should be of material node type - %s", value.GetTypeName().c_str());
            return false;
        }
    } else {
        TF_CODING_ERROR("Unknown UsdPreviewSurface parameter %s: %s", inputId.GetText(), value.GetTypeName().c_str());
        return false;
    }

    return true;
}

VtValue RprUsd_UsdPreviewSurface::GetOutput(TfToken const& outputId) {
    if (HdMaterialTerminalTokens->surface == outputId) {
        if (m_useSpecular) {
            RPR_ERROR_CHECK(m_rprNode->SetInput(RPR_MATERIAL_INPUT_UBER_REFLECTION_MODE, RPR_UBER_MATERIAL_IOR_MODE_PBR), "Failed to set material input");
            SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR, m_reflection);
        } else {
            RPR_ERROR_CHECK(m_rprNode->SetInput(RPR_MATERIAL_INPUT_UBER_REFLECTION_MODE, RPR_UBER_MATERIAL_IOR_MODE_METALNESS), "Failed to set material input");
            SetRprInput(m_rprNode.get(), RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR, m_albedo);
        }

        return RprUsd_BaseRuntimeNode::GetOutput(outputId);
    } else if (HdMaterialTerminalTokens->displacement == outputId) {
        return m_displacementOutput;
    }

    return VtValue();
}

//------------------------------------------------------------------------------
// RprUsd_UsdUVTexture
//------------------------------------------------------------------------------

TF_DEFINE_PUBLIC_TOKENS(RprUsd_UsdUVTextureTokens, RPRUSD_USD_UV_TEXTURE_TOKENS);

namespace {

rpr::ImageWrapType GetWrapType(VtValue const& value) {
    if (value.IsHolding<TfToken>()) {
        auto& id = value.UncheckedGet<TfToken>();
        if (id == RprUsd_UsdUVTextureTokens->black) {
            return RPR_IMAGE_WRAP_TYPE_CLAMP_ZERO;
        } else if (id == RprUsd_UsdUVTextureTokens->clamp) {
            return RPR_IMAGE_WRAP_TYPE_CLAMP_TO_EDGE;
        } else if (id == RprUsd_UsdUVTextureTokens->mirror) {
            return RPR_IMAGE_WRAP_TYPE_MIRRORED_REPEAT;
        } else if (id == RprUsd_UsdUVTextureTokens->repeat) {
            return RPR_IMAGE_WRAP_TYPE_REPEAT;
        } else {
            TF_CODING_ERROR("Unknown image wrap type: %s", id.GetText());
        }
    }

    return {};
}

} // namespace anonymous

RprUsd_UsdUVTexture::RprUsd_UsdUVTexture(
    RprUsd_MaterialBuilderContext* ctx,
    std::map<TfToken, VtValue> const& hydraParameters)
    : m_ctx(ctx) {

    auto fileIt = hydraParameters.find(RprUsd_UsdUVTextureTokens->file);
    if (fileIt == hydraParameters.end()) {
        throw RprUsd_NodeError("UsdUVTexture requires file parameter");
    }

    RprUsdMaterialRegistry::TextureCommit textureCommit = {};

    if (fileIt->second.IsHolding<SdfAssetPath>()) {
        auto& assetPath = fileIt->second.UncheckedGet<SdfAssetPath>();
        if (assetPath.GetResolvedPath().empty()) {
            textureCommit.filepath = assetPath.GetAssetPath();
        } else {
            textureCommit.filepath = assetPath.GetResolvedPath();
        }
    }

    if (textureCommit.filepath.empty()) {
        throw RprUsd_NodeError("UsdUVTexture: empty file path");
    }

    auto colorSpaceIt = hydraParameters.find(RprUsd_UsdUVTextureTokens->colorSpace);
    if (colorSpaceIt != hydraParameters.end() &&
        colorSpaceIt->second.Get<TfToken>() == RprUsd_UsdUVTextureTokens->linear) {
        textureCommit.forceLinearSpace = true;
    }

    rpr::ImageWrapType wrapS = {};
    auto wrapSIt = hydraParameters.find(RprUsd_UsdUVTextureTokens->wrapS);
    if (wrapSIt != hydraParameters.end()) wrapS = GetWrapType(wrapSIt->second);

    rpr::ImageWrapType wrapT = {};
    auto wrapTIt = hydraParameters.find(RprUsd_UsdUVTextureTokens->wrapT);
    if (wrapTIt != hydraParameters.end()) wrapT = GetWrapType(wrapTIt->second);

    if (wrapS || wrapT) {
        if (wrapS != wrapT) {
            TF_RUNTIME_ERROR("RPR renderer does not support different wrapS and wrapT modes");
        }

        textureCommit.wrapType = wrapS ? wrapS : wrapT;
    }

    rpr::Status status;
    m_imageNode.reset(ctx->rprContext->CreateMaterialNode(RPR_MATERIAL_NODE_IMAGE_TEXTURE, &status));
    if (!m_imageNode) {
        throw RprUsd_NodeError(RPR_GET_ERROR_MESSAGE(status, "Failed to create image texture material node"));
    }
    m_outputs[RprUsd_UsdUVTextureTokens->rgba] = VtValue(m_imageNode);

    textureCommit.setTextureCallback = [this](std::shared_ptr<RprUsdCoreImage> const& image) {
        if (!image) return;

        if (!RPR_ERROR_CHECK(m_imageNode->SetInput(RPR_MATERIAL_INPUT_DATA, image->GetRootImage()), "Failed to set material node image data input")) {
            m_image = image;
        }
    };

    // Texture loading is postponed to allow multi-threading loading.
    //
    RprUsdMaterialRegistry::GetInstance().CommitTexture(std::move(textureCommit));

    auto scaleIt = hydraParameters.find(RprUsd_UsdUVTextureTokens->scale);
    if (scaleIt != hydraParameters.end() &&
        scaleIt->second.IsHolding<GfVec4f>()) {
        auto& scale = scaleIt->second.UncheckedGet<GfVec4f>();
        if (!GfIsEqual(scale, GfVec4f(1.0f))) {
            m_scaleNode = RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_MUL, m_ctx);
            if (m_scaleNode) {
                if (!m_scaleNode->SetInput(0, m_outputs[RprUsd_UsdUVTextureTokens->rgba]) ||
                    !m_scaleNode->SetInput(1, scaleIt->second) ||
                    m_scaleNode->GetOutput().IsEmpty()) {
                    m_scaleNode = nullptr;
                } else {
                    m_outputs[RprUsd_UsdUVTextureTokens->rgba] = m_scaleNode->GetOutput();
                }
            }
        }
    }

    auto biasIt = hydraParameters.find(RprUsd_UsdUVTextureTokens->bias);
    if (biasIt != hydraParameters.end() &&
        biasIt->second.IsHolding<GfVec4f>()) {
        auto& bias = biasIt->second.UncheckedGet<GfVec4f>();
        if (!GfIsEqual(bias, GfVec4f(0.0f))) {
            m_biasNode = RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_ADD, m_ctx);
            if (m_biasNode) {
                if (!m_biasNode->SetInput(0, m_outputs[RprUsd_UsdUVTextureTokens->rgba]) ||
                    !m_biasNode->SetInput(1, biasIt->second) ||
                    m_biasNode->GetOutput().IsEmpty()) {
                    m_biasNode = nullptr;
                } else {
                    m_outputs[RprUsd_UsdUVTextureTokens->rgba] = m_biasNode->GetOutput();
                }
            }
        }
    }
}

VtValue RprUsd_UsdUVTexture::GetOutput(TfToken const& outputId) {
    if (outputId == RprUsd_UsdUVTextureTokens->rgb) {
        return GetOutput(RprUsd_UsdUVTextureTokens->rgba);
    }

    auto outputIt = m_outputs.find(outputId);
    if (outputIt != m_outputs.end()) {
        return outputIt->second;
    }

    rpr::MaterialNodeArithmeticOperation channel;
    if (outputId == RprUsd_UsdUVTextureTokens->r) {
        channel = RPR_MATERIAL_NODE_OP_SELECT_X;
    } else if (outputId == RprUsd_UsdUVTextureTokens->g) {
        channel = RPR_MATERIAL_NODE_OP_SELECT_Y;
    } else if (outputId == RprUsd_UsdUVTextureTokens->b) {
        channel = RPR_MATERIAL_NODE_OP_SELECT_Z;
    } else if (outputId == RprUsd_UsdUVTextureTokens->a) {
        channel = RPR_MATERIAL_NODE_OP_SELECT_W;
    } else {
        TF_CODING_ERROR("Invalid outputId requested: %s", outputId.GetText());
        return VtValue();
    }

    auto selectChannelNode = RprUsd_RprArithmeticNode::Create(channel, m_ctx);
    if (selectChannelNode) {
        if (selectChannelNode->SetInput(0, m_outputs[RprUsd_UsdUVTextureTokens->rgba]) &&
            !selectChannelNode->GetOutput().IsEmpty()) {
            auto output = selectChannelNode->GetOutput();
            m_outputs[outputId] = output;
            return output;
        } else {
            TF_RUNTIME_ERROR("Failed to set select node inputs");
        }
    } else {
        TF_RUNTIME_ERROR("Failed to create select node");
    }

    return VtValue();
}

bool RprUsd_UsdUVTexture::SetInput(
    TfToken const& inputId,
    VtValue const& value) {
    if (inputId == RprUsd_UsdUVTextureTokens->st) {
        return SetRprInput(m_imageNode.get(), RPR_MATERIAL_INPUT_UV, value);
    } else {
        TF_CODING_ERROR("UsdUVTexture accepts only `st` input");
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
// RprUsd_UsdPrimvarReader
//------------------------------------------------------------------------------

TF_DEFINE_PRIVATE_TOKENS(UsdPrimvarReaderTokens,
    (varname)
);

RprUsd_UsdPrimvarReader::RprUsd_UsdPrimvarReader(
    RprUsd_MaterialBuilderContext* ctx,
    std::map<TfToken, VtValue> const& hydraParameters)
    : RprUsd_BaseRuntimeNode(RPR_MATERIAL_NODE_INPUT_LOOKUP, ctx) {
    // Primvar reader node is a node that allows the user to read arbitrary primvar.
    // There is no such functionality in RPR, at least the one exposed in the same expressive manner.
    //
    // TODO: I think that we can use buffer sampler node to fully implement primvar reader but this is only an idea
    //
    // For now, we allow the creation of float2 primvar reader only
    // and we use it only for one purpose - control over mesh UVs.
    //
    // We are using this node to get the name of the primvar that should be interpreted as UVs.
    // Also, from the node logic standpoint, this node is fully correct -
    // it outputs actual UVs so that the user can manipulate it in any way.

    auto varnameNameIt = hydraParameters.find(UsdPrimvarReaderTokens->varname);
    if (varnameNameIt != hydraParameters.end() &&
        varnameNameIt->second.IsHolding<TfToken>()) {
        auto& varname = varnameNameIt->second.UncheckedGet<TfToken>();
        if (!varname.IsEmpty()) {
            ctx->uvPrimvarName = varname;
        }
    }

    auto status = m_rprNode->SetInput(RPR_MATERIAL_INPUT_VALUE, RPR_MATERIAL_NODE_LOOKUP_UV);
    if (status != RPR_SUCCESS) {
        throw RprUsd_NodeError(RPR_GET_ERROR_MESSAGE(status, "Failed to set lookup node input", ctx->rprContext));
    }
}

bool RprUsd_UsdPrimvarReader::SetInput(
    TfToken const& inputId,
    VtValue const& value) {
    TF_RUNTIME_ERROR("No inputs supported. Got %s", inputId.GetText());
    return false;
}

//------------------------------------------------------------------------------
// RprUsd_UsdTransform2d
//------------------------------------------------------------------------------

TF_DEFINE_PRIVATE_TOKENS(UsdTransform2dTokens,
    (rotation)
    (scale)
    (translation)
);

RprUsd_UsdTransform2d::RprUsd_UsdTransform2d(
    RprUsd_MaterialBuilderContext* ctx,
    std::map<TfToken, VtValue> const& hydraParameters)
    : m_ctx(ctx) {

    float rotationDegrees = 0.0f;
    GfVec2f scale(1.0f);
    GfVec2f translation(0.0f);

    auto rotationIt = hydraParameters.find(UsdTransform2dTokens->rotation);
    if (rotationIt != hydraParameters.end() &&
        rotationIt->second.IsHolding<float>()) {
        rotationDegrees = rotationIt->second.UncheckedGet<float>();
    }

    auto scaleIt = hydraParameters.find(UsdTransform2dTokens->scale);
    if (scaleIt != hydraParameters.end() &&
        scaleIt->second.IsHolding<GfVec2f>()) {
        scale = scaleIt->second.UncheckedGet<GfVec2f>();
    }

    auto translationIt = hydraParameters.find(UsdTransform2dTokens->translation);
    if (translationIt != hydraParameters.end() &&
        translationIt->second.IsHolding<GfVec2f>()) {
        translation = translationIt->second.UncheckedGet<GfVec2f>();
    }

    if (rotationDegrees == 0.0f &&
        scale == GfVec2f(1.0f) &&
        translation == GfVec2f(0.0f)) {
        throw RprUsd_NodeEmpty();
    }

    float rotation = GfDegreesToRadians(rotationDegrees);
    float rotCos = std::cos(rotation);
    float rotSin = std::sin(rotation);

    // XXX (Houdini): Proposal of UsdPreviewSurface states that rotation is
    //  "Counter-clockwise rotation in degrees around the origin",
    //  by default, the origin is the zero point on the UV coordinate system
    //  but Houdini's Karma uses origin = (0.5, 0.5). We do the same right now
    GfVec2f origin(0.5f);

    GfMatrix3f uvTransform(
        1.0, 0.0, -origin[0],
        0.0, 1.0, -origin[1],
        0.0, 0.0, 1.0);
    uvTransform = GfMatrix3f(
        scale[0], 0.0, 0.0,
        0.0, scale[1], 0.0,
        0.0, 0.0, 1.0) * uvTransform;
    uvTransform = GfMatrix3f(
        rotCos, -rotSin, 0.0,
        rotSin, rotCos, 0.0,
        0.0f, 0.0f, 1.0f) * uvTransform;
    uvTransform[0][2] += translation[0] + origin[0];
    uvTransform[1][2] += translation[1] + origin[1];

    m_setZToOneNode = RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_ADD, m_ctx);
    m_transformNode = RprUsd_RprArithmeticNode::Create(RPR_MATERIAL_NODE_OP_MAT_MUL, m_ctx);

    if (!m_setZToOneNode || !m_transformNode) {
        throw RprUsd_NodeError("Failed to create required arithmetic nodes");
    }

    auto& m = uvTransform;
    if (!m_setZToOneNode->SetInput(0, VtValue(GfVec4f(0.0f, 0.0f, 1.0f, 0.0f))) ||
        !m_transformNode->SetInput(0, VtValue(GfVec4f(m[0][0], m[0][1], m[0][2], 0.0f))) ||
        !m_transformNode->SetInput(1, VtValue(GfVec4f(m[1][0], m[1][1], m[1][2], 0.0f))) ||
        !m_transformNode->SetInput(2, VtValue(GfVec4f(m[2][0], m[2][1], m[2][2], 0.0f)))) {
        throw RprUsd_NodeError("Failed to set arithmetic node inputs");
    }
}

VtValue RprUsd_UsdTransform2d::GetOutput(TfToken const& outputId) {
    return m_transformNode->GetOutput();
}

bool RprUsd_UsdTransform2d::SetInput(
    TfToken const& inputId,
    VtValue const& value) {
    return m_setZToOneNode->SetInput(1, value) && m_transformNode->SetInput(3, m_setZToOneNode->GetOutput());
}

template <typename T>
RprUsd_MaterialNode* RprUsd_CreateUsdNode(
    RprUsd_MaterialBuilderContext* ctx,
    std::map<TfToken, VtValue> const& params) {
    return new T(ctx, params);
}

template <typename T>
void RprUsd_RegisterUsdNode(const char* id) {
    RprUsdMaterialRegistry::GetInstance().Register(
        TfToken(id, TfToken::Immortal),
        &RprUsd_CreateUsdNode<T>);
}

ARCH_CONSTRUCTOR(RprUsd_RegisterUsdNodes, 255, void) {
    RprUsd_RegisterUsdNode<RprUsd_UsdPreviewSurface>("UsdPreviewSurface");
    RprUsd_RegisterUsdNode<RprUsd_UsdPrimvarReader>("UsdPrimvarReader_float2");
    RprUsd_RegisterUsdNode<RprUsd_UsdTransform2d>("UsdTransform2d");
    RprUsd_RegisterUsdNode<RprUsd_UsdUVTexture>("UsdUVTexture");
}

PXR_NAMESPACE_CLOSE_SCOPE
