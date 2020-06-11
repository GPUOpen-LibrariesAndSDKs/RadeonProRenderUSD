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

#include "material.h"
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

static bool GetMaterialNetwork(
    TfToken const& terminal, HdSceneDelegate* delegate, HdMaterialNetworkMap const& networkMap, HdRprRenderParam const& renderParam,
    EMaterialType* out_materialType, HdMaterialNetwork const** out_network) {
    auto mapIt = networkMap.map.find(terminal);
    if (mapIt == networkMap.map.end()) {
        return false;
    }

    auto& network = mapIt->second;
    if (network.nodes.empty()) {
        return false;
    }

    *out_network = &network;

    for (auto& node : network.nodes) {
        if (node.identifier == HdRprMaterialTokens->UsdPreviewSurface) {
            *out_materialType = EMaterialType::USD_PREVIEW_SURFACE;
            return true;
        } else {
            if (renderParam.GetMaterialNetworkSelector() == HdRprMaterialNetworkSelectorTokens->karma) {
                auto implementationSource = delegate->Get(node.path, _tokens->infoImplementationSource);
                if (implementationSource.IsHolding<TfToken>() &&
                    implementationSource.UncheckedGet<TfToken>() == _tokens->sourceAsset) {
                    auto nodeAsset = delegate->Get(node.path, _tokens->infoSourceAsset);
                    if (nodeAsset.IsHolding<SdfAssetPath>()) {
                        auto& asset = nodeAsset.UncheckedGet<SdfAssetPath>();
                        if (!asset.GetAssetPath().empty()) {
                            static const std::string kPrincipledShaderDef = "opdef:/Vop/principledshader::2.0";
                            if (asset.GetAssetPath().compare(0, kPrincipledShaderDef.size(), kPrincipledShaderDef.c_str())) {
                                return false;
                            }

                            *out_materialType = EMaterialType::HOUDINI_PRINCIPLED_SHADER;
                            return true;
                        }
                    }
                }
            }
        }
    }

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
            auto& networkMap = vtMat.UncheckedGet<HdMaterialNetworkMap>();

            EMaterialType surfaceType = EMaterialType::NONE;
            HdMaterialNetwork const* surface = nullptr;

            EMaterialType displacementType = EMaterialType::NONE;
            HdMaterialNetwork const* displacement = nullptr;

            if (GetMaterialNetwork(HdMaterialTerminalTokens->surface, sceneDelegate, networkMap, *rprRenderParam, &surfaceType, &surface)) {
                if (GetMaterialNetwork(HdMaterialTerminalTokens->displacement, sceneDelegate, networkMap, *rprRenderParam, &displacementType, &displacement)) {
                    if (displacementType != surfaceType) {
                        displacement = nullptr;
                    }
                }

                MaterialAdapter matAdapter(surfaceType, *surface, displacement ? *displacement : HdMaterialNetwork{});
                m_stName = matAdapter.GetStName();
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
