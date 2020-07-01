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

#include "renderParam.h"
#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

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
            m_rprMaterial = rprApi->CreateMaterial(sceneDelegate, networkMap);
        }
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
