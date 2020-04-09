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

#include "materialFactory.h"
#include "imageCache.h"

#include "rpr/error.h"

#include <RadeonProRender.hpp>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

bool GfIsEqual(GfVec4f const& v1, GfVec4f const& v2, float tolerance = 1e-5f) {
    return std::abs(v1[0] - v2[0]) <= tolerance &&
           std::abs(v1[1] - v2[1]) <= tolerance &&
           std::abs(v1[2] - v2[2]) <= tolerance &&
           std::abs(v1[3] - v2[3]) <= tolerance;
}

bool GfIsEqual(GfMatrix3f const& m1, GfMatrix3f const& m2, float tolerance = 1e-5f) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (std::abs(m1[i][j] - m2[i][j]) >= tolerance) {
                return false;
            }
        }
    }
    return true;
}

bool GetWrapType(const EWrapMode& wrapMode, rpr_image_wrap_type& rprWrapType) {
    switch (wrapMode) {
        case EWrapMode::BLACK:
            rprWrapType = RPR_IMAGE_WRAP_TYPE_CLAMP_ZERO;
            return true;
        case EWrapMode::CLAMP:
            rprWrapType = RPR_IMAGE_WRAP_TYPE_CLAMP_TO_EDGE;
            return true;
        case EWrapMode::MIRROR:
            rprWrapType = RPR_IMAGE_WRAP_TYPE_MIRRORED_REPEAT;
            return true;
        case EWrapMode::REPEAT:
            rprWrapType = RPR_IMAGE_WRAP_TYPE_REPEAT;
            return true;
        default:
            break;
    }
    return false;
}

bool GetSelectedChannel(const EColorChannel& colorChannel, rpr_int& out_selectedChannel) {
    switch (colorChannel) {
        case EColorChannel::R:
            out_selectedChannel = RPR_MATERIAL_NODE_OP_SELECT_X;
            return true;
        case EColorChannel::G:
            out_selectedChannel = RPR_MATERIAL_NODE_OP_SELECT_Y;
            return true;
        case EColorChannel::B:
            out_selectedChannel = RPR_MATERIAL_NODE_OP_SELECT_Z;
            return true;
        case EColorChannel::A:
            out_selectedChannel = RPR_MATERIAL_NODE_OP_SELECT_W;
            return true;
        default:
            break;
    }
    return false;
}

} // namespace anonymous

RprMaterialFactory::RprMaterialFactory(ImageCache* imageCache)
    : m_imageCache(imageCache) {

}

HdRprApiMaterial* RprMaterialFactory::CreatePointsMaterial(VtVec3fArray const& colors) {
    auto context = m_imageCache->GetContext();

    auto setupPointsMaterial = [&colors, context](HdRprApiMaterial* material) -> bool {
        rpr::Status status;
        auto rootMaterialNode = context->CreateMaterialNode(RPR_MATERIAL_NODE_UBERV2, &status);
        if (!rootMaterialNode) {
            return !RPR_ERROR_CHECK(status, "Failed to create material node");
        }
        material->rootMaterial = rootMaterialNode;

        rpr::BufferDesc bufferDesc;
        bufferDesc.nb_element = colors.size();
        bufferDesc.element_type = RPR_BUFFER_ELEMENT_TYPE_FLOAT32;
        bufferDesc.element_channel_size = 3;

        auto colorsBuffer = context->CreateBuffer(bufferDesc, colors.data(), &status);
        if (!colorsBuffer) {
            return !RPR_ERROR_CHECK(status, "Failed to create colors buffer");
        }
        material->auxiliaryObjects.push_back(colorsBuffer);

        auto lookupIndex = context->CreateMaterialNode(RPR_MATERIAL_NODE_INPUT_LOOKUP, &status);
        if (!lookupIndex) {
            return !RPR_ERROR_CHECK(status, "Failed to create input lookup node");
        }
        material->auxiliaryObjects.push_back(lookupIndex);
        if (RPR_ERROR_CHECK(lookupIndex->SetInput(RPR_MATERIAL_INPUT_VALUE, RPR_MATERIAL_NODE_LOOKUP_OBJECT_ID), "Failed to set lookup node input value")) {
            return false;
        }

        auto bufferSampler = context->CreateMaterialNode(RPR_MATERIAL_NODE_BUFFER_SAMPLER, &status);
        if (!bufferSampler) {
            return !RPR_ERROR_CHECK(status, "Failed to create buffer sampler node");
        }
        material->auxiliaryObjects.push_back(bufferSampler);

        if (RPR_ERROR_CHECK(bufferSampler->SetInput(RPR_MATERIAL_INPUT_DATA, colorsBuffer), "Failed to set buffer sampler node input data") ||
            RPR_ERROR_CHECK(bufferSampler->SetInput(RPR_MATERIAL_INPUT_UV, lookupIndex), "Failed to set buffer sampler node input uv") ||
            RPR_ERROR_CHECK(rootMaterialNode->SetInput(RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR, bufferSampler), "Failed to set root material diffuse color")) {
            return false;
        }

        return true;
    };

    auto material = new HdRprApiMaterial;
    if (!setupPointsMaterial(material)) {
        Release(material);
        material = nullptr;
    }

    return material;
}

