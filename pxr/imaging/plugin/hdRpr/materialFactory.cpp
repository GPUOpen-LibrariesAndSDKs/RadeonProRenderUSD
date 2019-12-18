#include "materialFactory.h"
#include "rprcpp/rprImage.h"
#include "pxr/usd/ar/resolver.h"

PXR_NAMESPACE_OPEN_SCOPE

#define SAFE_DELETE_RPR_OBJECT(x) if(x) {rprObjectDelete( x ); x = nullptr;}

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

RprMaterialFactory::RprMaterialFactory(rpr_material_system matSys,
                                       ImageCache* imageCache)
    : m_matSys(matSys),
    m_imageCache(imageCache) {

}

RprApiMaterial* RprMaterialFactory::CreateMaterial(EMaterialType type, const MaterialAdapter& materialAdapter) {
    rpr_material_node_type materialType = 0;

    switch (type) {
        case EMaterialType::EMISSIVE:
            materialType = RPR_MATERIAL_NODE_EMISSIVE;
            break;
        case EMaterialType::TRANSPERENT:
            materialType = RPR_MATERIAL_NODE_TRANSPARENT;
            break;
        case EMaterialType::COLOR:
        case EMaterialType::USD_PREVIEW_SURFACE:
            materialType = RPR_MATERIAL_NODE_UBERV2;
            break;
        default:
            return nullptr;
    }

    rpr_material_node rootMaterialNode = nullptr;
    if (RPR_ERROR_CHECK(rprMaterialSystemCreateNode(m_matSys, materialType, &rootMaterialNode), "Failed to create material node")) {
        return nullptr;
    }

    auto material = new RprApiMaterial;
    material->rootMaterial = rootMaterialNode;

    if (materialAdapter.IsDoublesided()) {
        if (!RPR_ERROR_CHECK(rprMaterialSystemCreateNode(m_matSys, RPR_MATERIAL_NODE_TWOSIDED, &material->twosidedNode), "Failed to create twosided node")) {
            RPR_ERROR_CHECK(rprMaterialNodeSetInputNByKey(material->twosidedNode, RPR_MATERIAL_INPUT_FRONTFACE, rootMaterialNode), "Failed to set front face input of twosided node");
        }
    }

    for (auto const& param : materialAdapter.GetVec4fRprParams()) {
        const uint32_t& paramId = param.first;
        const GfVec4f& paramValue = param.second;

        if (materialAdapter.GetTexRprParams().count(paramId)) {
            continue;
        }
        RPR_ERROR_CHECK(rprMaterialNodeSetInputFByKey(material->rootMaterial, paramId, paramValue[0], paramValue[1], paramValue[2], paramValue[3]), "Failed to set material node vec4 input");
    }

    for (auto param : materialAdapter.GetURprParams()) {
        const uint32_t& paramId = param.first;
        const uint32_t& paramValue = param.second;

        RPR_ERROR_CHECK(rprMaterialNodeSetInputUByKey(material->rootMaterial, paramId, paramValue), "Failed to set material node uint input");
    }

    auto getTextureMaterialNode = [&material](ImageCache* imageCache, rpr_material_system matSys, MaterialTexture const& matTex) -> rpr_material_node {
        if (matTex.path.empty()) {
            return nullptr;
        }

        auto image = imageCache->GetImage(matTex.path);
        if (!image) {
            return nullptr;
        }
        auto rprImage = image->GetHandle();
        material->materialImages.push_back(std::move(image));

        rpr_image_wrap_type rprWrapSType;
        rpr_image_wrap_type rprWrapTType;
        if (GetWrapType(matTex.wrapS, rprWrapSType) && GetWrapType(matTex.wrapT, rprWrapTType)) {
            if (rprWrapSType != rprWrapTType) {
                TF_CODING_WARNING("RPR renderer does not support different WrapS and WrapT modes");
            }
            rprImageSetWrap(rprImage, rprWrapSType);
        }

        rpr_material_node materialNode = nullptr;
        RPR_ERROR_CHECK(rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_IMAGE_TEXTURE, &materialNode), "Failed to create image texture material node");
        if (!materialNode) {
            return nullptr;
        }

        RPR_ERROR_CHECK(rprMaterialNodeSetInputImageDataByKey(materialNode, RPR_MATERIAL_INPUT_DATA, rprImage), "Failed to set material node image data input");
        material->materialNodes.push_back(materialNode);

        if (!GfIsEqual(matTex.uvTransform, GfMatrix3f(1.0f))) {
            rpr_material_node uvLookupNode = nullptr;
            RPR_ERROR_CHECK(rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_INPUT_LOOKUP, &uvLookupNode), "Failed to create uv lookup material node");
            if (uvLookupNode) {
                RPR_ERROR_CHECK(rprMaterialNodeSetInputUByKey(uvLookupNode, RPR_MATERIAL_INPUT_VALUE, RPR_MATERIAL_NODE_LOOKUP_UV), "Failed to set material node uint input");

                rpr_material_node transformUvNode = nullptr;
                RPR_ERROR_CHECK(rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &transformUvNode), "Failed to create arithmetic material node");
                if (transformUvNode) {
                    // XXX (RPR): due to missing functionality to set explicitly third component of UV vector to 1
                    // third component set to 1 using addition
                    rpr_material_node setZtoOneNode = nullptr;
                    RPR_ERROR_CHECK(rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &setZtoOneNode), "Failed to create arithmetic material node");
                    RPR_ERROR_CHECK(rprMaterialNodeSetInputUByKey(setZtoOneNode, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_ADD), "Failed to set material node uint input");
                    RPR_ERROR_CHECK(rprMaterialNodeSetInputFByKey(setZtoOneNode, RPR_MATERIAL_INPUT_COLOR0, 0.0f, 0.0f, 1.0f, 0.0f), "Failed to set material node vec4 input");
                    RPR_ERROR_CHECK(rprMaterialNodeSetInputNByKey(setZtoOneNode, RPR_MATERIAL_INPUT_COLOR1, uvLookupNode), "Failed to set material node node input");

                    RPR_ERROR_CHECK(rprMaterialNodeSetInputUByKey(transformUvNode, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_MAT_MUL), "Failed to set material node uint input");
                    RPR_ERROR_CHECK(rprMaterialNodeSetInputFByKey(transformUvNode, RPR_MATERIAL_INPUT_COLOR0, matTex.uvTransform[0][0], matTex.uvTransform[0][1], matTex.uvTransform[0][2], 0.0f), "Failed to set material node vec4 input");
                    RPR_ERROR_CHECK(rprMaterialNodeSetInputFByKey(transformUvNode, RPR_MATERIAL_INPUT_COLOR1, matTex.uvTransform[1][0], matTex.uvTransform[1][1], matTex.uvTransform[1][2], 0.0f), "Failed to set material node vec4 input");
                    RPR_ERROR_CHECK(rprMaterialNodeSetInputFByKey(transformUvNode, RPR_MATERIAL_INPUT_COLOR2, matTex.uvTransform[2][0], matTex.uvTransform[2][1], matTex.uvTransform[2][2], 0.0f), "Failed to set material node vec4 input");
                    RPR_ERROR_CHECK(rprMaterialNodeSetInputNByKey(transformUvNode, RPR_MATERIAL_INPUT_COLOR3, setZtoOneNode), "Failed to set material node node input");

                    RPR_ERROR_CHECK(rprMaterialNodeSetInputNByKey(materialNode, RPR_MATERIAL_INPUT_UV, transformUvNode), "Failed to set material node node input");

                    material->materialNodes.push_back(transformUvNode);
                    material->materialNodes.push_back(setZtoOneNode);
                    material->materialNodes.push_back(uvLookupNode);
                } else {
                    RPR_ERROR_CHECK(rprObjectDelete(uvLookupNode), "Failed to release uv lookup node");
                }
            }
        }

        if (!GfIsEqual(matTex.scale, GfVec4f(1.0f))) {
            rpr_material_node arithmetic = nullptr;
            RPR_ERROR_CHECK(rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &arithmetic), "Failed to set material node vec4 input");
            if (arithmetic) {
                RPR_ERROR_CHECK(rprMaterialNodeSetInputUByKey(arithmetic, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_MUL), "Failed to set material node uint input");
                RPR_ERROR_CHECK(rprMaterialNodeSetInputNByKey(arithmetic, RPR_MATERIAL_INPUT_COLOR0, materialNode), "Failed to set material node node input");
                RPR_ERROR_CHECK(rprMaterialNodeSetInputFByKey(arithmetic, RPR_MATERIAL_INPUT_COLOR1, matTex.scale[0], matTex.scale[1], matTex.scale[2], matTex.scale[3]), "Failed to set material node vec4 input");
                material->materialNodes.push_back(arithmetic);

                materialNode = arithmetic;
            }
        }

        if (!GfIsEqual(matTex.bias, GfVec4f(0.0f))) {
            rpr_material_node arithmetic = nullptr;
            RPR_ERROR_CHECK(rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &arithmetic), "Failed to create arithmetic material node");
            if (arithmetic) {
                RPR_ERROR_CHECK(rprMaterialNodeSetInputUByKey(arithmetic, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_ADD), "Failed to set material node uint input");
                RPR_ERROR_CHECK(rprMaterialNodeSetInputNByKey(arithmetic, RPR_MATERIAL_INPUT_COLOR0, materialNode), "Failed to set material node node input");
                RPR_ERROR_CHECK(rprMaterialNodeSetInputFByKey(arithmetic, RPR_MATERIAL_INPUT_COLOR1, matTex.bias[0], matTex.bias[1], matTex.bias[2], matTex.bias[3]), "Failed to set material node vec4 input");
                material->materialNodes.push_back(arithmetic);

                materialNode = arithmetic;
            }
        }

        rpr_material_node outTexture = nullptr;
        if (matTex.channel != EColorChannel::NONE) {
            rpr_int selectedChannel = 0;

            if (GetSelectedChannel(matTex.channel, selectedChannel)) {
                rpr_material_node arithmetic = nullptr;
                RPR_ERROR_CHECK(rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &arithmetic), "Failed to create arithmetic material node");
                if (arithmetic) {
                    RPR_ERROR_CHECK(rprMaterialNodeSetInputNByKey(arithmetic, RPR_MATERIAL_INPUT_COLOR0, materialNode), "Failed to set material node node input");
                    RPR_ERROR_CHECK(rprMaterialNodeSetInputFByKey(arithmetic, RPR_MATERIAL_INPUT_COLOR1, 0.0, 0.0, 0.0, 0.0), "Failed to set material node vec4 input");
                    RPR_ERROR_CHECK(rprMaterialNodeSetInputUByKey(arithmetic, RPR_MATERIAL_INPUT_OP, selectedChannel), "Failed to set material node uint input");
                    material->materialNodes.push_back(arithmetic);

                    outTexture = arithmetic;
                }
            } else {
                outTexture = materialNode;
            }
        } else {
            outTexture = materialNode;
        }

        return outTexture;
    };

    rpr_material_node emissionColorNode = nullptr;

    for (auto const& texParam : materialAdapter.GetTexRprParams()) {
        const uint32_t& paramId = texParam.first;
        const MaterialTexture& matTex = texParam.second;

        rpr_material_node outNode = getTextureMaterialNode(m_imageCache, m_matSys, matTex);
        if (!outNode) {
            continue;
        }

        if (paramId == RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR) {
            emissionColorNode = outNode;
        }

        // SIGGRAPH HACK: Fix for models from Apple AR quick look gallery.
        //   Of all the available ways to load images, none of them gives the opportunity to
        //   get the gamma of the image and does not convert the image to linear space
        if ((paramId == RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR ||
             paramId == RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR) &&
            ArGetResolver().GetExtension(matTex.path) == "png") {
            rprImageSetGamma(material->materialImages.back()->GetHandle(), 2.2f);
        }

        // normal map textures need to be passed through the normal map node
        if (paramId == RPR_MATERIAL_INPUT_UBER_DIFFUSE_NORMAL ||
            paramId == RPR_MATERIAL_INPUT_UBER_REFLECTION_NORMAL) {
            rpr_material_node textureNode = outNode;
            if (!RPR_ERROR_CHECK(rprMaterialSystemCreateNode(m_matSys, RPR_MATERIAL_NODE_NORMAL_MAP, &outNode), "Failed to create normal map material node")) {
                material->materialNodes.push_back(outNode);
                RPR_ERROR_CHECK(rprMaterialNodeSetInputNByKey(outNode, RPR_MATERIAL_INPUT_COLOR, textureNode), "Failed to set material node node input");
            }
        }

        rprMaterialNodeSetInputNByKey(material->rootMaterial, paramId, outNode);
    }

    if (emissionColorNode) {
        rpr_material_node averageNode = nullptr;
        rprMaterialSystemCreateNode(m_matSys, RPR_MATERIAL_NODE_ARITHMETIC, &averageNode);
        if (averageNode) {
            rprMaterialNodeSetInputUByKey(averageNode, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_AVERAGE_XYZ);
            rprMaterialNodeSetInputNByKey(averageNode, RPR_MATERIAL_INPUT_COLOR0, emissionColorNode);

            rpr_material_node isBlackColorNode = nullptr;
            rprMaterialSystemCreateNode(m_matSys, RPR_MATERIAL_NODE_ARITHMETIC, &isBlackColorNode);
            if (isBlackColorNode) {
                material->materialNodes.push_back(averageNode);
                material->materialNodes.push_back(isBlackColorNode);
                rprMaterialNodeSetInputUByKey(isBlackColorNode, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_GREATER);
                rprMaterialNodeSetInputNByKey(isBlackColorNode, RPR_MATERIAL_INPUT_COLOR0, averageNode);
                rprMaterialNodeSetInputFByKey(isBlackColorNode, RPR_MATERIAL_INPUT_COLOR1, 0.0f, 0.0f, 0.0f, 0.0f);

                rprMaterialNodeSetInputNByKey(material->rootMaterial, RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT, isBlackColorNode);
            } else {
                rprObjectDelete(averageNode);
            }
        }
    }

    material->displacementMaterial = getTextureMaterialNode(m_imageCache, m_matSys, materialAdapter.GetDisplacementTexture());

    return material;
}

