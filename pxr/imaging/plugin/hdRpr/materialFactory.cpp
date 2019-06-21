#include "materialFactory.h"

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

class RprMaterial : public RprApiMaterial
{
public:
	~RprMaterial() {
	}

	const rpr_material_node & GetRootRprMaterialNode() const { return m_material; }

	rpr_material_node & GetRootRprMaterialNode() { return m_material; }

private:
	rpr_material_node m_material;
};

class RprxMaterial : public RprApiMaterial
{
public:
	~RprxMaterial() {}

	const rprx_material & GetRprxMaterial() const { return m_material; }

	rprx_material & GetRprxMaterial() { return m_material; }
private:
	rprx_material m_material;
};


RprMaterialFactory::RprMaterialFactory(rpr_material_system matSys) : m_matSys(matSys)
{
}

RprApiMaterial * RprMaterialFactory::CreateMaterial(const EMaterialType type)
{
	rpr_material_node_type materialType = 0;


	if (type == EMaterialType::COLOR)
	{
		materialType = RPR_MATERIAL_NODE_DIFFUSE;
	}

	if (type == EMaterialType::EMISSIVE)
	{
		materialType = RPR_MATERIAL_NODE_EMISSIVE;
	}

	if (type == EMaterialType::TRANSPERENT)
	{
		materialType = RPR_MATERIAL_NODE_TRANSPARENT;
	}

	if (!materialType)
	{
		return nullptr;
	}

	RprMaterial * material = new RprMaterial();
	material->SetType(type);

	rpr_material_node & rootMaterialNode = material->GetRootRprMaterialNode();

	rprMaterialSystemCreateNode(m_matSys, materialType, &rootMaterialNode);
	return material;

}

void RprMaterialFactory::AttachMaterialToShape(rpr_shape mesh, const RprApiMaterial * material)
{
	const RprMaterial * rprMaterial = static_cast<const RprMaterial *>(material);
	rprShapeSetMaterial(mesh, rprMaterial->GetRootRprMaterialNode());
}

void RprMaterialFactory::AttachCurveToShape(rpr_shape curve, const RprApiMaterial * material)
{
	const RprMaterial * rprMaterial = static_cast<const RprMaterial *>(material);
	rprCurveSetMaterial(curve, rprMaterial->GetRootRprMaterialNode());
}

void RprMaterialFactory::SetMaterialInputs(RprApiMaterial * material, const MaterialAdapter & materialAdapter)
{
	RprMaterial * rprMaterial = static_cast<RprMaterial *>(material);

	for (auto param : materialAdapter.GetVec4fRprParams())
	{
		const TfToken & paramName = param.first;
		const GfVec4f & paramValue = param.second;

		rprMaterialNodeSetInputF(rprMaterial->GetRootRprMaterialNode(), paramName.data(), paramValue[0], paramValue[1], paramValue[2], paramValue[3]);
	}
}

void RprMaterialFactory::DeleteMaterial(RprApiMaterial * rprmaterial)
{
	RprMaterial * rprMaterial = static_cast<RprMaterial *>(rprmaterial);
	SAFE_DELETE_RPR_OBJECT(rprMaterial->GetRootRprMaterialNode());
}




RprXMaterialFactory::RprXMaterialFactory(rpr_material_system matSys, rpr_context rprContext) : m_matSys(matSys), m_context(rprContext){
	rprxCreateContext(m_matSys, 0, &m_contextX);
}

RprXMaterialFactory::~RprXMaterialFactory()
{
	rprxDeleteContext(m_contextX);
}

RprApiMaterial * RprXMaterialFactory::CreateMaterial(const EMaterialType type)
{
	rpr_material_node_type materialType = 0;

	materialType = RPRX_MATERIAL_UBER;

	if (!materialType)
	{
		return nullptr;
	}

	RprxMaterial * material = new RprxMaterial();
	material->SetType(type);

	rprx_material & rprxMaterial = material->GetRprxMaterial();

	rprxCreateMaterial(m_contextX, materialType, &rprxMaterial);

	return material;
}

void RprXMaterialFactory::AttachMaterialToShape(rpr_shape mesh, const RprApiMaterial * material)
{
	const RprxMaterial * rprxMaterial = static_cast<const RprxMaterial *>(material);
	rprxShapeAttachMaterial(m_contextX, mesh, rprxMaterial->GetRprxMaterial());

	rprxMaterialCommit(m_contextX, rprxMaterial->GetRprxMaterial());

}

void RprXMaterialFactory::AttachCurveToShape(rpr_shape mesh, const RprApiMaterial * material)
{
	// No-op
}

