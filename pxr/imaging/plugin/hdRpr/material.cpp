#include "material.h"
#include "materialAdapter.h"

#include "renderParam.h"
#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

static bool GetMaterial(const HdMaterialNetworkMap& networkMap, EMaterialType& out_materialType, HdMaterialNetwork& out_surface) {
    for (const auto& networkIt : networkMap.map) {
        const HdMaterialNetwork& network = networkIt.second;

        if (network.nodes.empty()) {
            continue;
        }

        for (const HdMaterialNode& node : network.nodes) {
            if (node.identifier == HdRprMaterialTokens->UsdPreviewSurface) {
                out_surface = network;
                out_materialType = EMaterialType::USD_PREVIEW_SURFACE;

                return true;
            }
        }
    }

    out_materialType = EMaterialType::NONE;
    return false;
}

HdRprMaterial::HdRprMaterial(SdfPath const& id) : HdMaterial(id) {

}

void HdRprMaterial::Sync(HdSceneDelegate* sceneDelegate,
                         HdRenderParam* renderParam,
                         HdDirtyBits* dirtyBits) {

    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    if (*dirtyBits & HdMaterial::DirtyResource) {
        VtValue vtMat = sceneDelegate->GetMaterialResource(GetId());
        if (vtMat.IsHolding<HdMaterialNetworkMap>()) {

            HdMaterialNetworkMap networkMap = vtMat.UncheckedGet<HdMaterialNetworkMap>();

            EMaterialType materialType;
            HdMaterialNetwork surface;

            if (GetMaterial(networkMap, materialType, surface)) {
                MaterialAdapter matAdapter = MaterialAdapter(materialType, surface);
                m_rprMaterial = rprApi->CreateMaterial(matAdapter);
            } else {
                TF_CODING_WARNING("Material type not supported");
            }
        }
    }

    *dirtyBits = Clean;
}

HdDirtyBits HdRprMaterial::GetInitialDirtyBitsMask() const {
    return HdMaterial::DirtyResource;
}

void HdRprMaterial::Reload() {
    // no-op
}

void HdRprMaterial::Finalize(HdRenderParam* renderParam) {
    static_cast<HdRprRenderParam*>(renderParam)->AcquireRprApiForEdit()->Release(m_rprMaterial);
    m_rprMaterial = nullptr;

    HdMaterial::Finalize(renderParam);
}

HdRprApiMaterial const* HdRprMaterial::GetRprMaterialObject() const {
    return m_rprMaterial;
}

PXR_NAMESPACE_CLOSE_SCOPE
