#include "sphereLight.h"
#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

const TfTokenVector k_requiredGeometryParam =
{
	HdLightTokens->radius,
};


bool HdRprSphereLight::IsDirtyGeomParam(std::map<TfToken, float> & params)
{
	if (params.find(HdLightTokens->radius) == params.end())
	{
		return false;
	}

	float radius = std::abs(params[HdLightTokens->radius]);

	bool isDirty = radius != m_radius;

	m_radius = radius;

	return isDirty;
}

const TfTokenVector & HdRprSphereLight::FetchLightGeometryParamNames() const
{
	return k_requiredGeometryParam;
}

RprApiObjectPtr HdRprSphereLight::CreateLightMesh(std::map<TfToken, float>& params)
{
	
	HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
	if (!rprApi)
	{
		TF_CODING_ERROR("RprApi is expired");
		return nullptr;
	}

	return rprApi->CreateSphereLightMesh(params[HdLightTokens->radius]);
}

GfVec3f HdRprSphereLight::NormalizeLightColor(const GfMatrix4d& transform, const GfVec3f& inColor) {
	const double sx = GfVec3d(transform[0][0], transform[1][0], transform[2][0]).GetLength() * m_radius;
	const double sy = GfVec3d(transform[0][1], transform[1][1], transform[2][1]).GetLength() * m_radius;
	const double sz = GfVec3d(transform[0][2], transform[1][2], transform[2][2]).GetLength() * m_radius;

	if (sx == 0. && sy == 0. && sz == 0.)
	{
		return inColor;
	}

	float scaleFactor = 1.0f;
	if (sx == sy && sy == sz)
	{
		// Can use the simple formula for surface area of a sphere
		scaleFactor = static_cast<float>(1. / (sx * sx));
	}
	else
	{
		// Approximating the area of a stretched ellipsoid using the Knud Thomsen formula:
		// http://www.numericana.com/answer/ellipsoid.htm
		constexpr const double p = 1.6075;
		constexpr const double pinv = 1. / 1.6075;
		double sx_p = pow(sx, p);
		double sy_p = pow(sy, p);
		double sz_p = pow(sz, p);

		scaleFactor = static_cast<float>(pow(3. / (sx_p * sy_p + sx_p * sz_p + sy_p * sz_p), pinv));
	}

	return inColor * scaleFactor;
}

PXR_NAMESPACE_CLOSE_SCOPE