void RprXMaterialFactory::SetMaterialInputs(RprApiMaterial * material, const MaterialAdapter & materialAdapter)
{
	RprxMaterial * rprxMaterial = static_cast<RprxMaterial *>(material);

	for (auto param : materialAdapter.GetVec4fRprxParams())
	{
		const uint32_t & paramId = param.first;
		const GfVec4f & paramValue = param.second;

		rprxMaterialSetParameterF(m_contextX, rprxMaterial->GetRprxMaterial(), paramId, paramValue[0], paramValue[1], paramValue[2], paramValue[3]);
	}

	for (auto param : materialAdapter.GetURprxParams())
	{
		const uint32_t & paramId = param.first;
		const uint32_t & paramValue = param.second;

		rprxMaterialSetParameterU(m_contextX, rprxMaterial->GetRprxMaterial(), paramId, paramValue);
	}

	for (auto texParam : materialAdapter.GetTexRprxParams())
	{
		rpr_int status;
		const uint32_t & paramId = texParam.first;
		const MaterialTexture & matTex = texParam.second;

		rpr_image_wrap_type rprWrapSType;
		rpr_image_wrap_type rprWrapTType;

		rpr_image img = NULL;
		rpr_material_node materialNode = NULL;

		status = rprContextCreateImageFromFile(m_context, texParam.second.Path.c_str(), &img);
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail create image %s  Error code %d", texParam.second.Path.c_str(), status);
			continue;
		}

		if (getWrapType(matTex.WrapS, rprWrapSType) && getWrapType(matTex.WrapT, rprWrapTType))
		{
			if (rprWrapSType != rprWrapTType)
			{
				TF_CODING_WARNING("RPR renderer does not support different WrapS and WrapT modes");
			}
			rprImageSetWrap(img, rprWrapSType);
		}

		
		rprMaterialSystemCreateNode(m_matSys, RPR_MATERIAL_NODE_IMAGE_TEXTURE, &materialNode);
		rprMaterialNodeSetInputImageData(materialNode, "data", img);

		// TODO: one minus src color


		if (matTex.IsScaleEmable || matTex.IsBiasEmable)
		{
			rpr_material_node uv_node = NULL;
			status = rprMaterialSystemCreateNode(m_matSys, RPR_MATERIAL_NODE_INPUT_LOOKUP, &uv_node);
			status = rprMaterialNodeSetInputU(uv_node, "value", RPR_MATERIAL_NODE_LOOKUP_UV);

			rpr_material_node uv_scaled_node;
			rpr_material_node uv_bias_node;

			
			if (matTex.IsScaleEmable)
			{
				const GfVec4f & scale = matTex.Scale;
				status = rprMaterialSystemCreateNode(m_matSys, RPR_MATERIAL_NODE_ARITHMETIC, &uv_scaled_node);

				status = rprMaterialNodeSetInputU(uv_scaled_node, "op", RPR_MATERIAL_NODE_OP_MUL);
				status = rprMaterialNodeSetInputN(uv_scaled_node, "color0", uv_node);
				status = rprMaterialNodeSetInputF(uv_scaled_node, "color1", scale[0], scale[1], scale[2], 0);
			}

			if (matTex.IsBiasEmable)
			{
				const GfVec4f & bias = matTex.Bias;
				rpr_material_node & color0Input = (matTex.IsScaleEmable) ? uv_scaled_node : uv_node;
				
				status = rprMaterialSystemCreateNode(m_matSys, RPR_MATERIAL_NODE_ARITHMETIC, &uv_bias_node);
				
				status = rprMaterialNodeSetInputU(uv_bias_node, "op", RPR_MATERIAL_NODE_OP_ADD);
				status = rprMaterialNodeSetInputN(uv_bias_node, "color0", color0Input);
				status = rprMaterialNodeSetInputF(uv_bias_node, "color1", bias[0], bias[1], bias[2], 0);
			}

			rpr_material_node & uvIn = (matTex.IsBiasEmable) ? uv_bias_node : uv_scaled_node;
			rprMaterialNodeSetInputN(materialNode, "uv", uvIn);
		
		}

		rpr_material_node outTexture = NULL;
		if (matTex.Chanel != EColorChanel::NONE)
		{
			rpr_int selectedChanel = NULL;
			
			if (getSelectedChanel(matTex.Chanel, selectedChanel))
			{
				rpr_material_node arithmetic = NULL;
				status = rprMaterialSystemCreateNode(m_matSys, RPR_MATERIAL_NODE_ARITHMETIC, &arithmetic);
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

		rprxMaterialSetParameterN(m_contextX, rprxMaterial->GetRprxMaterial(), paramId, outTexture);
	}
}

void RprXMaterialFactory::DeleteMaterial(RprApiMaterial * material)
{
	RprxMaterial * rprxMaterial = static_cast<RprxMaterial *>(material);
	rprxMaterialDelete(m_contextX, rprxMaterial->GetRprxMaterial());
	delete rprxMaterial;
}


PXR_NAMESPACE_CLOSE_SCOPE