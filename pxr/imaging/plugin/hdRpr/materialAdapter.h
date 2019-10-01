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

typedef std::map<TfToken, GfVec4f> MaterialRprParamsVec4f;

typedef std::map<uint32_t, GfVec4f> MaterialRprxParamsVec4f;
typedef std::map<uint32_t, uint32_t> MaterialRprxParamsU;
typedef std::map<uint32_t, MaterialTexture> MaterialRprxParamsTexture;

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

	const MaterialRprxParamsVec4f & GetVec4fRprxParams() const
	{
		return m_vec4fRprxParams;
	}

	const MaterialRprxParamsU & GetURprxParams() const
	{
		return m_uRprxParams;
	}

	const MaterialRprxParamsTexture & GetTexRprxParams() const
	{
		return m_texRprx;
	}

	const MaterialTexture& GetDisplacementTexture() const
	{
	    return m_displacementTexture;
	}

private:
	void PoulateRprxColor(const MaterialParams & params);
	void PoulateRprColor(const MaterialParams & params);
	void PoulateEmissive(const MaterialParams & params);
	void PoulateTransparent(const MaterialParams & params);
	void PoulateUsdPreviewSurface(const MaterialParams & params, const MaterialTextures & textures);

	const EMaterialType m_type;
	MaterialRprParamsVec4f m_vec4fRprParams;

	MaterialRprxParamsVec4f m_vec4fRprxParams;
	MaterialRprxParamsU	m_uRprxParams;
	MaterialRprxParamsTexture m_texRprx;

    MaterialTexture m_displacementTexture;
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_ADAPTER_H