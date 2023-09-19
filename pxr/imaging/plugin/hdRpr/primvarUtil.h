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

#ifndef HDRPR_PRIMVAR_UTIL_H
#define HDRPR_PRIMVAR_UTIL_H

#include "pxr/usd/sdf/path.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

void HdRprFillPrimvarDescsPerInterpolation(
    HdSceneDelegate* sceneDelegate, SdfPath const& id,
    std::map<HdInterpolation, HdPrimvarDescriptorVector>* primvarDescsPerInterpolation);

const HdPrimvarDescriptor* HdRprFindFirstPrimvarRole(
    std::map<HdInterpolation, HdPrimvarDescriptorVector> const& primvarDescsPerInterpolation,
    const std::string& role);

bool HdRprIsPrimvarExists(
    TfToken const& primvarName,
    std::map<HdInterpolation, HdPrimvarDescriptorVector> const& primvarDescsPerInterpolation,
    HdInterpolation* interpolation = nullptr);

bool HdRprIsValidPrimvarSize(
    size_t primvarSize,
    HdInterpolation primvarInterpolation,
    size_t uniformInterpolationSize,
    size_t vertexInterpolationSize);

struct HdRprGeometrySettings {
    int id = -1;
    int subdivisionLevel = 0;
    float subdivisionCreaseWeight = 0.0;
    uint32_t visibilityMask = 0;
    bool ignoreContour = false;
    std::string cryptomatteName;
    int numGeometrySamples = 1;
};

void HdRprParseGeometrySettings(
    HdSceneDelegate* sceneDelegate, SdfPath const& id,
    HdPrimvarDescriptorVector const& constantPrimvarDescs,
    HdRprGeometrySettings* geomSettings);

inline void HdRprParseGeometrySettings(
    HdSceneDelegate* sceneDelegate, SdfPath const& id,
    std::map<HdInterpolation, HdPrimvarDescriptorVector> const& primvarDescsPerInterpolation,
    HdRprGeometrySettings* geomSettings) {
    auto constantPrimvarDescIt = primvarDescsPerInterpolation.find(HdInterpolationConstant);
    if (constantPrimvarDescIt == primvarDescsPerInterpolation.end()) {
        return;
    }

    HdRprParseGeometrySettings(sceneDelegate, id, constantPrimvarDescIt->second, geomSettings);
}

template <typename T>
bool HdRprGetConstantPrimvar(TfToken const& name, HdSceneDelegate* sceneDelegate, SdfPath const& id, T* out_value) {
    auto value = sceneDelegate->Get(id, name);
    if (value.IsHolding<T>()) {
        *out_value = value.UncheckedGet<T>();
        return true;
    }

    auto typeName = value.GetTypeName();
    TF_WARN("[%s] %s: unexpected type. Expected %s but actual type is %s", id.GetText(), name.GetText(), typeid(T).name(), typeName.c_str());
    return false;
}

template <typename T>
bool HdRprSamplePrimvar(
    SdfPath const& id,
    TfToken const& key,
    HdSceneDelegate* sceneDelegate,
    size_t maxSampleCount,
    VtArray<T>* sampleValuesPtr) {
    std::vector<float> sampleTimes(maxSampleCount);
    std::vector<VtValue> sampleVtValues(maxSampleCount);

    size_t authoredSampleCount = sceneDelegate->SamplePrimvar(id, key, maxSampleCount, sampleTimes.data(), sampleVtValues.data());
    if (!authoredSampleCount) {
        return false;
    }

    if (authoredSampleCount < maxSampleCount) {
        sampleTimes.resize(authoredSampleCount);
        sampleVtValues.resize(authoredSampleCount);
    }

    if (sampleTimes.size() > 1) {
        float baselineTimeStep = sampleTimes[1] - sampleTimes[0];
        for (size_t i = 1; i < sampleTimes.size() - 1; ++i) {
            float timeStep = sampleTimes[i + 1] - sampleTimes[i];
            if (std::abs(baselineTimeStep - timeStep) > 1e-6f) {
                // Definitely an issue but we can at least use such data with the current API, so just log a warning
                TF_WARN("[%s] RPR does not support non-linear in time sub-frame primvar samples", id.GetText());
                break;
            }
        }
    }

    size_t baselineSize = 0;

    auto& sampleValues = *sampleValuesPtr;
    sampleValues.resize(sampleVtValues.size());
    for (size_t i = 0; i < sampleVtValues.size(); ++i) {
        if (sampleVtValues[i].IsHolding<T>()) {
            sampleValues[i] = sampleVtValues[i].UncheckedGet<T>();

            if (i == 0) {
                baselineSize = sampleValues[i].size();
            } else if (baselineSize != sampleValues[i].size()) {
                TF_RUNTIME_ERROR("[%s] RPR does not support non-uniform sub-frame samples - %s", id.GetText(), key.GetText());
                return false;
            }
        } else {
            TF_RUNTIME_ERROR("[%s] Failed to sample %s primvar data: unexpected underlying type - %s", id.GetText(), key.GetText(), sampleVtValues[i].GetTypeName().c_str());
            return false;
        }
    }

    return true;
}

template <typename T>
bool HdRprSamplePrimvar(
    SdfPath const& id,
    TfToken const& key,
    HdSceneDelegate* sceneDelegate,
    std::map<HdInterpolation, HdPrimvarDescriptorVector> const& primvarDescsPerInterpolation,
    size_t maxSampleCount,
    VtArray<T>* sampleValues,
    HdInterpolation* interpolation) {

    for (auto& primvarDescsEntry : primvarDescsPerInterpolation) {
        for (auto& pv : primvarDescsEntry.second) {
            if (pv.name == key) {
                if (!HdRprSamplePrimvar(id, key, sceneDelegate, maxSampleCount, sampleValues)) {
                    return false;
                }

                *interpolation = primvarDescsEntry.first;
                return true;
            }
        }
    }

    return false;
}

inline void HdRprGetPrimvarIndices(HdInterpolation interpolation, VtIntArray const& faceIndices, VtIntArray* out_indices) {
    out_indices->clear();
    if (interpolation == HdInterpolationFaceVarying) {
        out_indices->reserve(faceIndices.size());
        for (int i = 0; i < faceIndices.size(); ++i) {
            out_indices->push_back(i);
        }
    } else if (interpolation == HdInterpolationConstant) {
        *out_indices = VtIntArray(faceIndices.size(), 0);
    }
}

inline VtValue HdRpr_GetParam(HdSceneDelegate* sceneDelegate, SdfPath id, TfToken name) {
    // TODO: This is not Get() Because of the reasons listed here:
    // https://groups.google.com/g/usd-interest/c/k-N05Ac7SRk/m/RtK5HvglAQAJ
    // We may need to fix this in newer versions of USD

    // Order here is important
    // GetCameraParamValue works with deprecated schema and required work backward compatibility
    //
    // GetLightParamValue works with new schema, but if it wouldn't find any value
    // it would return default value (But real value might be stored in GetCameraParamValue)

    VtValue cameraValue = sceneDelegate->GetCameraParamValue(id, name);
    if (!cameraValue.IsEmpty()) {
        return cameraValue;
    }

    VtValue lightValue = sceneDelegate->GetLightParamValue(id, name);
    if (!lightValue.IsEmpty()) {
        return lightValue;
    }

    return VtValue();
}

template<typename T>
T HdRpr_GetParam(HdSceneDelegate* sceneDelegate, SdfPath id, TfToken name, T defaultValue) {
    return HdRpr_GetParam(sceneDelegate, id, name).GetWithDefault(defaultValue);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_PRIMVAR_UTIL_H
