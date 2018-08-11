#include "sphereLight.h"
#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE
      

TF_DEFINE_PRIVATE_TOKENS(
	HdRprSphereLightTokens,
	(radius)
	(normalize)									
);


const TfTokenVector k_requiredGeometryParam =
{
	HdRprSphereLightTokens->radius,
};


bool HdRprSphereLight::IsDirtyGeomParam(std::map<TfToken, float> & params)
{
	if (params.find(HdRprSphereLightTokens->radius) == params.end())
	{
		return false;
	}

	float radius = params[HdRprSphereLightTokens->radius];

	bool isDirty = radius != m_radius;

	m_radius = radius;

	return isDirty;
}

const TfTokenVector & HdRprSphereLight::FetchLightGeometryParamNames() const
{
	return k_requiredGeometryParam;
}

RprApiObject HdRprSphereLight::CreateLightMesh(std::map<TfToken, float> & params)
{
	
	HdRprApiSharedPtr rprApi = m_rprApiWeakPrt.lock();
	if (!rprApi)
	{
		TF_CODING_ERROR("RprApi is expired");
		return nullptr;
	}

	return rprApi->CreateSphereLightMesh(params[HdRprSphereLightTokens->radius]);
}

GfVec3f HdRprSphereLight::NormalizeLightColor(const GfMatrix4d & transform, std::map<TfToken, float> & params, const GfVec3f & inColor)
{
	constexpr const double p = 1.6075;
	constexpr const double pinv = 1. / 1.6075;

	const double sx = GfVec3d(transform[0][0], transform[1][0], transform[2][0]).GetLength();
	const double sy = GfVec3d(transform[0][1], transform[1][1], transform[2][1]).GetLength();
	const double sz = GfVec3d(transform[0][2], transform[1][2], transform[2][2]).GetLength();

	return  inColor * static_cast<float>(3. / pow((pow(sx, p) * pow(sy, p) + pow(sx, p) * pow(sz, p) + pow(sy, p) * pow(sz, p)), pinv));
}

PXR_NAMESPACE_CLOSE_SCOPE