HdRprApiMaterial* RprMaterialFactory::CreateMaterial(EMaterialType type, const MaterialAdapter& materialAdapter) {
    rpr::MaterialNodeType materialType;

    switch (type) {
        case EMaterialType::EMISSIVE:
            materialType = RPR_MATERIAL_NODE_EMISSIVE;
            break;
        case EMaterialType::TRANSPERENT:
            materialType = RPR_MATERIAL_NODE_TRANSPARENT;
            break;
        case EMaterialType::COLOR:
        case EMaterialType::USD_PREVIEW_SURFACE:
        case EMaterialType::HOUDINI_PRINCIPLED_SHADER:
            materialType = RPR_MATERIAL_NODE_UBERV2;
            break;
        default:
            return nullptr;
    }

    auto context = m_imageCache->GetContext();

    rpr::Status status;
    auto rootMaterialNode = context->CreateMaterialNode(materialType, &status);
    if (!rootMaterialNode) {
        RPR_ERROR_CHECK(status, "Failed to create material node");
        return nullptr;
    }

    auto material = new HdRprApiMaterial;
    material->rootMaterial = rootMaterialNode;

    if (materialAdapter.IsDoublesided()) {
        material->twosidedNode = context->CreateMaterialNode(RPR_MATERIAL_NODE_TWOSIDED, &status);
        if (!material->twosidedNode) {
            RPR_ERROR_CHECK(status, "Failed to create twosided node");
        } else {
            RPR_ERROR_CHECK(material->twosidedNode->SetInput(RPR_MATERIAL_INPUT_FRONTFACE, rootMaterialNode), "Failed to set front face input of twosided node");
        }
    }

    for (auto const& param : materialAdapter.GetVec4fRprParams()) {
        auto& paramId = param.first;
        auto& paramValue = param.second;

        if (materialAdapter.GetTexRprParams().count(paramId)) {
            continue;
        }
        RPR_ERROR_CHECK(material->rootMaterial->SetInput(paramId, paramValue[0], paramValue[1], paramValue[2], paramValue[3]), "Failed to set material node vec4 input");
    }

    for (auto param : materialAdapter.GetURprParams()) {
        auto& paramId = param.first;
        auto& paramValue = param.second;

        RPR_ERROR_CHECK(material->rootMaterial->SetInput(paramId, paramValue), "Failed to set material node uint input");
    }

    auto getTextureMaterialNode = [&material](ImageCache* imageCache, MaterialTexture const& matTex) -> rpr::MaterialNode* {
        if (matTex.path.empty()) {
            return nullptr;
        }

        auto image = imageCache->GetImage(matTex.path, matTex.forceLinearSpace);
        if (!image) {
            return nullptr;
        }
        auto rprImage = image.get();
        material->materialImages.push_back(std::move(image));

        rpr::ImageWrapType rprWrapType;
        if (GetWrapType(matTex.wrapMode, rprWrapType)) {
            RPR_ERROR_CHECK(rprImage->SetWrap(rprWrapType), "Failed to set image wrap mode");
        }

        rpr::Status status;
        auto context = imageCache->GetContext();

        rpr::MaterialNode* materialNode = context->CreateMaterialNode(RPR_MATERIAL_NODE_IMAGE_TEXTURE, &status);
        if (!materialNode) {
            RPR_ERROR_CHECK(status, "Failed to create image texture material node");
            return nullptr;
        }

        RPR_ERROR_CHECK(materialNode->SetInput(RPR_MATERIAL_INPUT_DATA, rprImage), "Failed to set material node image data input");
        material->auxiliaryObjects.push_back(materialNode);

        if (!GfIsEqual(matTex.uvTransform, GfMatrix3f(1.0f))) {
            rpr::MaterialNode* uvLookupNode = context->CreateMaterialNode(RPR_MATERIAL_NODE_INPUT_LOOKUP, &status);
            if (uvLookupNode) {
                RPR_ERROR_CHECK(uvLookupNode->SetInput(RPR_MATERIAL_INPUT_VALUE, rpr_uint(RPR_MATERIAL_NODE_LOOKUP_UV)), "Failed to set material node uint input");

                rpr::MaterialNode* transformUvNode = context->CreateMaterialNode(RPR_MATERIAL_NODE_ARITHMETIC, &status);
                if (transformUvNode) {
                    // XXX (RPR): due to missing functionality to set explicitly third component of UV vector to 1
                    // third component set to 1 using addition
                    rpr::MaterialNode* setZtoOneNode = context->CreateMaterialNode(RPR_MATERIAL_NODE_ARITHMETIC, &status);
                    if (setZtoOneNode) {
                        RPR_ERROR_CHECK(setZtoOneNode->SetInput(RPR_MATERIAL_INPUT_OP, rpr_uint(RPR_MATERIAL_NODE_OP_ADD)), "Failed to set material node uint input");
                        RPR_ERROR_CHECK(setZtoOneNode->SetInput(RPR_MATERIAL_INPUT_COLOR0, 0.0f, 0.0f, 1.0f, 0.0f), "Failed to set material node vec4 input");
                        RPR_ERROR_CHECK(setZtoOneNode->SetInput(RPR_MATERIAL_INPUT_COLOR1, uvLookupNode), "Failed to set material node node input");

                        RPR_ERROR_CHECK(transformUvNode->SetInput(RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_MAT_MUL), "Failed to set material node uint input");
                        RPR_ERROR_CHECK(transformUvNode->SetInput(RPR_MATERIAL_INPUT_COLOR0, matTex.uvTransform[0][0], matTex.uvTransform[0][1], matTex.uvTransform[0][2], 0.0f), "Failed to set material node vec4 input");
                        RPR_ERROR_CHECK(transformUvNode->SetInput(RPR_MATERIAL_INPUT_COLOR1, matTex.uvTransform[1][0], matTex.uvTransform[1][1], matTex.uvTransform[1][2], 0.0f), "Failed to set material node vec4 input");
                        RPR_ERROR_CHECK(transformUvNode->SetInput(RPR_MATERIAL_INPUT_COLOR2, matTex.uvTransform[2][0], matTex.uvTransform[2][1], matTex.uvTransform[2][2], 0.0f), "Failed to set material node vec4 input");
                        RPR_ERROR_CHECK(transformUvNode->SetInput(RPR_MATERIAL_INPUT_COLOR3, setZtoOneNode), "Failed to set material node node input");

                        RPR_ERROR_CHECK(materialNode->SetInput(RPR_MATERIAL_INPUT_UV, transformUvNode), "Failed to set material node node input");

                        material->auxiliaryObjects.push_back(transformUvNode);
                        material->auxiliaryObjects.push_back(setZtoOneNode);
                        material->auxiliaryObjects.push_back(uvLookupNode);
                    } else {
                        RPR_ERROR_CHECK(status, "Failed to create arithmetic material node");
                        delete uvLookupNode;
                        delete transformUvNode;
                    }
                } else {
                    RPR_ERROR_CHECK(status, "Failed to create arithmetic material node");
                    delete uvLookupNode;
                }
            } else {
                RPR_ERROR_CHECK(status, "Failed to create uv lookup material node");
            }
        }

        if (!GfIsEqual(matTex.scale, GfVec4f(1.0f))) {
            rpr::MaterialNode* arithmetic = context->CreateMaterialNode(RPR_MATERIAL_NODE_ARITHMETIC, &status);
            if (arithmetic) {
                RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_MUL), "Failed to set material node uint input");
                RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_COLOR0, materialNode), "Failed to set material node node input");
                RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_COLOR1, matTex.scale[0], matTex.scale[1], matTex.scale[2], matTex.scale[3]), "Failed to set material node vec4 input");
                material->auxiliaryObjects.push_back(arithmetic);

                materialNode = arithmetic;
            } else {
                RPR_ERROR_CHECK(status, "Failed to set material node vec4 input");
            }
        }

        if (!GfIsEqual(matTex.bias, GfVec4f(0.0f))) {
            rpr::MaterialNode* arithmetic = context->CreateMaterialNode(RPR_MATERIAL_NODE_ARITHMETIC, &status);
            if (arithmetic) {
                RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_OP, rpr_uint(RPR_MATERIAL_NODE_OP_ADD)), "Failed to set material node uint input");
                RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_COLOR0, materialNode), "Failed to set material node node input");
                RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_COLOR1, matTex.bias[0], matTex.bias[1], matTex.bias[2], matTex.bias[3]), "Failed to set material node vec4 input");
                material->auxiliaryObjects.push_back(arithmetic);

                materialNode = arithmetic;
            } else {
                RPR_ERROR_CHECK(status, "Failed to create arithmetic material node");

            }
        }

        rpr::MaterialNode* outTexture = nullptr;
        if (matTex.channel != EColorChannel::NONE) {
            rpr_int selectedChannel = 0;

            if (GetSelectedChannel(matTex.channel, selectedChannel)) {
                rpr::MaterialNode* arithmetic = context->CreateMaterialNode(RPR_MATERIAL_NODE_ARITHMETIC, &status);
                if (arithmetic) {
                    RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_COLOR0, materialNode), "Failed to set material node node input");
                    RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_COLOR1, 0.0, 0.0, 0.0, 0.0), "Failed to set material node vec4 input");
                    RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_OP, selectedChannel), "Failed to set material node uint input");
                    material->auxiliaryObjects.push_back(arithmetic);

                    outTexture = arithmetic;
                } else {
                    RPR_ERROR_CHECK(status, "Failed to create arithmetic material node");
                }
            } else if (matTex.channel == EColorChannel::LUMINANCE) {
                rpr::MaterialNode* arithmetic = context->CreateMaterialNode(RPR_MATERIAL_NODE_ARITHMETIC, &status);
                if (arithmetic) {
                    RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_COLOR0, materialNode), "Failed to set material node node input");
                    RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_COLOR1, 0.2126, 0.7152, 0.0722, 0.0), "Failed to set material node vec4 input");
                    RPR_ERROR_CHECK(arithmetic->SetInput(RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_DOT3), "Failed to set material node uint input");
                    material->auxiliaryObjects.push_back(arithmetic);

                    outTexture = arithmetic;
                } else {
                    RPR_ERROR_CHECK(status, "Failed to create arithmetic material node");
                }
            } else {
                outTexture = materialNode;
            }
        } else {
            outTexture = materialNode;
        }

        return outTexture;
    };

    rpr::MaterialNode* emissionColorNode = nullptr;

    for (auto const& texParam : materialAdapter.GetTexRprParams()) {
        auto& paramId = texParam.first;
        auto& matTex = texParam.second;

        auto outNode = getTextureMaterialNode(m_imageCache, matTex);
        if (!outNode) {
            continue;
        }

        if (paramId == RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR) {
            emissionColorNode = outNode;
        }

        material->rootMaterial->SetInput(paramId, outNode);
    }

    for (auto const& normalMapParam : materialAdapter.GetNormalMapParams()) {
        auto textureNode = getTextureMaterialNode(m_imageCache, normalMapParam.second.texture);
        if (!textureNode) {
            continue;
        }

        auto normalMapNode = context->CreateMaterialNode(RPR_MATERIAL_NODE_NORMAL_MAP, &status);
        if (normalMapNode) {
            material->auxiliaryObjects.push_back(normalMapNode);
            RPR_ERROR_CHECK(normalMapNode->SetInput(RPR_MATERIAL_INPUT_COLOR, textureNode), "Failed to set material node node input");

            auto s = normalMapParam.second.effectScale;
            RPR_ERROR_CHECK(normalMapNode->SetInput(RPR_MATERIAL_INPUT_SCALE, s, s, s, s), "Failed to set material node node input");

            for (auto paramId : normalMapParam.first) {
                RPR_ERROR_CHECK(material->rootMaterial->SetInput(paramId, normalMapNode), "Failed to set normal map node");
            }
        } else {
            RPR_ERROR_CHECK(status, "Failed to create normal map material node");
        }
    }

    if (emissionColorNode) {
        auto averageNode = context->CreateMaterialNode(RPR_MATERIAL_NODE_ARITHMETIC, &status);
        if (averageNode) {
            averageNode->SetInput(RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_AVERAGE_XYZ);
            averageNode->SetInput(RPR_MATERIAL_INPUT_COLOR0, emissionColorNode);

            auto isBlackColorNode = context->CreateMaterialNode(RPR_MATERIAL_NODE_ARITHMETIC, &status);
            if (isBlackColorNode) {
                material->auxiliaryObjects.push_back(averageNode);
                material->auxiliaryObjects.push_back(isBlackColorNode);

                isBlackColorNode->SetInput(RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_GREATER);
                isBlackColorNode->SetInput(RPR_MATERIAL_INPUT_COLOR0, averageNode);
                isBlackColorNode->SetInput(RPR_MATERIAL_INPUT_COLOR1, 0.0f, 0.0f, 0.0f, 0.0f);

                material->rootMaterial->SetInput(RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT, isBlackColorNode);
            } else {
                RPR_ERROR_CHECK(status, "Failed to create isBlackColor node");
                delete averageNode;
            }
        } else {
            RPR_ERROR_CHECK(status, "Failed to create averaging node");
        }
    }

    material->displacementMaterial = getTextureMaterialNode(m_imageCache, materialAdapter.GetDisplacementTexture());

    return material;
}

