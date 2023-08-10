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

#include "primvarUtil.h"
#include "rprApi.h"

#include "pxr/imaging/rprUsd/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

void HdRprParseGeometrySettings(
    HdSceneDelegate* sceneDelegate, SdfPath const& id,
    HdPrimvarDescriptorVector const& constantPrimvarDescs,
    HdRprGeometrySettings* geomSettings) {

    auto setVisibilityFlag = [&sceneDelegate, &id, &geomSettings]
        (TfToken const& primvarName, HdRprVisibilityFlag flag) {
        bool toggle;
        if (!HdRprGetConstantPrimvar(primvarName, sceneDelegate, id, &toggle)) {
            return;
        }

        if (toggle) {
            geomSettings->visibilityMask |= flag;
        } else {
            geomSettings->visibilityMask &= ~flag;
        }
    };

    for (auto& desc : constantPrimvarDescs) {
        std::string primvarName = "primvars:" + desc.name.GetString();
        if (primvarName == RprUsdTokens->primvarsRprObjectId) {
            HdRprGetConstantPrimvar(desc.name, sceneDelegate, id, &geomSettings->id);
        } else if (primvarName == RprUsdTokens->primvarsRprMeshSubdivisionLevel) {
            int subdivisionLevel;
            if (HdRprGetConstantPrimvar(desc.name, sceneDelegate, id, &subdivisionLevel)) {
                geomSettings->subdivisionLevel = std::max(0, std::min(subdivisionLevel, 7));
            }
        } else if (primvarName == RprUsdTokens->primvarsRprMeshSubdivisionCreaseWeight) {
            HdRprGetConstantPrimvar(desc.name, sceneDelegate, id, &geomSettings->subdivisionCreaseWeight);
        } else if (primvarName == RprUsdTokens->primvarsRprMeshIgnoreContour) {
            HdRprGetConstantPrimvar(desc.name, sceneDelegate, id, &geomSettings->ignoreContour);
        } else if (primvarName == RprUsdTokens->primvarsRprObjectAssetName) {
            HdRprGetConstantPrimvar(desc.name, sceneDelegate, id, &geomSettings->cryptomatteName);
        } else if (primvarName == RprUsdTokens->primvarsRprObjectDeformSamples) {
            HdRprGetConstantPrimvar(desc.name, sceneDelegate, id, &geomSettings->numGeometrySamples);
            geomSettings->numGeometrySamples = std::max(1, geomSettings->numGeometrySamples);
        } else if (primvarName == RprUsdTokens->primvarsRprObjectVisibilityCamera) {
            setVisibilityFlag(desc.name, kVisiblePrimary);
        } else if (primvarName == RprUsdTokens->primvarsRprObjectVisibilityShadow) {
            setVisibilityFlag(desc.name, kVisibleShadow);
        } else if (primvarName == RprUsdTokens->primvarsRprObjectVisibilityReflection) {
            setVisibilityFlag(desc.name, kVisibleReflection);
        } else if (primvarName == RprUsdTokens->primvarsRprObjectVisibilityGlossyReflection) {
            setVisibilityFlag(desc.name, kVisibleGlossyReflection);
        } else if (primvarName == RprUsdTokens->primvarsRprObjectVisibilityRefraction) {
            setVisibilityFlag(desc.name, kVisibleRefraction);
        } else if (primvarName == RprUsdTokens->primvarsRprObjectVisibilityGlossyRefraction) {
            setVisibilityFlag(desc.name, kVisibleGlossyRefraction);
        } else if (primvarName == RprUsdTokens->primvarsRprObjectVisibilityDiffuse) {
            setVisibilityFlag(desc.name, kVisibleDiffuse);
        } else if (primvarName == RprUsdTokens->primvarsRprObjectVisibilityTransparent) {
            setVisibilityFlag(desc.name, kVisibleTransparent);
        }
    }
}

void HdRprFillPrimvarDescsPerInterpolation(
    HdSceneDelegate* sceneDelegate,
    SdfPath const& id,
    std::map<HdInterpolation, HdPrimvarDescriptorVector>* primvarDescsPerInterpolation) {
    if (!primvarDescsPerInterpolation->empty()) {
        return;
    }

    auto interpolations = {
        HdInterpolationConstant,
        HdInterpolationUniform,
        HdInterpolationVarying,
        HdInterpolationVertex,
        HdInterpolationFaceVarying,
        HdInterpolationInstance,
    };
    for (auto& interpolation : interpolations) {
        auto primvarDescs = sceneDelegate->GetPrimvarDescriptors(id, interpolation);
        if (!primvarDescs.empty()) {
            primvarDescsPerInterpolation->emplace(interpolation, std::move(primvarDescs));
        }
    }

    // If primitive has no primvars,
    // insert dummy entry so that the next time user calls this function,
    // it will not rerun sceneDelegate->GetPrimvarDescriptors which is quite costly
    if (primvarDescsPerInterpolation->empty()) {
        primvarDescsPerInterpolation->emplace(HdInterpolationCount, HdPrimvarDescriptorVector{});
    }
}

const HdPrimvarDescriptor* HdRprFindFirstPrimvarRole(
    std::map<HdInterpolation, HdPrimvarDescriptorVector> const& primvarDescsPerInterpolation,
    const std::string& role)
{
    for (const auto& primvarDescs : primvarDescsPerInterpolation) {
        for (const auto& primvar : primvarDescs.second) {
            if (primvar.role == role) {
                // Just take the first one.
                return &primvar;
            }
        }
    }
    return nullptr;
}

bool HdRprIsPrimvarExists(
    TfToken const& primvarName,
    std::map<HdInterpolation, HdPrimvarDescriptorVector> const& primvarDescsPerInterpolation,
    HdInterpolation* interpolation) {
    for (auto& entry : primvarDescsPerInterpolation) {
        for (auto& pv : entry.second) {
            if (pv.name == primvarName) {
                if (interpolation) {
                    *interpolation = entry.first;
                }
                return true;
            }
        }
    }
    return false;
}

bool HdRprIsValidPrimvarSize(size_t primvarSize, HdInterpolation primvarInterpolation, size_t uniformInterpSize, size_t vertexInterpSize) {
    switch (primvarInterpolation) {
    case HdInterpolationConstant:
        return primvarSize > 0;
    case HdInterpolationUniform:
        return primvarSize == uniformInterpSize;
    case HdInterpolationVertex:
        return primvarSize == vertexInterpSize;
    case HdInterpolationVarying:
        return true;
    default:
        return false;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
