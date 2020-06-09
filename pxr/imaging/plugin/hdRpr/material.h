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

#ifndef HDRPR_MATERIAL_H
#define HDRPR_MATERIAL_H

#include "pxr/imaging/hd/material.h"

PXR_NAMESPACE_OPEN_SCOPE

struct HdRprApiMaterial;

class HdRprMaterial final : public HdMaterial {
public:
    HdRprMaterial(SdfPath const& id);

    ~HdRprMaterial() override = default;

    void Sync(HdSceneDelegate* sceneDelegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Reload() override;
    void Finalize(HdRenderParam* renderParam) override;

    /// Get pointer to RPR material
    /// In case material сreation failure return nullptr
    HdRprApiMaterial const* GetRprMaterialObject() const;

    TfToken const& GetStName() const { return m_stName; }

private:
    HdRprApiMaterial* m_rprMaterial = nullptr;
    TfToken m_stName;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_H
