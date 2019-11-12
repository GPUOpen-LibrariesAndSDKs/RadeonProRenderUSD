#ifndef HDRPR_MATERIAL_FACTORY_H
#define HDRPR_MATERIAL_FACTORY_H

#include "pxr/pxr.h"
#include "materialAdapter.h"
#include "imageCache.h"

#include <RadeonProRender.h>

PXR_NAMESPACE_OPEN_SCOPE

struct RprApiMaterial {
    rpr_material_node rootMaterial;
    rpr_material_node displacementMaterial;
    std::vector<std::shared_ptr<rpr::Image>> materialImages;
    std::vector<rpr_material_node> materialNodes;
};

class RprMaterialFactory {
public:
    RprMaterialFactory(rpr_material_system matSys, ImageCache* imageCache);

    RprApiMaterial* CreateMaterial(EMaterialType type, const MaterialAdapter& materialAdapter);

    void DeleteMaterial(RprApiMaterial* rprmaterial);

    void AttachMaterialToShape(rpr_shape mesh, const RprApiMaterial* material);

    void AttachMaterialToCurve(rpr_shape mesh, const RprApiMaterial* material);

private:
    rpr_material_system m_matSys;
    ImageCache* m_imageCache;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_FACTORY_H
