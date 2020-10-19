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

#ifndef RPRUSD_MATERIAL_H
#define RPRUSD_MATERIAL_H

#include "pxr/imaging/rprUsd/api.h"
#include "pxr/base/vt/value.h"
#include "pxr/base/tf/token.h"

namespace rpr { class Shape; class Curve; class MaterialNode; }

PXR_NAMESPACE_OPEN_SCOPE

class RprUsdMaterial {
public:
    RPRUSD_API
    virtual ~RprUsdMaterial() = default;

    RPRUSD_API
    TfToken const& GetUvPrimvarName() const { return m_uvPrimvarName; }

    RPRUSD_API
    bool AttachTo(rpr::Shape* mesh, bool displacementEnabled) const;

    RPRUSD_API
    bool AttachTo(rpr::Curve* curve) const;

    RPRUSD_API
    static void DetachFrom(rpr::Shape* mesh);

    RPRUSD_API
    static void DetachFrom(rpr::Curve* curve);

    RPRUSD_API
    void SetName(const char* name);

protected:
    rpr::MaterialNode* m_surfaceNode = nullptr;
    rpr::MaterialNode* m_displacementNode = nullptr;
    rpr::MaterialNode* m_volumeNode = nullptr;
    bool m_isShadowCatcher = false;
    bool m_isReflectionCatcher = false;
    TfToken m_uvPrimvarName;
    VtValue m_displacementScale;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_H