void RprMaterialFactory::Release(HdRprApiMaterial* material) {
    if (!material) {
        return;
    }

    if (!material->materialImages.empty()) {
        m_imageCache->RequireGarbageCollection();
    }

    delete material->rootMaterial;
    delete material->twosidedNode;
    for (auto node : material->auxiliaryObjects) {
        delete node;
    }
    delete material;
}

void RprMaterialFactory::AttachMaterial(rpr::Shape* mesh, HdRprApiMaterial const* material, bool doublesided, bool displacementEnabled) {
    if (material) {
        if (material->twosidedNode) {
            RPR_ERROR_CHECK(material->twosidedNode->SetInput(RPR_MATERIAL_INPUT_BACKFACE, doublesided ? material->rootMaterial : nullptr), "Failed to set back face input of twosided node");
            RPR_ERROR_CHECK(mesh->SetMaterial(material->twosidedNode), "Failed to set shape material");
        } else {
            RPR_ERROR_CHECK(mesh->SetMaterial(material->rootMaterial), "Failed to set shape material");
        }

        if (displacementEnabled && material->displacementMaterial) {
            size_t dummy;
            int subdFactor;
            if (RPR_ERROR_CHECK(mesh->GetInfo(RPR_SHAPE_SUBDIVISION_FACTOR, sizeof(subdFactor), &subdFactor, &dummy), "Failed to query mesh subdivision factor")) {
                subdFactor = 0;
            }

            if (subdFactor == 0) {
                TF_WARN("Displacement material requires subdivision to be enabled. The subdivision will be enabled with refine level of 1");
                if (!RPR_ERROR_CHECK(mesh->SetSubdivisionFactor(1), "Failed to set mesh subdividion")) {
                    subdFactor = 1;
                }
            }
            if (subdFactor > 0) {
                RPR_ERROR_CHECK(mesh->SetDisplacementMaterial(material->displacementMaterial), "Failed to set shape displacement material");
            }
        } else {
            RPR_ERROR_CHECK(mesh->SetDisplacementMaterial(nullptr), "Failed to unset shape displacement material");
        }
    } else {
        RPR_ERROR_CHECK(mesh->SetMaterial(nullptr), "Failed to unset shape material");
        RPR_ERROR_CHECK(mesh->SetDisplacementMaterial(nullptr), "Failed to unset shape displacement material");
    }
}

void RprMaterialFactory::AttachMaterial(rpr::Curve* curve, HdRprApiMaterial const* material) {
    RPR_ERROR_CHECK(curve->SetMaterial(material ? material->rootMaterial : nullptr), "Failed to set curve material");
}

PXR_NAMESPACE_CLOSE_SCOPE