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
#include "pxr/imaging/rprUsd/materialNodes/rpr/materialXNode.h"

#include "renderParam.h"
#include "rprApi.h"

#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprMaterial::HdRprMaterial(SdfPath const& id) : HdMaterial(id) {

}

void HdRprMaterial::Sync(HdSceneDelegate* sceneDelegate,
                         HdRenderParam* renderParam,
                         HdDirtyBits* dirtyBits) {

    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    if (*dirtyBits & HdMaterial::DirtyResource) {
        if (m_rprMaterial) {
            rprApi->Release(m_rprMaterial);
            m_rprMaterial = nullptr;
        }

        VtValue vtMat = sceneDelegate->GetMaterialResource(GetId());
        if (vtMat.IsHolding<HdMaterialNetworkMap>()) {
            auto& networkMap = vtMat.UncheckedGet<HdMaterialNetworkMap>();
            m_rprMaterial = rprApi->CreateMaterial(sceneDelegate, networkMap);
        }

        if (!m_rprMaterial) {
            // Autodesk's Hydra Scene delegate may give us a mtlx file path directly,
            // to reuse existing material processing code, we create HdMaterialNetworkMap
            // that holds rpr_materialx_node
            //
            static TfToken materialXFilenameToken("MaterialXFilename", TfToken::Immortal);
            auto materialXFilename = sceneDelegate->Get(GetId(), materialXFilenameToken);
            if (materialXFilename.IsHolding<SdfAssetPath>()) {
                auto& mtlxAssetPath = materialXFilename.UncheckedGet<SdfAssetPath>();
                auto& mtlxPath = mtlxAssetPath.GetResolvedPath();
                if (!mtlxPath.empty()) {
                    HdMaterialNetwork network;
                    network.nodes.emplace_back();
                    HdMaterialNode& mtlxNode = network.nodes.back();
                    mtlxNode.identifier = RprUsdRprMaterialXNodeTokens->rpr_materialx_node;
                    mtlxNode.parameters.emplace(RprUsdRprMaterialXNodeTokens->file, materialXFilename);

                    // Use the same network for both surface and displacement terminals,
                    // RprUsdMaterialRegistry handles automatically shared nodes between terminal networks
                    //
                    HdMaterialNetworkMap networkMap;
                    networkMap.map[HdMaterialTerminalTokens->surface] = network;
                    networkMap.map[HdMaterialTerminalTokens->displacement] = network;
                    networkMap.terminals.push_back(mtlxNode.path);

                    m_rprMaterial = rprApi->CreateMaterial(sceneDelegate, networkMap);
                }
            }
        }

        rprRenderParam->MaterialDidChange(sceneDelegate, GetId());
    }

    *dirtyBits = Clean;
}

HdDirtyBits HdRprMaterial::GetInitialDirtyBitsMask() const {
    return HdMaterial::DirtyResource;
}

void HdRprMaterial::Reload() {
    // possibly we can use it to reload .mtlx definition if it's changed but I don't know when and how Reload is actually called
}

void HdRprMaterial::Finalize(HdRenderParam* renderParam) {
    static_cast<HdRprRenderParam*>(renderParam)->AcquireRprApiForEdit()->Release(m_rprMaterial);
    m_rprMaterial = nullptr;

    HdMaterial::Finalize(renderParam);
}

RprUsdMaterial const* HdRprMaterial::GetRprMaterialObject() const {
    return m_rprMaterial;
}

PXR_NAMESPACE_CLOSE_SCOPE
