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

#include "rifFilter.h"
#include "rifError.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

namespace {

class FilterAIDenoise final : public Filter {
    enum {
        RemapDepthFilter,
        RemapNormalFilter,
        AuxFilterMax
    };
    enum {
        RemappedDepthImage,
        RemappedNormalImage,
        AuxImageMax
    };
public:
    explicit FilterAIDenoise(Context* rifContext, std::uint32_t width, std::uint32_t height);
    ~FilterAIDenoise() override = default;

    void AttachFilter(rif_image inputImage) override;
};

class FilterEaw final : public Filter {
    enum {
        ColorVar,
        Mlaa,
        AuxFilterMax
    };

    enum {
        ColorVarianceImage,
        DenoisedOutputImage,
        AuxImageMax
    };

public:
    explicit FilterEaw(Context* rifContext, std::uint32_t width, std::uint32_t height);
    ~FilterEaw() override = default;

    void AttachFilter(rif_image inputImage) override;
};

class FilterResample final : public Filter {
public:
    explicit FilterResample(Context* rifContext, std::uint32_t width, std::uint32_t height);
    ~FilterResample() override = default;

    void Resize(std::uint32_t width, std::uint32_t height) override;
};

class FilterCustom final : public Filter {
public:
    explicit FilterCustom(Context* rifContext, rif_image_filter_type type) : Filter(rifContext) {
        m_rifFilter = rifContext->CreateImageFilter(type);
    }
    ~FilterCustom() override = default;
};

FilterAIDenoise::FilterAIDenoise(Context* rifContext, std::uint32_t width, std::uint32_t height) : Filter(rifContext) {
    m_rifFilter = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_AI_DENOISE);

    // setup const parameters
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1u(m_rifFilter, "useHDR", 1), "Failed to set filter \"usdHDR\" parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterString(m_rifFilter, "modelPath", rifContext->GetModelPath().c_str()), "Failed to set filter \"modelPath\" parameter");

    // auxillary filters
    m_auxFilters.resize(AuxFilterMax, nullptr);
    m_auxFilters[RemapDepthFilter] = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_REMAP_RANGE);
    m_auxFilters[RemapNormalFilter] = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_REMAP_RANGE);

    // setup remapping filters
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_auxFilters[RemapDepthFilter], "dstLo", 0.0f), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_auxFilters[RemapDepthFilter], "dstHi", 1.0f), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_auxFilters[RemapNormalFilter], "dstLo", 0.0f), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_auxFilters[RemapNormalFilter], "dstHi", 1.0f), "Failed to set filter parameter");

    // auxillary rif images
    auto desc = Image::GetDesc(width, height, HdFormatFloat32Vec4);

    m_auxImages.resize(AuxImageMax);
    m_auxImages[RemappedDepthImage] = rifContext->CreateImage(desc);
    m_auxImages[RemappedNormalImage] = rifContext->CreateImage(desc);
}

void FilterAIDenoise::AttachFilter(rif_image inputImage) {
    // setup inputs
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "normalsImg", m_auxImages[RemappedNormalImage]->GetHandle()), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "depthImg", m_auxImages[RemappedDepthImage]->GetHandle()), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "colorImg", m_inputs.at(Color).rifImage), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "albedoImg", m_inputs.at(Albedo).rifImage), "Failed to set filter parameter");

    m_rifContext->AttachFilter(m_auxFilters[RemapDepthFilter], m_inputs.at(LinearDepth).rifImage, m_auxImages[RemappedDepthImage]->GetHandle());
    m_rifContext->AttachFilter(m_auxFilters[RemapNormalFilter], m_inputs.at(Normal).rifImage, m_auxImages[RemappedNormalImage]->GetHandle());

    Filter::AttachFilter(inputImage);
}

FilterEaw::FilterEaw(Context* rifContext, std::uint32_t width, std::uint32_t height) : Filter(rifContext) {
    // main EAW filter
    m_rifFilter = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_EAW_DENOISE);

    // auxillary EAW filters
    m_auxFilters.resize(AuxFilterMax, nullptr);
    m_auxFilters[ColorVar] = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_TEMPORAL_ACCUMULATOR);
    m_auxFilters[Mlaa] = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_MLAA);

    // auxillary rif images
    auto desc = Image::GetDesc(width, height, HdFormatFloat32Vec4);

    m_auxImages.resize(AuxImageMax);
    m_auxImages[ColorVarianceImage] = rifContext->CreateImage(desc);
    m_auxImages[DenoisedOutputImage] = rifContext->CreateImage(desc);
}

