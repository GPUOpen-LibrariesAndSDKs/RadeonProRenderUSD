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

#ifndef HDRPR_POINTS_H
#define HDRPR_POINTS_H

#include "pxr/imaging/hd/points.h"

#include "pxr/base/gf/matrix4f.h"

namespace rpr { class Shape; }

PXR_NAMESPACE_OPEN_SCOPE

class RprUsdMaterial;

class HdRprPoints : public HdPoints {
public:
    HdRprPoints(SdfPath const& id, SdfPath const& instancerId);

    ~HdRprPoints() override = default;

    void Sync(HdSceneDelegate* delegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits,
              TfToken const& reprSelector) override;

    void Finalize(HdRenderParam* renderParam) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

    void _InitRepr(TfToken const& reprName,
                   HdDirtyBits* dirtyBits) override;

private:
    rpr::Shape* m_prototypeMesh = nullptr;
    std::vector<rpr::Shape*> m_instances;
    RprUsdMaterial* m_material = nullptr;

    GfMatrix4f m_transform;

    VtVec3fArray m_points;

    VtVec3fArray m_colors;
    HdInterpolation m_colorsInterpolation;

    VtFloatArray m_widths;
    HdInterpolation m_widthsInterpolation;

    uint32_t m_visibilityMask;
    int m_subdivisionLevel;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_POINTS_H
