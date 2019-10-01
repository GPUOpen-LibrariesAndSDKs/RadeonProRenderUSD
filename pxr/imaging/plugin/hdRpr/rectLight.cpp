#include "rectLight.h"

PXR_NAMESPACE_OPEN_SCOPE
	  

const TfTokenVector k_requiredGeometryParam = {
	HdLightTokens->width,
	HdLightTokens->height,
};

const TfTokenVector& HdRprRectLight::FetchLightGeometryParamNames() const {
	return k_requiredGeometryParam;
}

bool HdRprRectLight::IsDirtyGeomParam(std::map<TfToken, float>& params) {
	if (params.find(HdLightTokens->width) == params.end() || params.find(HdLightTokens->height) == params.end()) {
		return false;
	}

	float width = params[HdLightTokens->width];
	float height = params[HdLightTokens->height];

	bool isDirty = (width != m_width || m_height != height);

	m_width = width;
	m_height = height;

	return isDirty;
}

RprApiObjectPtr HdRprRectLight::CreateLightMesh(std::map<TfToken, float>& params) {
	HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
	if (!rprApi) {
		TF_CODING_ERROR("RprApi is expired");
		return nullptr;
	}
	return rprApi->CreateRectLightMesh(params[HdLightTokens->width], params[HdLightTokens->height]);
}

GfVec3f HdRprRectLight::NormalizeLightColor(const GfMatrix4d& transform, std::map<TfToken, float>& params, const GfVec3f& inColor) {
	const double width = static_cast<double>(params[HdLightTokens->width]);
	const double height = static_cast<double>(params[HdLightTokens->height]);

	const GfVec4d ox(width, 0., 0., 0.);
	const GfVec4d oy(0., height, 0., 0.);

	const GfVec4d oxTrans = ox * transform;
	const GfVec4d oyTrans = oy * transform;

	return static_cast<float>(1. / (oxTrans.GetLength() * oyTrans.GetLength())) * inColor;
}

PXR_NAMESPACE_CLOSE_SCOPE
