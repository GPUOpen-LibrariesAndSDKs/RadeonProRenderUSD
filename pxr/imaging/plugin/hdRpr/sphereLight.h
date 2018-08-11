#ifndef HDRPR_SPHERE_LIGHT_H
#define HDRPR_SPHERE_LIGHT_H

#include "lightBase.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprSphereLight : public HdRprLightBase {
	
public:
	HdRprSphereLight(SdfPath const & id, HdRprApiSharedPtr rprApi)
		: HdRprLightBase(id, rprApi) {}

protected:

	virtual bool IsDirtyGeomParam(std::map<TfToken, float> & params);

	// Ferch required params for geometry
	virtual const TfTokenVector & FetchLightGeometryParamNames() const override;

	// Create mesh with emmisive material
	virtual RprApiObject CreateLightMesh(std::map<TfToken, float> & params) override;

	// Normalize Light Color with surface area
	virtual GfVec3f NormalizeLightColor(const GfMatrix4d & transform, std::map<TfToken, float> & params, const GfVec3f & emmisionColor) override;

	float m_radius = std::numeric_limits<float>::quiet_NaN();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_SPHERE_LIGHT_H
