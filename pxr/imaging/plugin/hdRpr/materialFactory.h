/************************************************************************
Copyright 2020 Advanced Micro Devices, Inc
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
************************************************************************/

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
    std::vector<rpr::ContextObject*> auxiliaryObjects;
    std::vector<std::shared_ptr<rpr::Image>> materialImages;
};

class ImageCache;

class RprMaterialFactory {
public:
    RprMaterialFactory(ImageCache* imageCache);

    HdRprApiMaterial* CreateMaterial(EMaterialType type, MaterialAdapter const& materialAdapter);
    HdRprApiMaterial* CreatePointsMaterial(VtVec3fArray const& colors);
    void Release(HdRprApiMaterial* material);

    void AttachMaterial(rpr::Shape* mesh, HdRprApiMaterial const* material, bool doublesided, bool displacementEnabled);
    void AttachMaterial(rpr::Curve* mesh, HdRprApiMaterial const* material);

private:
    ImageCache* m_imageCache;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_FACTORY_H
