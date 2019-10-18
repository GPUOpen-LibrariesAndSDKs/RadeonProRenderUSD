#include "rifFilter.h"
#include "rifError.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

namespace {

class FilterAIDenoise final : public Filter {
    enum {
        RemapNormalFilter,
        RemapDepthFilter,
        AuxFilterMax
    };
public:
    explicit FilterAIDenoise(Context* rifContext);
    ~FilterAIDenoise() override = default;

    void AttachFilter() override;
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

    void AttachFilter() override;
};

class FilterResample final : public Filter {
public:
    explicit FilterResample(Context* rifContext, std::uint32_t width, std::uint32_t height);
    ~FilterResample() override = default;

    void AttachFilter() override;
};

class FilterCustom final : public Filter {
public:
    explicit FilterCustom(Context* rifContext, rif_image_filter_type type) : Filter(rifContext) {
        m_rifFilter = rifContext->CreateImageFilter(type);
    }
    ~FilterCustom() override = default;

    void AttachFilter() override {
        m_rifContext->AttachFilter(m_rifFilter, m_inputs.at(Color).rifImage, m_outputImage);
    }
};

FilterAIDenoise::FilterAIDenoise(Context* rifContext) : Filter(rifContext) {
    m_rifFilter = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_AI_DENOISE);

    // setup const parameters
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1u(m_rifFilter, "useHDR", 1), "Failed to set filter \"usdHDR\" parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterString(m_rifFilter, "modelPath", rifContext->GetModelPath().c_str()), "Failed to set filter \"modelPath\" parameter");

    // auxillary filters
    //m_auxFilters.resize(AuxFilterMax, nullptr);
    //m_auxFilters[RemapNormalFilter] = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_REMAP_RANGE);
    //m_auxFilters[RemapDepthFilter] = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_REMAP_RANGE);

    // setup remapping filters
    //RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1u(m_auxFilters[RemapNormalFilter], "srcRangeAuto", 0), "Failed to set filter parameter");
    //RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_auxFilters[RemapNormalFilter], "dstLo", -1.0f), "Failed to set filter parameter");
    //RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_auxFilters[RemapNormalFilter], "dstHi", +1.0f), "Failed to set filter parameter");
    //RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_auxFilters[RemapDepthFilter], "dstLo", 0.0f), "Failed to set filter parameter");
    //RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_auxFilters[RemapDepthFilter], "dstHi", 1.0f), "Failed to set filter parameter");
}

void FilterAIDenoise::AttachFilter() {
    // setup inputs
    //RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "normalsImg", m_inputs.at(Normal).rifImage), "Failed to set filter parameter");
    //RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "depthImg", m_inputs.at(Depth).rifImage), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "colorImg", m_inputs.at(Color).rifImage), "Failed to set filter parameter");
    //RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "albedoImg", m_inputs.at(Albedo).rifImage), "Failed to set filter parameter");

    //m_rifContext->AttachFilter(m_auxFilters[RemapNormalFilter], m_inputs.at(Normal).rifImage, m_inputs.at(Normal).rifImage);
    //m_rifContext->AttachFilter(m_auxFilters[RemapDepthFilter], m_inputs.at(Depth).rifImage, m_inputs.at(Depth).rifImage);
    m_rifContext->AttachFilter(m_rifFilter, m_inputs.at(Color).rifImage, m_outputImage);
}

FilterEaw::FilterEaw(Context* rifContext, std::uint32_t width, std::uint32_t height) : Filter(rifContext) {
    // main EAW filter
    m_rifFilter = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_EAW_DENOISE);

    // auxillary EAW filters
    m_auxFilters.resize(AuxFilterMax, nullptr);
    m_auxFilters[ColorVar] = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_TEMPORAL_ACCUMULATOR);
    m_auxFilters[Mlaa] = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_MLAA);

    // auxillary rif images
    rif_image_desc desc = { width, height, 1, width, width * height, 4, RIF_COMPONENT_TYPE_FLOAT32 };

    m_auxImages.resize(AuxImageMax);
    m_auxImages[ColorVarianceImage] = rifContext->CreateImage(desc);
    m_auxImages[DenoisedOutputImage] = rifContext->CreateImage(desc);
}

