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

#ifndef RIFCPP_FILTER_H
#define RIFCPP_FILTER_H

#include "rifContext.h"
#include "rifImage.h"

#include "pxr/imaging/rprUsd/boostIncludePath.h"
#include BOOST_INCLUDE_PATH(variant.hpp)

#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/vec2i.h"

#include <unordered_map>
#include <vector>
#include <map>

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApiFramebuffer;

namespace rif {

enum FilterInputType
{
    Color,
    Normal,
    LinearDepth,
    WorldCoordinate,
    ObjectId,
    Trans,
    Albedo,
    MaxInput
};

using FilterParam = BOOST_NS::variant<int, float, std::string, GfVec2i, GfMatrix4f, rif_image>;

enum class FilterType
{
    None = -1,
    AIDenoise,
    Resample,
    EawDenoise,
    FIRST = AIDenoise,
    LAST = EawDenoise
};

class Filter {
public:
    static std::unique_ptr<Filter> Create(FilterType type, Context* rifContext, std::uint32_t width, std::uint32_t height);
    static std::unique_ptr<Filter> CreateCustom(rif_image_filter_type type, Context* rifContext);

    virtual ~Filter();

    void SetInput(FilterInputType inputType, Filter* filter);
    void SetInput(FilterInputType inputType, rif_image rifImage, float sigma = 1.0f);
    void SetInput(FilterInputType inputType, HdRprApiFramebuffer* rprFrameBuffer, float sigma = 1.0f);
    void SetInput(const char* name, HdRprApiFramebuffer* rprFrameBuffer);
    void SetInput(const char* name, rif_image rifImage);
    void SetOutput(rif_image rifImage);
    void SetOutput(rif_image_desc imageDesc);
    void SetOutput(HdRprApiFramebuffer* rprFrameBuffer);
    void SetParam(const char* name, FilterParam param);
    void SetParamFilter(const char* name, Filter* filter);

    rif_image GetInput(FilterInputType inputType) const;
    rif_image GetOutput();

    virtual void Resize(std::uint32_t width, std::uint32_t height);
    void Update();
    void Resolve();

protected:
    Filter(Context* rifContext) : m_rifContext(rifContext) {}

    void DetachFilter();
    virtual void AttachFilter(rif_image inputImage);

    void ApplyParameters();

protected:
    Context* m_rifContext;
    rif_image_filter m_rifFilter = nullptr;

    std::vector<rif_image_filter> m_auxFilters;
    std::vector<std::unique_ptr<Image>> m_auxImages;

    std::unique_ptr<rif::Image> m_retainedOutputImage;

    struct InputTraits {
        rif_image rifImage;
        HdRprApiFramebuffer* rprFrameBuffer;
        float sigma;

        std::unique_ptr<rif::Image> retainedImage;

        InputTraits() : rifImage(nullptr), rprFrameBuffer(nullptr), sigma(0.0f) {}
        InputTraits(rif_image rifImage, float sigma) : rifImage(rifImage), rprFrameBuffer(nullptr), sigma(sigma) {}
        InputTraits(HdRprApiFramebuffer* rprFrameBuffer, Context* context, float sigma) : rprFrameBuffer(rprFrameBuffer), sigma(sigma) {
            retainedImage = context->CreateImage(rprFrameBuffer);
            rifImage = retainedImage->GetHandle();
        }
    };

    std::unordered_map<FilterInputType, InputTraits, std::hash<std::underlying_type<FilterInputType>::type>> m_inputs;
    std::map<std::string, InputTraits> m_namedInputs;
    std::unordered_map<std::string, FilterParam> m_params;

    rif_image m_outputImage = nullptr;

    enum ChangeTracker {
        Clean = 0,
        DirtyAll = ~0u,
        DirtyIOImage = 1 << 0,
        DirtyParameters = 1 << 2
    };
    uint32_t m_dirtyFlags = DirtyAll;

    bool m_isAttached = false;
};

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RIFCPP_FILTER_H
