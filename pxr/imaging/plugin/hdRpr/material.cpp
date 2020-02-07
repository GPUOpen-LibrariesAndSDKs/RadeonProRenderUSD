#include "material.h"
#include "materialFactory.h"
#include "materialAdapter.h"

#include "renderParam.h"
#include "rprApi.h"

#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((infoSourceAsset, "info:sourceAsset")) \
    ((infoImplementationSource, "info:implementationSource")) \
    (sourceAsset)
);

static bool GetMaterial(HdSceneDelegate* delegate, HdMaterialNetworkMap const& networkMap, HdRprRenderParam* renderParam, EMaterialType& out_materialType, HdMaterialNetwork& out_surface) {
    out_materialType = EMaterialType::NONE;

    for (const auto& networkIt : networkMap.map) {
        if (networkIt.first != HdMaterialTerminalTokens->surface) {
            continue;
        }

        auto& network = networkIt.second;
        if (network.nodes.empty()) {
            continue;
        }

        for (auto& node : network.nodes) {
            if (node.identifier == HdRprMaterialTokens->UsdPreviewSurface) {
                out_surface = network;
                out_materialType = EMaterialType::USD_PREVIEW_SURFACE;
            } else {
                if (renderParam->GetMaterialNetworkSelector() == HdRprMaterialNetworkSelectorTokens->karma) {
                    auto implementationSource = delegate->Get(node.path, _tokens->infoImplementationSource);
                    if (implementationSource.IsHolding<TfToken>() &&
                        implementationSource.UncheckedGet<TfToken>() == _tokens->sourceAsset) {
                        auto nodeAsset = delegate->Get(node.path, _tokens->infoSourceAsset);
                        if (nodeAsset.IsHolding<SdfAssetPath>()) {
                            auto& asset = nodeAsset.UncheckedGet<SdfAssetPath>();
                            if (!asset.GetAssetPath().empty()) {
                                std::string principledShaderDef("opdef:/Vop/principledshader::2.0");
                                if (asset.GetAssetPath().compare(0, principledShaderDef.size(), principledShaderDef.c_str())) {
                                    return false;
                                }

                                out_surface = network;
                                out_materialType = EMaterialType::HOUDINI_PRINCIPLED_SHADER;
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    return out_materialType != EMaterialType::NONE;
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

            if (GetMaterial(sceneDelegate, networkMap, rprRenderParam, materialType, surface)) {
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
    // Stop render thread to safely release resources
    static_cast<HdRprRenderParam*>(renderParam)->GetRenderThread()->StopRender();

    HdMaterial::Finalize(renderParam);
}

RprApiObject const* HdRprMaterial::GetRprMaterialObject() const {
    return m_rprMaterial.get();
}

PXR_NAMESPACE_CLOSE_SCOPE
