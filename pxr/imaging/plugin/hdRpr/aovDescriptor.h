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

#ifndef HDRPR_AOV_DESCRIPTOR_H
#define HDRPR_AOV_DESCRIPTOR_H

#include "pxr/base/tf/singleton.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/gf/vec4f.h"

#include "pxr/imaging/hd/types.h"

#include <RadeonProRender.hpp>

#include <map>

PXR_NAMESPACE_OPEN_SCOPE

#define HDRPR_AOV_TOKENS \
    (rawColor) \
    (albedo) \
    (variance) \
    (worldCoordinate) \
    (opacity) \
    ((primvarsSt, "primvars:st")) \
    (materialId) \
    (geometricNormal) \
    (objectGroupId) \
    (shadowCatcher) \
    (background) \
    (emission) \
    (velocity) \
    (directIllumination) \
    (indirectIllumination) \
    (ao) \
    (directDiffuse) \
    (directReflect) \
    (indirectDiffuse) \
    (indirectReflect) \
    (refract) \
    (volume) \
    (lightGroup0) \
    (lightGroup1) \
    (lightGroup2) \
    (lightGroup3) \
    (viewShadingNormal) \
    (reflectionCatcher) \
    (colorRight) \
    (lpe0) \
    (lpe1) \
    (lpe2) \
    (lpe3) \
    (lpe4) \
    (lpe5) \
    (lpe6) \
    (lpe7) \
    (lpe8) \
    (cameraNormal) \
    (cryptomatteMat0) \
    (cryptomatteMat1) \
    (cryptomatteMat2) \
    (cryptomatteObj0) \
    (cryptomatteObj1) \
    (cryptomatteObj2) \

TF_DECLARE_PUBLIC_TOKENS(HdRprAovTokens, HDRPR_AOV_TOKENS);

const rpr::Aov kAovNone = static_cast<rpr::Aov>(-1);

enum ComputedAovs {
    kNdcDepth = 0,
    kColorAlpha,
    kComputedAovsCount
};

struct HdRprAovDescriptor {
    uint32_t id;
    HdFormat format;
    bool multiSampled;
    bool computed;
    GfVec4f clearValue;

    HdRprAovDescriptor(uint32_t id = kAovNone, bool multiSampled = true, HdFormat format = HdFormatFloat32Vec4, GfVec4f clearValue = GfVec4f(0.0f), bool computed = false)
        : id(id), format(format), multiSampled(multiSampled), computed(computed), clearValue(clearValue) {

    }
};

class HdRprAovRegistry {
public:
    static HdRprAovRegistry& GetInstance() {
        return TfSingleton<HdRprAovRegistry>::GetInstance();
    }

    HdRprAovDescriptor const& GetAovDesc(TfToken const& name);
    HdRprAovDescriptor const& GetAovDesc(uint32_t id, bool computed);

    HdRprAovRegistry(HdRprAovRegistry const&) = delete;
    HdRprAovRegistry& operator=(HdRprAovRegistry const&) = delete;
    HdRprAovRegistry(HdRprAovRegistry&&) = delete;
    HdRprAovRegistry& operator=(HdRprAovRegistry&&) = delete;

private:
    HdRprAovRegistry();
    ~HdRprAovRegistry() = default;

    friend class TfSingleton<HdRprAovRegistry>;

private:
    struct AovNameLookupValue {
        uint32_t id;
        bool isComputed;

        AovNameLookupValue(uint32_t id, bool isComputed = false)
            : id(id), isComputed(isComputed) {

        }
    };
    std::map<TfToken, AovNameLookupValue> m_aovNameLookup;

    std::vector<HdRprAovDescriptor> m_aovDescriptors;
    std::vector<HdRprAovDescriptor> m_computedAovDescriptors;
};

TfToken const& HdRprGetCameraDepthAovName();

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_AOV_DESCRIPTOR_H
