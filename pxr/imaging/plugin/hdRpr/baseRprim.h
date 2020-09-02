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

#ifndef HDRPR_BASE_RPRIM_H
#define HDRPR_BASE_RPRIM_H

#include "renderParam.h"

#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

template <typename Base>
class HdRprBaseRprim : public Base {
public:
    HdRprBaseRprim(
        SdfPath const& id,
        SdfPath const& instancerId)
        : Base(id, instancerId) {

    }
    ~HdRprBaseRprim() override = default;

    void Finalize(HdRenderParam* renderParam) override {
        if (!m_materialId.IsEmpty()) {
            auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
            rprRenderParam->UnsubscribeFromMaterialUpdates(m_materialId, Base::GetId());
        }

        Base::Finalize(renderParam);
    }

protected:
    void UpdateMaterialId(HdSceneDelegate* sceneDelegate, HdRprRenderParam* renderParam) {
        auto newMaterialId = sceneDelegate->GetMaterialId(Base::GetId());
        if (m_materialId != newMaterialId) {
            if (!m_materialId.IsEmpty()) {
                renderParam->UnsubscribeFromMaterialUpdates(m_materialId, Base::GetId());
            }
            renderParam->SubscribeForMaterialUpdates(newMaterialId, Base::GetId());

            m_materialId = newMaterialId;
        }
    }

protected:
    SdfPath m_materialId;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_BASE_RPRIM_H