void RprMaterialFactory::DeleteMaterial(RprApiMaterial* material) {
    if (!material) {
        return;
    }

    if (!material->materialImages.empty()) {
        m_imageCache->RequireGarbageCollection();
    }

    SAFE_DELETE_RPR_OBJECT(material->rootMaterial);
    SAFE_DELETE_RPR_OBJECT(material->twosidedNode);
    for (auto node : material->materialNodes) {
        SAFE_DELETE_RPR_OBJECT(node);
    }
    delete material;
}

void RprMaterialFactory::AttachMaterialToShape(rpr_shape mesh, const RprApiMaterial* material, bool doublesided, bool displacementEnabled) {
    if (material) {
        if (material->twosidedNode) {
            RPR_ERROR_CHECK(rprMaterialNodeSetInputNByKey(material->twosidedNode, RPR_MATERIAL_INPUT_BACKFACE, doublesided ? material->rootMaterial : nullptr), "Failed to set back face input of twosided node");
            RPR_ERROR_CHECK(rprShapeSetMaterial(mesh, material->twosidedNode), "Failed to set shape material");
        } else {
            RPR_ERROR_CHECK(rprShapeSetMaterial(mesh, material->rootMaterial), "Failed to set shape material");
        }

        if (displacementEnabled && material->displacementMaterial) {
            size_t dummy;
            int subdFactor;
            if (RPR_ERROR_CHECK(rprShapeGetInfo(mesh, RPR_SHAPE_SUBDIVISION_FACTOR, sizeof(subdFactor), &subdFactor, &dummy), "Failed to query mesh subdivision factor")) {
                subdFactor = 0;
            }

            if (subdFactor == 0) {
                TF_WARN("Displacement material requires subdivision to be enabled. The subdivision will be enabled with refine level of 1");
                if (!RPR_ERROR_CHECK(rprShapeSetSubdivisionFactor(mesh, 1), "Failed to set mesh subdividion")) {
                    subdFactor = 1;
                }
            }
            if (subdFactor > 0) {
                RPR_ERROR_CHECK(rprShapeSetDisplacementMaterial(mesh, material->displacementMaterial), "Failed to set shape displacement material");
            }
        } else {
            RPR_ERROR_CHECK(rprShapeSetDisplacementMaterial(mesh, nullptr), "Failed to unset shape displacement material");
        }
    } else {
        RPR_ERROR_CHECK(rprShapeSetMaterial(mesh, nullptr), "Failed to unset shape material");
        RPR_ERROR_CHECK(rprShapeSetDisplacementMaterial(mesh, nullptr), "Failed to unset shape displacement material");
    }
}

void RprMaterialFactory::AttachMaterialToCurve(rpr_shape curve, const RprApiMaterial* material) {
    rprCurveSetMaterial(curve, material ? material->rootMaterial : nullptr);
}

PXR_NAMESPACE_CLOSE_SCOPE