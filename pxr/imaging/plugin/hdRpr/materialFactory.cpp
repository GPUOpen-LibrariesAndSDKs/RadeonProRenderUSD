#include "materialFactory.h"

#include <pxr/imaging/glf/uvTextureData.h>

PXR_NAMESPACE_OPEN_SCOPE

#define SAFE_DELETE_RPR_OBJECT(x) if(x) {rprObjectDelete( x ); x = nullptr;}

bool getWrapType(const EWrapMode & wrapMode, rpr_image_wrap_type & rprWrapType)
{
	switch (wrapMode)
	{
	case EWrapMode::BLACK:
		rprWrapType = RPR_IMAGE_WRAP_TYPE_CLAMP_ZERO;
		return true;
	case EWrapMode::CLAMP:
		rprWrapType = FR_IMAGE_WRAP_TYPE_CLAMP_TO_EDGE;
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

bool getSelectedChanel(const EColorChanel & colorChanel, rpr_int & out_selectedChanel)
{
	switch (colorChanel)
	{
	case EColorChanel::R:
		out_selectedChanel = RPR_MATERIAL_NODE_OP_SELECT_X;
		return true;
	case EColorChanel::G:
		out_selectedChanel = RPR_MATERIAL_NODE_OP_SELECT_Y;
		return true;
	case EColorChanel::B:
		out_selectedChanel = RPR_MATERIAL_NODE_OP_SELECT_Z;
		return true;
	case EColorChanel::A:
		out_selectedChanel = RPR_MATERIAL_NODE_OP_SELECT_W;
		return true;
	default:
		break;
	}
	return false;
}

RprMaterialFactory::RprMaterialFactory(rpr_material_system matSys, rpr_context rprContext) : m_matSys(matSys), m_context(rprContext)
{
}

RprApiMaterial* RprMaterialFactory::CreateMaterial(EMaterialType type, const MaterialAdapter & materialAdapter)
{
    rpr_material_node_type materialType = 0;

    switch (type) {
        case EMaterialType::COLOR:
            materialType = RPR_MATERIAL_NODE_DIFFUSE;
            break;
        case EMaterialType::EMISSIVE:
            materialType = RPR_MATERIAL_NODE_EMISSIVE;
            break;
        case EMaterialType::TRANSPERENT:
            materialType = RPR_MATERIAL_NODE_TRANSPARENT;
            break;
        case EMaterialType::USD_PREVIEW_SURFACE:
            materialType = RPR_MATERIAL_NODE_UBERV2;
            break;
        default:
            return nullptr;
    }

    auto material = new RprApiMaterial;
    rprMaterialSystemCreateNode(m_matSys, materialType, &material->rootMaterial);

    for (auto const& param : materialAdapter.GetVec4fRprParams())
    {
        const TfToken & paramName = param.first;
        const GfVec4f & paramValue = param.second;

        rprMaterialNodeSetInputF(material->rootMaterial, paramName.data(), paramValue[0], paramValue[1], paramValue[2], paramValue[3]);
    }

    for (auto const& param : materialAdapter.GetVec4fRprxParams())
    {
        const uint32_t & paramId = param.first;
        const GfVec4f & paramValue = param.second;

        rprMaterialNodeSetInputFByKey(material->rootMaterial, paramId, paramValue[0], paramValue[1], paramValue[2], paramValue[3]);
    }

    for (auto param : materialAdapter.GetURprxParams())
    {
        const uint32_t & paramId = param.first;
        const uint32_t & paramValue = param.second;

        rprMaterialNodeSetInputUByKey(material->rootMaterial, paramId, paramValue);
    }

    auto getTextureMaterialNode = [](rpr_context rprContext, rpr_material_system matSys, MaterialTexture const& matTex) -> rpr_material_node {
        if (matTex.Path.empty()) {
            return nullptr;
        }

        rpr_image_wrap_type rprWrapSType;
        rpr_image_wrap_type rprWrapTType;

        rpr_int status = RPR_SUCCESS;
        rpr_image img = nullptr;
        rpr_material_node materialNode = nullptr;

        auto textureData = GlfUVTextureData::New(matTex.Path, INT_MAX, 0, 0, 0, 0);
        if (!textureData || !textureData->Read(0, false)) {
            TF_CODING_ERROR("Failed to read image %s", matTex.Path.c_str());
            return nullptr;
        }

        rpr_image_format format = {};
        int bytesPerChannel = 0;
        switch (textureData->GLType()) {
            case GL_UNSIGNED_BYTE:
                format.type = RPR_COMPONENT_TYPE_UINT8;
                bytesPerChannel = 1;
                break;
            case GL_FLOAT:
                format.type = RPR_COMPONENT_TYPE_FLOAT32;
                bytesPerChannel = 4;
                break;
            default:
                TF_CODING_ERROR("Failed to create image %s. Unsupported pixel data GLtype: %#x", matTex.Path.c_str(), textureData->GLType());
                return nullptr;
        }

        switch (textureData->GLFormat()) {
            case GL_RED:
                format.num_components = 1;
                break;
            case GL_RGB:
                format.num_components = 3;
                break;
            case GL_RGBA:
                format.num_components = 4;
                break;
            default:
                TF_CODING_ERROR("Failed to create image %s. Unsupported pixel data GLformat: %#x", matTex.Path.c_str(), textureData->GLFormat());
                return nullptr;
        }
        int bytesPerPixel = bytesPerChannel * format.num_components;

        rpr_image_desc desc = {};
        desc.image_width = textureData->ResizedWidth();
        desc.image_height = textureData->ResizedHeight();
        desc.image_depth = 1;
        desc.image_row_pitch = bytesPerPixel * desc.image_width;
        desc.image_slice_pitch = desc.image_row_pitch * desc.image_height;
        status = rprContextCreateImage(rprContext, format, &desc, textureData->GetRawBuffer(), &img);
        if (status != RPR_SUCCESS) {
            TF_CODING_ERROR("Failed to create image %s  Error code %d", matTex.Path.c_str(), status);
            return nullptr;
        }

        if (getWrapType(matTex.WrapS, rprWrapSType) && getWrapType(matTex.WrapT, rprWrapTType))
        {
            if (rprWrapSType != rprWrapTType)
            {
                TF_CODING_WARNING("RPR renderer does not support different WrapS and WrapT modes");
            }
            rprImageSetWrap(img, rprWrapSType);
        }


        rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_IMAGE_TEXTURE, &materialNode);
        rprMaterialNodeSetInputImageData(materialNode, "data", img);

        // TODO: one minus src color

        if (matTex.IsScaleEnabled || matTex.IsBiasEnabled)
        {
            rpr_material_node uv_node = nullptr;
            status = rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_INPUT_LOOKUP, &uv_node);
            status = rprMaterialNodeSetInputU(uv_node, "value", RPR_MATERIAL_NODE_LOOKUP_UV);

            rpr_material_node uv_scaled_node;
            rpr_material_node uv_bias_node;


            if (matTex.IsScaleEnabled)
            {
                const GfVec4f & scale = matTex.Scale;
                status = rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &uv_scaled_node);

                status = rprMaterialNodeSetInputU(uv_scaled_node, "op", RPR_MATERIAL_NODE_OP_MUL);
                status = rprMaterialNodeSetInputN(uv_scaled_node, "color0", uv_node);
                status = rprMaterialNodeSetInputF(uv_scaled_node, "color1", scale[0], scale[1], scale[2], 0);
            }

            if (matTex.IsBiasEnabled)
            {
                const GfVec4f & bias = matTex.Bias;
                rpr_material_node & color0Input = (matTex.IsScaleEnabled) ? uv_scaled_node : uv_node;

                status = rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &uv_bias_node);

                status = rprMaterialNodeSetInputU(uv_bias_node, "op", RPR_MATERIAL_NODE_OP_ADD);
                status = rprMaterialNodeSetInputN(uv_bias_node, "color0", color0Input);
                status = rprMaterialNodeSetInputF(uv_bias_node, "color1", bias[0], bias[1], bias[2], 0);
            }

            rpr_material_node & uvIn = (matTex.IsBiasEnabled) ? uv_bias_node : uv_scaled_node;
            rprMaterialNodeSetInputN(materialNode, "uv", uvIn);

        }

        rpr_material_node outTexture = nullptr;
        if (matTex.Chanel != EColorChanel::NONE)
        {
            rpr_int selectedChanel = 0;

            if (getSelectedChanel(matTex.Chanel, selectedChanel))
            {
                rpr_material_node arithmetic = nullptr;
                status = rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &arithmetic);
                status = rprMaterialNodeSetInputN(arithmetic, "color0", materialNode);
                status = rprMaterialNodeSetInputF(arithmetic, "color1", 0.0, 0.0, 0.0, 0.0);
                status = rprMaterialNodeSetInputU(arithmetic, "op", selectedChanel);

                outTexture = arithmetic;
            }
            else
            {
                outTexture = materialNode;
            }
        }
        else
        {
            outTexture = materialNode;
        }

        return outTexture;
    };

    for (auto const& texParam : materialAdapter.GetTexRprxParams())
    {
        const uint32_t & paramId = texParam.first;
        const MaterialTexture & matTex = texParam.second;

        rpr_material_node outTexture = getTextureMaterialNode(m_context, m_matSys, matTex);
        if (!outTexture) {
            continue;
        }

        rprMaterialNodeSetInputNByKey(material->rootMaterial, paramId, outTexture);
    }

    material->displacementMaterial = getTextureMaterialNode(m_context, m_matSys, materialAdapter.GetDisplacementTexture());

    return material;
}

void RprMaterialFactory::DeleteMaterial(RprApiMaterial* material)
{
    if (!material) {
        return;
    }

    SAFE_DELETE_RPR_OBJECT(material->rootMaterial);
    SAFE_DELETE_RPR_OBJECT(material->displacementMaterial);
    delete material;
}

void RprMaterialFactory::AttachMaterialToShape(rpr_shape mesh, const RprApiMaterial * material)
{
	rprShapeSetMaterial(mesh, material->rootMaterial);
	if (material->displacementMaterial) {
	    rprShapeSetDisplacementMaterial(mesh, material->displacementMaterial);
	}
}

void RprMaterialFactory::AttachCurveToShape(rpr_shape curve, const RprApiMaterial * material)
{
	rprCurveSetMaterial(curve, material->rootMaterial);
}

PXR_NAMESPACE_CLOSE_SCOPE