#ifndef HDRPR_MATERIAL_FACTORY_H
#define HDRPR_MATERIAL_FACTORY_H

#include "pxr/pxr.h"
#include "materialAdapter.h"

#include <vector>

namespace rpr { class MaterialNode; class Image; class Shape; class Curve; }

PXR_NAMESPACE_OPEN_SCOPE

struct HdRprApiMaterial {
    rpr::MaterialNode* rootMaterial = nullptr;
    rpr::MaterialNode* twosidedNode = nullptr;
    rpr::MaterialNode* displacementMaterial = nullptr;
    std::vector<rpr::MaterialNode*> materialNodes;
    std::vector<std::shared_ptr<rpr::Image>> materialImages;
};

class ImageCache;

class RprMaterialFactory {
public:
    RprMaterialFactory(ImageCache* imageCache);

    HdRprApiMaterial* CreateMaterial(EMaterialType type, MaterialAdapter const& materialAdapter);
    void Release(HdRprApiMaterial* material);

    void AttachMaterial(rpr::Shape* mesh, HdRprApiMaterial const* material, bool doublesided, bool displacementEnabled);
    void AttachMaterial(rpr::Curve* mesh, HdRprApiMaterial const* material);

private:
    ImageCache* m_imageCache;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_FACTORY_H
