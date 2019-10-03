#include "lightBase.h"
#include "material.h"
#include "materialFactory.h"

#include "pxr/imaging/hd/sceneDelegate.h"
// for color temperature
#include "pxr/usd/usdLux/blackbody.h"

PXR_NAMESPACE_OPEN_SCOPE

static float computeLightIntensity(float intensity, float exposure)
{
	return intensity * exp2(exposure);
}

bool HdRprLightBase::IsDirtyMaterial(const GfVec3f & emissionColor)
{
	bool isDirty = (m_emmisionColor != emissionColor);
	m_emmisionColor = emissionColor;
	return isDirty;
}

RprApiObjectPtr HdRprLightBase::CreateLightMaterial(const GfVec3f & illumColor)
{
	HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
	if (!rprApi)
	{
		TF_CODING_ERROR("RprApi is expired");
		return nullptr;
	}

	MaterialAdapter matAdapter = MaterialAdapter(EMaterialType::EMISSIVE,
		MaterialParams{ { HdLightTokens->color, VtValue(illumColor) } });

	return rprApi->CreateMaterial(matAdapter);
}

void HdRprLightBase::Sync(HdSceneDelegate *sceneDelegate,
	HdRenderParam   *renderParam,
	HdDirtyBits     *dirtyBits)
{
	SdfPath const & id = GetId();

	HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
	if (!rprApi)
	{
		TF_CODING_ERROR("RprApi is expired");
		return;
	}

	HdDirtyBits bits = *dirtyBits;

	SdfPath path = GetId();

	if (bits & DirtyTransform) {
		VtValue transformValue = sceneDelegate->Get(id, HdLightTokens->transform);
		m_transform = (transformValue.IsHolding<GfMatrix4d>()) ? transformValue.Get<GfMatrix4d>() : GfMatrix4d(1);
	}

	if (bits & DirtyParams)
	{
		// Get the color of the light
		GfVec3f color = sceneDelegate->GetLightParamValue(id,HdPrimvarRoleTokens->color).Get<GfVec3f>();

		// Extract intensity
		const float intensity = sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity).Get<float>();

		// Extract the exposure of the light
		const float exposure = sceneDelegate->GetLightParamValue(id, HdLightTokens->exposure).Get<float>();

		// Get the colorTemerature of the light
		if(sceneDelegate->GetLightParamValue(id, HdLightTokens->enableColorTemperature).Get<bool>()) {
			GfVec3f temperatureColor = UsdLuxBlackbodyTemperatureAsRgb(sceneDelegate->GetLightParamValue(id, HdLightTokens->colorTemperature).Get<float>());
			color[0] *= temperatureColor[0];
			color[1] *= temperatureColor[1];
			color[2] *= temperatureColor[2];
		}

		// Compute
		const float illuminationIntensity = computeLightIntensity(intensity, exposure);

		const TfTokenVector& paramNames = FetchLightGeometryParamNames();

		std::map<TfToken, float> params;
		for (const TfToken& paramName : paramNames) {
			params[paramName] = sceneDelegate->GetLightParamValue(id, paramName).Get<float>();
		}


		if (!m_lightMesh || this->IsDirtyGeomParam(params))
		{
			m_lightMesh = CreateLightMesh(params);
		}

		if (!m_lightMesh)
		{
			TF_CODING_ERROR("Light Mesh was not created");
			return;
		}

		const bool isNormalize = sceneDelegate->GetLightParamValue(id, HdLightTokens->normalize).Get<bool>();
		const GfVec3f illumColor = color * illuminationIntensity;
		const GfVec3f emissionColor = (isNormalize) ? NormalizeLightColor(m_transform, params, illumColor) : illumColor;

		if (!m_lightMaterial || IsDirtyMaterial(emissionColor))
		{
			m_lightMaterial = CreateLightMaterial(emissionColor);
		}

		if (!m_lightMaterial) {
			TF_CODING_ERROR("Light material was not created");
			return;
		}

		rprApi->SetMeshMaterial(m_lightMesh.get(), m_lightMaterial.get());
	}

	if ((bits & DirtyTransform) && m_lightMesh) {
		rprApi->SetMeshTransform(m_lightMesh.get(), m_transform);
	}

	*dirtyBits = DirtyBits::Clean;
}


HdDirtyBits HdRprLightBase::GetInitialDirtyBitsMask() const {
	return DirtyBits::AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE
