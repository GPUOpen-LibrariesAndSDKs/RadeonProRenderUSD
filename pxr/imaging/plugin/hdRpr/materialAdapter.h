#ifndef HDRPR_MATERIAL_ADAPTER_H
#define HDRPR_MATERIAL_ADAPTER_H

#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/imaging/hd/material.h"

PXR_NAMESPACE_OPEN_SCOPE



enum class EWrapMode
{
	NONE = -1
	, BLACK
	, CLAMP
	, REPEAT
	, MIRROR
};

enum class EColorChanel
{
	NONE = -1
	, RGBA
	, RGB
	, R
	, G
	, B
	, A

};

struct MaterialTexture
{
	std::string Path;

	EColorChanel Chanel = EColorChanel::NONE;

	EWrapMode WrapS = EWrapMode::NONE;
	EWrapMode WrapT = EWrapMode::NONE;

	bool IsScaleEnabled = false;
	GfVec4f Scale;

	bool IsBiasEnabled = false;
	GfVec4f Bias;
};


typedef std::map<TfToken, VtValue> MaterialParams;
typedef std::map<TfToken, MaterialTexture> MaterialTextures;

typedef std::map<uint32_t, GfVec4f> MaterialRprParamsVec4f;
typedef std::map<uint32_t, uint32_t> MaterialRprParamsU;
typedef std::map<uint32_t, MaterialTexture> MaterialRprParamsTexture;

enum class EMaterialType : int32_t
{
	NONE = -1
	, COLOR = 0
	, EMISSIVE 
	, TRANSPERENT
	, USD_PREVIEW_SURFACE
	,
};


class MaterialAdapter
{
public:
	MaterialAdapter(const EMaterialType type, const MaterialParams & params);

	MaterialAdapter(const EMaterialType type, const HdMaterialNetwork & materialNetwork);

	const EMaterialType GetType() const
	{
		return m_type;
	}

	const MaterialRprParamsVec4f & GetVec4fRprParams() const
	{
		return m_vec4fRprParams;
	}

	const MaterialRprParamsU & GetURprParams() const
	{
		return m_uRprParams;
	}

	const MaterialRprParamsTexture & GetTexRprParams() const
	{
		return m_texRpr;
	}

	const MaterialTexture& GetDisplacementTexture() const
	{
	    return m_displacementTexture;
	}

private:
	void PopulateRprColor(const MaterialParams & params);
	void PopulateEmissive(const MaterialParams & params);
	void PopulateTransparent(const MaterialParams & params);
	void PopulateUsdPreviewSurface(const MaterialParams & params, const MaterialTextures & textures);

	const EMaterialType m_type;

	MaterialRprParamsVec4f m_vec4fRprParams;
	MaterialRprParamsU	m_uRprParams;
	MaterialRprParamsTexture m_texRpr;

    MaterialTexture m_displacementTexture;
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_ADAPTER_H