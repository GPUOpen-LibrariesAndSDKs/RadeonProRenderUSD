#ifndef HDRPR_MATERIAL_FACTORY_H
#define HDRPR_MATERIAL_FACTORY_H

#include "pxr/pxr.h"

#include "RadeonProRender.h"

#include "materialAdapter.h"

PXR_NAMESPACE_OPEN_SCOPE

struct RprApiMaterial {
    rpr_material_node rootMaterial;
    rpr_material_node displacementMaterial;
	std::vector<rpr_image> materialImages;
	std::vector<rpr_material_node> materialNodes;
};

class RprMaterialFactory
{
public:
    RprMaterialFactory(rpr_material_system matSys, rpr_context rprContext);

	RprApiMaterial* CreateMaterial(EMaterialType type, const MaterialAdapter & materialAdapter);

	void DeleteMaterial(RprApiMaterial * rprmaterial);

	void AttachMaterialToShape(rpr_shape mesh, const RprApiMaterial* material);

	void AttachCurveToShape(rpr_shape mesh, const RprApiMaterial* material);

private:
    rpr_material_system m_matSys;
    rpr_context m_context;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_FACTORY_H