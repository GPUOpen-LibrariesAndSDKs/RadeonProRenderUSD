#include "materialFactory.h"
#include "rprcpp/rprImage.h"
#include "pxr/usd/ar/resolver.h"

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

RprMaterialFactory::RprMaterialFactory(rpr_material_system matSys, ImageCache* imageCache) : m_matSys(matSys), m_imageCache(imageCache)
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
        const uint32_t & paramId = param.first;
        const GfVec4f & paramValue = param.second;

        rprMaterialNodeSetInputFByKey(material->rootMaterial, paramId, paramValue[0], paramValue[1], paramValue[2], paramValue[3]);
    }

    for (auto param : materialAdapter.GetURprParams())
    {
        const uint32_t & paramId = param.first;
        const uint32_t & paramValue = param.second;

        rprMaterialNodeSetInputUByKey(material->rootMaterial, paramId, paramValue);
    }

    auto getTextureMaterialNode = [&material](ImageCache* imageCache, rpr_material_system matSys, MaterialTexture const& matTex) -> rpr_material_node {
        if (matTex.Path.empty()) {
            return nullptr;
        }

        rpr_int status = RPR_SUCCESS;

        auto image = imageCache->GetImage(matTex.Path);
        if (!image) {
            return nullptr;
        }
        auto rprImage = image->GetHandle();
        material->materialImages.push_back(std::move(image));

        rpr_image_wrap_type rprWrapSType;
        rpr_image_wrap_type rprWrapTType;
        if (getWrapType(matTex.WrapS, rprWrapSType) && getWrapType(matTex.WrapT, rprWrapTType))
        {
            if (rprWrapSType != rprWrapTType)
            {
                TF_CODING_WARNING("RPR renderer does not support different WrapS and WrapT modes");
            }
            rprImageSetWrap(rprImage, rprWrapSType);
        }

        rpr_material_node materialNode = nullptr;
        rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_IMAGE_TEXTURE, &materialNode);
        rprMaterialNodeSetInputImageDataByKey(materialNode, RPR_MATERIAL_INPUT_DATA, rprImage);
		material->materialNodes.push_back(materialNode);

        if (matTex.IsScaleEnabled || matTex.IsBiasEnabled)
        {
            rpr_material_node uv_node = nullptr;
            status = rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_INPUT_LOOKUP, &uv_node);
            status = rprMaterialNodeSetInputUByKey(uv_node, RPR_MATERIAL_INPUT_VALUE, RPR_MATERIAL_NODE_LOOKUP_UV);
            material->materialNodes.push_back(uv_node);

            rpr_material_node uv_scaled_node;
            rpr_material_node uv_bias_node;


            if (matTex.IsScaleEnabled)
            {
                const GfVec4f & scale = matTex.Scale;
                status = rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &uv_scaled_node);
                material->materialNodes.push_back(uv_scaled_node);

                status = rprMaterialNodeSetInputUByKey(uv_scaled_node, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_MUL);
                status = rprMaterialNodeSetInputNByKey(uv_scaled_node, RPR_MATERIAL_INPUT_COLOR0, uv_node);
                status = rprMaterialNodeSetInputFByKey(uv_scaled_node, RPR_MATERIAL_INPUT_COLOR1, scale[0], scale[1], scale[2], 0);
            }

            if (matTex.IsBiasEnabled)
            {
                const GfVec4f & bias = matTex.Bias;
                rpr_material_node & color0Input = (matTex.IsScaleEnabled) ? uv_scaled_node : uv_node;

                status = rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &uv_bias_node);
				material->materialNodes.push_back(uv_bias_node);

                status = rprMaterialNodeSetInputUByKey(uv_bias_node, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_ADD);
                status = rprMaterialNodeSetInputNByKey(uv_bias_node, RPR_MATERIAL_INPUT_COLOR0, color0Input);
                status = rprMaterialNodeSetInputFByKey(uv_bias_node, RPR_MATERIAL_INPUT_COLOR1, bias[0], bias[1], bias[2], 0);
            }

            rpr_material_node & uvIn = (matTex.IsBiasEnabled) ? uv_bias_node : uv_scaled_node;
            rprMaterialNodeSetInputNByKey(materialNode, RPR_MATERIAL_INPUT_UV, uvIn);
        }

        if (matTex.IsOneMinusSrcColor) {
            rpr_material_node arithmetic = nullptr;
            status = rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &arithmetic);
            status = rprMaterialNodeSetInputFByKey(arithmetic, RPR_MATERIAL_INPUT_COLOR0, 1.0, 1.0, 1.0, 1.0);
            status = rprMaterialNodeSetInputNByKey(arithmetic, RPR_MATERIAL_INPUT_COLOR1, materialNode);
            status = rprMaterialNodeSetInputUByKey(arithmetic, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_SUB);
            material->materialNodes.push_back(arithmetic);

            materialNode = arithmetic;
        }

        rpr_material_node outTexture = nullptr;
        if (matTex.Chanel != EColorChanel::NONE)
        {
            rpr_int selectedChanel = 0;

            if (getSelectedChanel(matTex.Chanel, selectedChanel))
            {
                rpr_material_node arithmetic = nullptr;
                status = rprMaterialSystemCreateNode(matSys, RPR_MATERIAL_NODE_ARITHMETIC, &arithmetic);
                status = rprMaterialNodeSetInputNByKey(arithmetic, RPR_MATERIAL_INPUT_COLOR0, materialNode);
                status = rprMaterialNodeSetInputFByKey(arithmetic, RPR_MATERIAL_INPUT_COLOR1, 0.0, 0.0, 0.0, 0.0);
                status = rprMaterialNodeSetInputUByKey(arithmetic, RPR_MATERIAL_INPUT_OP, selectedChanel);
				material->materialNodes.push_back(arithmetic);

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

    for (auto const& texParam : materialAdapter.GetTexRprParams())
    {
        const uint32_t & paramId = texParam.first;
        const MaterialTexture & matTex = texParam.second;

        rpr_material_node outTexture = getTextureMaterialNode(m_imageCache, m_matSys, matTex);
        if (!outTexture) {
            continue;
        }

        // SIGGRAPH HACK: Fix for models from Apple AR quick look gallery.
        //   Of all the available ways to load images, none of them gives the opportunity to
        //   get the gamma of the image and does not convert the image to linear space
        if ((paramId == RPR_UBER_MATERIAL_INPUT_DIFFUSE_COLOR ||
            paramId == RPR_UBER_MATERIAL_INPUT_REFLECTION_COLOR) &&
            ArGetResolver().GetExtension(matTex.Path) == "png") {
            rprImageSetGamma(material->materialImages.back()->GetHandle(), 2.2f);
        }

        // normal map textures need to be passed through the normal map node
        if (paramId == RPR_UBER_MATERIAL_INPUT_DIFFUSE_NORMAL ||
            paramId == RPR_UBER_MATERIAL_INPUT_REFLECTION_NORMAL) {
            rpr_material_node temp = outTexture;
            int status = rprMaterialSystemCreateNode(m_matSys, RPR_MATERIAL_NODE_NORMAL_MAP, &outTexture);
            status = rprMaterialNodeSetInputNByKey(outTexture, RPR_MATERIAL_INPUT_COLOR, temp);
        }

        rprMaterialNodeSetInputNByKey(material->rootMaterial, paramId, outTexture);
    }

    material->displacementMaterial = getTextureMaterialNode(m_imageCache, m_matSys, materialAdapter.GetDisplacementTexture());

    return material;
}

void RprMaterialFactory::DeleteMaterial(RprApiMaterial* material)
{
    if (!material) {
        return;
    }

    SAFE_DELETE_RPR_OBJECT(material->rootMaterial);
    SAFE_DELETE_RPR_OBJECT(material->displacementMaterial);
    for (auto node : material->materialNodes)
    {
        SAFE_DELETE_RPR_OBJECT(node);
    }
    delete material;
}

void RprMaterialFactory::AttachMaterialToShape(rpr_shape mesh, const RprApiMaterial* material) {
    if (material) {
        rprShapeSetMaterial(mesh, material->rootMaterial);
        if (material->displacementMaterial) {
            rprShapeSetDisplacementMaterial(mesh, material->displacementMaterial);
        }
    } else {
        rprShapeSetMaterial(mesh, nullptr);
    }
}

void RprMaterialFactory::AttachMaterialToCurve(rpr_shape curve, const RprApiMaterial* material) {
    rprCurveSetMaterial(curve, material ? material->rootMaterial : nullptr);
}

PXR_NAMESPACE_CLOSE_SCOPE