void FilterEaw::AttachFilter(rif_image inputImage) {
    // setup params
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "normalsImg", m_inputs.at(Normal).rifImage), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "transImg", m_inputs.at(ObjectId).rifImage), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "colorVar", m_inputs.at(Color).rifImage), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_rifFilter, "colorSigma", m_inputs.at(Color).sigma), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_rifFilter, "normalSigma", m_inputs.at(Normal).sigma), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_rifFilter, "depthSigma", m_inputs.at(LinearDepth).sigma), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_rifFilter, "transSigma", m_inputs.at(ObjectId).sigma), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[Mlaa], "normalsImg", m_inputs.at(Normal).rifImage), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[Mlaa], "meshIDImg", m_inputs.at(ObjectId).rifImage), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[ColorVar], "positionsImg", m_inputs.at(WorldCoordinate).rifImage), "Failed to set variance filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[ColorVar], "normalsImg", m_inputs.at(Normal).rifImage), "Failed to set variance filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[ColorVar], "meshIdsImg", m_inputs.at(ObjectId).rifImage), "Failed to set variance filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[ColorVar], "outVarianceImg", m_auxImages[ColorVarianceImage]->GetHandle()), "Failed to set variance filter parameter");

    m_rifContext->AttachFilter(m_auxFilters[ColorVar], inputImage, m_outputImage);
    m_rifContext->AttachFilter(m_rifFilter, m_outputImage, m_auxImages[DenoisedOutputImage]->GetHandle());
    m_rifContext->AttachFilter(m_auxFilters[Mlaa], m_auxImages[DenoisedOutputImage]->GetHandle(), m_outputImage);
}

FilterResample::FilterResample(Context* rifContext, std::uint32_t width, std::uint32_t height) : Filter(rifContext) {
    m_rifFilter = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_RESAMPLE);

    // setup const parameters
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1u(m_rifFilter, "interpOperator", RIF_IMAGE_INTERPOLATION_NEAREST), "Failed to set parameter of resample filter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter2u(m_rifFilter, "outSize", width, height), "Failed to set parameter of resample filter");
}

void FilterResample::Resize(std::uint32_t width, std::uint32_t height) {
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter2u(m_rifFilter, "outSize", width, height), "Failed to set parameter of resample filter");
    Filter::Resize(width, height);
}

} // namespace anonymous

std::unique_ptr<Filter> Filter::Create(FilterType type, Context* rifContext, std::uint32_t width, std::uint32_t height) {
    if (!width || !height) {
        return nullptr;
    }

    switch (type) {
        case FilterType::AIDenoise:
            return std::unique_ptr<FilterAIDenoise>(new FilterAIDenoise(rifContext, width, height));
        case FilterType::EawDenoise:
            return std::unique_ptr<FilterEaw>(new FilterEaw(rifContext, width, height));
        case FilterType::Resample:
            return std::unique_ptr<FilterResample>(new FilterResample(rifContext, width, height));
        default:
            return nullptr;
    }
}
std::unique_ptr<Filter> Filter::CreateCustom(rif_image_filter_type type, Context* rifContext) {
    if (!rifContext) {
        return nullptr;
    }

    return std::unique_ptr<FilterCustom>(new FilterCustom(rifContext, type));
}

Filter::~Filter() {
    DetachFilter();

    rif_int rifStatus = RIF_SUCCESS;

    for (const rif_image_filter& auxFilter : m_auxFilters) {
        rifStatus = rifObjectDelete(auxFilter);
        assert(RIF_SUCCESS == rifStatus);
    }

    if (m_rifFilter != nullptr) {
        rifStatus = rifObjectDelete(m_rifFilter);
        assert(RIF_SUCCESS == rifStatus);
    }
}

void Filter::SetInput(FilterInputType inputType, Filter* filter) {
    m_inputs[inputType] = InputTraits(rif_image(filter->m_rifFilter), 1.0f);
    m_dirtyFlags |= DirtyIOImage;
}

void Filter::SetInput(FilterInputType inputType, rif_image rifImage, const float sigma) {
    assert(rifImage);

    m_inputs[inputType] = InputTraits(rifImage, sigma);
    m_dirtyFlags |= DirtyIOImage;
}

void Filter::SetInput(FilterInputType inputType, HdRprApiFramebuffer* rprFrameBuffer, const float sigma) {
    assert(rprFrameBuffer);

    m_inputs[inputType] = InputTraits(rprFrameBuffer, m_rifContext, sigma);
    m_dirtyFlags |= DirtyIOImage;
}

void Filter::SetInput(const char* name, HdRprApiFramebuffer* rprFrameBuffer) {
    m_namedInputs[name] = InputTraits(rprFrameBuffer, m_rifContext, 1.0f);
    m_dirtyFlags |= DirtyParameters;
}

void Filter::SetInput(const char* name, rif_image rifImage) {
    m_namedInputs[name] = InputTraits(rifImage, 1.0f);
    m_dirtyFlags |= DirtyParameters;
}

void Filter::SetOutput(rif_image_desc imageDesc) {
    m_retainedOutputImage = m_rifContext->CreateImage(imageDesc);
    m_outputImage = m_retainedOutputImage->GetHandle();
    m_dirtyFlags |= DirtyIOImage;
}

void Filter::SetOutput(rif_image rifImage) {
    m_retainedOutputImage = nullptr;
    m_outputImage = rifImage;
    m_dirtyFlags |= DirtyIOImage;
}

void Filter::SetOutput(HdRprApiFramebuffer* rprFrameBuffer) {
    m_retainedOutputImage = m_rifContext->CreateImage(rprFrameBuffer);
    m_outputImage = m_retainedOutputImage->GetHandle();
    m_dirtyFlags |= DirtyIOImage;
}

rif_image Filter::GetInput(FilterInputType inputType) const {
    auto inputIter = m_inputs.find(inputType);
    if (inputIter != m_inputs.end()) {
        return inputIter->second.rifImage;
    }

    return nullptr;
}

rif_image Filter::GetOutput() {
    return m_outputImage;
}

void Filter::SetParam(const char* name, FilterParam param) {
    m_params[name] = param;
    m_dirtyFlags |= DirtyParameters;
}

void Filter::SetParamFilter(const char* name, Filter* filter) {
    if (filter) {
        SetParam(name, rif_image(filter->m_rifFilter));
    }
}

void Filter::Resize(std::uint32_t width, std::uint32_t height) {
    auto desc = Image::GetDesc(width, height, HdFormatFloat32Vec4);
    for (auto& image : m_auxImages) {
        if (image) {
            image = m_rifContext->CreateImage(desc);
        }
    }
}

template <typename Iter>
void UpdateInputs(Iter begin, Iter end, Context* context) {
    for (; begin != end; ++begin) {
        if (begin->second.rprFrameBuffer) {
            context->UpdateInputImage(begin->second.rprFrameBuffer, begin->second.rifImage);
        }
    }
}

void Filter::Update() {
    if (m_dirtyFlags & DirtyParameters) {
        ApplyParameters();
    }
    if (m_dirtyFlags & DirtyIOImage) {
        DetachFilter();
        if (m_outputImage) {
            AttachFilter(GetInput(Color));
            m_isAttached = true;
        }
    }

    m_dirtyFlags = Clean;
}

void Filter::Resolve() {
    UpdateInputs(m_inputs.begin(), m_inputs.end(), m_rifContext);
    UpdateInputs(m_namedInputs.begin(), m_namedInputs.end(), m_rifContext);
}

void Filter::AttachFilter(rif_image inputImage) {
    m_rifContext->AttachFilter(m_rifFilter, inputImage, m_outputImage);
}

void Filter::DetachFilter() {
    if (!m_rifContext || !m_isAttached) {
        return;
    }
    m_isAttached = false;

    for (const rif_image_filter& auxFilter : m_auxFilters) {
        m_rifContext->DetachFilter(auxFilter);
    }
    m_rifContext->DetachFilter(m_rifFilter);
}

struct ParameterSetter : public BOOST_NS::static_visitor<rif_int> {
    const char* paramName;
    rif_image_filter filter;

    rif_int operator()(int value) {
        return rifImageFilterSetParameter1u(filter, paramName, value);
    }

    rif_int operator()(float value) {
        return rifImageFilterSetParameter1f(filter, paramName, value);
    }

    rif_int operator()(std::string const& value) {
        return rifImageFilterSetParameterString(filter, paramName, value.c_str());
    }

    rif_int operator()(GfVec2i const& value) {
        return rifImageFilterSetParameter2u(filter, paramName, value[0], value[1]);
    }

    rif_int operator()(GfMatrix4f const& value) {
        return rifImageFilterSetParameter16f(filter, paramName, const_cast<float*>(value.data()));
    }

    rif_int operator()(rif_image image) {
        return rifImageFilterSetParameterImage(filter, paramName, image);
    }
};

void Filter::ApplyParameters() {
    for (const auto& param : m_params) {
        ParameterSetter setter;
        setter.paramName = param.first.c_str();
        setter.filter = m_rifFilter;
        RIF_ERROR_CHECK_THROW(BOOST_NS::apply_visitor(setter, param.second), "Failed to set image filter parameter");
    }
    for (const auto& namedInput : m_namedInputs) {
        if (namedInput.second.rifImage) {
            RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, namedInput.first.c_str(), namedInput.second.rifImage), "Failed to set image filter named parameter");
        }
    }
}

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE
