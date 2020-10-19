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

#ifndef HDRPR_BASIS_CURVES_H
#define HDRPR_BASIS_CURVES_H

#include "baseRprim.h"

#include "pxr/imaging/hd/basisCurves.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/vec2f.h"

namespace rpr { class Curve; }

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;
class RprUsdMaterial;

class HdRprMaterial;

class HdRprBasisCurves : public HdRprBaseRprim<HdBasisCurves> {

public:
    HdRprBasisCurves(SdfPath const& id,
                     SdfPath const& instancerId);

    ~HdRprBasisCurves() override = default;

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
    rpr::Curve* CreateLinearRprCurve(HdRprApi* rprApi);
    rpr::Curve* CreateBezierRprCurve(HdRprApi* rprApi);

private:
    rpr::Curve* m_rprCurve = nullptr;
    RprUsdMaterial* m_fallbackMaterial = nullptr;

    HdBasisCurvesTopology m_topology;
    VtIntArray m_indices;
    VtFloatArray m_widths;
    HdInterpolation m_widthsInterpolation;
    VtVec2fArray m_uvs;
    HdInterpolation m_uvsInterpolation;
    VtVec3fArray m_points;
    GfMatrix4f m_transform;

    uint32_t m_visibilityMask;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_BASIS_CURVES_H