void FilterEaw::AttachFilter() {
    // setup params
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "normalsImg", m_inputs.at(Normal).rifImage), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "transImg", m_inputs.at(ObjectId).rifImage), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_rifFilter, "colorVar", m_inputs.at(Color).rifImage), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_rifFilter, "colorSigma", m_inputs.at(Color).sigma), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_rifFilter, "normalSigma", m_inputs.at(Normal).sigma), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_rifFilter, "depthSigma", m_inputs.at(Depth).sigma), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1f(m_rifFilter, "transSigma", m_inputs.at(ObjectId).sigma), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[Mlaa], "normalsImg", m_inputs.at(Normal).rifImage), "Failed to set filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[Mlaa], "meshIDImg", m_inputs.at(ObjectId).rifImage), "Failed to set filter parameter")
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[ColorVar], "positionsImg", m_inputs.at(WorldCoordinate).rifImage), "Failed to set variance filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[ColorVar], "normalsImg", m_inputs.at(Normal).rifImage), "Failed to set variance filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[ColorVar], "meshIdsImg", m_inputs.at(ObjectId).rifImage), "Failed to set variance filter parameter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameterImage(m_auxFilters[ColorVar], "outVarianceImg", m_auxImages[ColorVarianceImage]->GetHandle()), "Failed to set variance filter parameter");

    m_rifContext->AttachFilter(m_auxFilters[ColorVar], m_inputs.at(Color).rifImage, m_outputImage);
    m_rifContext->AttachFilter(m_rifFilter, m_outputImage, m_auxImages[DenoisedOutputImage]->GetHandle());
    m_rifContext->AttachFilter(m_auxFilters[Mlaa], m_auxImages[DenoisedOutputImage]->GetHandle(), m_outputImage);
}

FilterResample::FilterResample(Context* rifContext, std::uint32_t width, std::uint32_t height) : Filter(rifContext) {
    m_rifFilter = rifContext->CreateImageFilter(RIF_IMAGE_FILTER_RESAMPLE);

    // setup const parameters
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter1u(m_rifFilter, "interpOperator", RIF_IMAGE_INTERPOLATION_NEAREST), "Failed to set parameter of resample filter");
    RIF_ERROR_CHECK_THROW(rifImageFilterSetParameter2u(m_rifFilter, "outSize", width, height), "Failed to set parameter of resample filter");
}

void FilterResample::AttachFilter() {
    // attach filters
    m_rifContext->AttachFilter(m_rifFilter, m_inputs.at(Color).rifImage, m_outputImage);
}

} // namespace anonymous

std::unique_ptr<Filter> Filter::Create(FilterType type, Context* rifContext, std::uint32_t width, std::uint32_t height) {
    if (!width || !height) {
        return nullptr;
    }

    switch (type) {
        case FilterType::AIDenoise:
            return std::unique_ptr<FilterAIDenoise>(new FilterAIDenoise(rifContext));
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
    DettachFilter();

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

void Filter::SetInput(FilterInputType inputType, rif_image rifImage, const float sigma) {
    assert(rifImage);

    m_inputs[inputType] = {rifImage, nullptr, sigma};
    m_dirtyFlags |= DirtyIOImage;
}

void Filter::SetInput(FilterInputType inputType, rpr::FrameBuffer* rprFrameBuffer, const float sigma) {
    assert(rprFrameBuffer);

    m_retainedImages.push_back(m_rifContext->CreateImage(rprFrameBuffer));
    m_inputs[inputType] = {m_retainedImages.back()->GetHandle(), rprFrameBuffer, sigma};
    m_dirtyFlags |= DirtyIOImage;
}

void Filter::SetOutput(rif_image_desc imageDesc) {
    m_retainedImages.push_back(m_rifContext->CreateImage(imageDesc));
    SetOutput(m_retainedImages.back()->GetHandle());
}

void Filter::SetOutput(rif_image rifImage) {
    m_outputImage = rifImage;
    m_dirtyFlags |= DirtyIOImage;
}

void Filter::SetOutput(rpr::FrameBuffer* rprFrameBuffer) {
    m_retainedImages.push_back(m_rifContext->CreateImage(rprFrameBuffer));
    SetOutput(m_retainedImages.back()->GetHandle());
}

rif_image Filter::GetOutput() {
    return m_outputImage;
}

void Filter::SetParam(const char* name, FilterParam param) {
    m_params[name] = param;
    m_dirtyFlags |= DirtyParameters;
}

void Filter::Update() {
    if (m_dirtyFlags & DirtyParameters) {
        ApplyParameters();
    }
    if (m_dirtyFlags & DirtyIOImage) {
        DettachFilter();
        AttachFilter();
        m_isAttached = true;
    }

    for (auto& input : m_inputs) {
        if (input.second.rprFrameBuffer) {
            m_rifContext->UpdateInputImage(input.second.rprFrameBuffer, input.second.rifImage);
        }
    }

    m_dirtyFlags = Clean;
}

void Filter::DettachFilter() {
    if (!m_rifContext || !m_isAttached) {
        return;
    }
    m_isAttached = false;

    for (const rif_image_filter& auxFilter : m_auxFilters) {
        m_rifContext->DettachFilter(auxFilter);
    }
    m_rifContext->DettachFilter(m_rifFilter);
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

    rif_int operator()(GfMatrix4f const& value) {
        return rifImageFilterSetParameter16f(filter, paramName, const_cast<float*>(value.data()));
    }
};

void Filter::ApplyParameters() {
    for (const auto& param : m_params) {
        ParameterSetter setter;
        setter.paramName = param.first.c_str();
        setter.filter = m_rifFilter;
        RIF_ERROR_CHECK_THROW(BOOST_NS::apply_visitor(setter, param.second), "Failed to set image filter parameter");
    }
}

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE
