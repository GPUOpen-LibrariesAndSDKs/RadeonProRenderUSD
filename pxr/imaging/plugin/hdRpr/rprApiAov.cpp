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

#include "rprApiAov.h"
#include "rprApi.h"
#include "rprApiFramebuffer.h"
#include "rifcpp/rifError.h"

#include "pxr/imaging/rprUsd/contextMetadata.h"
#include "pxr/imaging/rprUsd/error.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

bool
ReadRifImage(rif_image image, void* dstBuffer, std::size_t dstBufferSize, std::int32_t width, std::int32_t height)
{
    if (!image || !dstBuffer) {
        return false;
    }

    std::size_t rifImageSize;
    std::size_t dummy;
    auto rifStatus = rifImageGetInfo(image, RIF_IMAGE_DATA_SIZEBYTE, sizeof(rifImageSize), &rifImageSize, &dummy);

    if (rifStatus != RIF_SUCCESS) {
        return false;
    }
    
    const std::int32_t pixelSize = rifImageSize / (width * height);
    const std::int32_t rowSize = width * pixelSize;

    enum class MappingMode
    {
        EqualSize,
        AdditionalRow,
        AdditionalColumn,
        AdditionalRowAndColumn
    };

    MappingMode mode = MappingMode::EqualSize;

    // For upscaled images destination size might not exactly be equal to RIF output
    // To prevent data loss plugin renders image with bigger size
    if (dstBufferSize != rifImageSize) {
        bool validBufferSize = false;

        const std::int32_t additionalRowSize = pixelSize * width;

        if (dstBufferSize + additionalRowSize == rifImageSize) {
            validBufferSize = true;
            mode = MappingMode::AdditionalRow;
        }

        const std::int32_t additionalColSize = pixelSize * height;

        if (dstBufferSize + additionalColSize == rifImageSize) {
            validBufferSize = true;
            mode = MappingMode::AdditionalColumn;
        }

        if (dstBufferSize + additionalColSize + additionalRowSize + pixelSize == rifImageSize) {
            validBufferSize = true;
            mode = MappingMode::AdditionalRowAndColumn;
        }

        if (!validBufferSize) {
            return false;
        }
    }

    void* data = nullptr;
    rifStatus = rifImageMap(image, RIF_IMAGE_MAP_READ, &data);

    if (rifStatus != RIF_SUCCESS) {
        return false;
    }

    switch (mode)
    {
    // If output dimesions equal to input dimensions use simple copy
    // Additional row (if rendered) would be discarded
    case MappingMode::EqualSize:
    case MappingMode::AdditionalRow:
        std::memcpy(dstBuffer, data, dstBufferSize);
        break;

    // For additional column each last row pixel should be discarded
    case MappingMode::AdditionalColumn:
    case MappingMode::AdditionalRowAndColumn:
        for (std::int32_t i = 0; i < height; i++) {
            const std::int32_t offset = i * rowSize;
            std::memcpy(((std::byte*)dstBuffer) + offset, ((std::byte*)data) + offset + i * pixelSize, rowSize);
        }
        break;
    }

    rifStatus = rifImageUnmap(image, data);
    if (rifStatus != RIF_SUCCESS) {
        TF_WARN("Failed to unmap rif image");
    }

    return true;
}

} // namespace anonymous

HdRprApiAov::HdRprApiAov(int width,
                         int height,
                         float renderResolution,
                         HdFormat format,
                         rif::Context* rifContext,
                         rpr_aov rprAovType,
                         rpr::Context* rprContext,
                         RprUsdContextMetadata const& rprContextMetadata)
    : m_aovDescriptor(HdRprAovRegistry::GetInstance().GetAovDesc(rprAovType, false))
    , m_format(format)
    , m_width(width)
    , m_height(height)
    , m_renderResolution(renderResolution)
    , m_rifContext(rifContext)
{
    if (rif::Image::GetDesc(0, 0, format).type == 0) {
        RIF_THROW_ERROR_MSG("Unsupported format: " + TfEnum::GetName(format));
    }

    std::int32_t actualWidth = std::ceil(width * renderResolution);
    std::int32_t actualHeight = std::ceil(height * renderResolution);

    m_aov = pxr::make_unique<HdRprApiFramebuffer>(rprContext, actualWidth, actualHeight);
    m_aov->AttachAs(rprAovType);


    // XXX (Hybrid): Hybrid plugin does not support framebuffer resolving (rprContextResolveFrameBuffer)
    if (rprContextMetadata.pluginType != kPluginHybrid) {
        m_resolved = pxr::make_unique<HdRprApiFramebuffer>(rprContext, actualWidth, actualHeight);
    }
}

HdRprApiAov::HdRprApiAov(int width,
                         int height,
                         float renderResolution,
                         HdFormat format,
                         rif::Context* rifContext,
                         HdRprAovDescriptor const& aovDescriptor)
    : m_aovDescriptor(aovDescriptor)
    , m_format(format)
    , m_width(width)
    , m_height(height)
    , m_renderResolution(renderResolution)
    , m_rifContext(rifContext)
{}

void 
HdRprApiAov::Resolve()
{
    if (m_aov) {
        m_aov->Resolve(m_resolved.get());
    }

    for (auto& filter : m_filters) {
        filter.second->Resolve();
    }
}

void
HdRprApiAov::Clear() 
{
    if (m_aov) {
        auto& v = m_aovDescriptor.clearValue;
        m_aov->Clear(v[0], v[1], v[2], v[3]);
    }
}

bool 
HdRprApiAov::GetDataImpl(void* dstBuffer, size_t dstBufferSize)
{
    if (!m_filters.empty()) {
        return ReadRifImage(m_filters.back().second->GetOutput(), dstBuffer, dstBufferSize, m_width, m_height);
    }

    HdRprApiFramebuffer* resolvedFb = GetResolvedFb();

    if (!resolvedFb) {
        return false;
    }

    return resolvedFb->GetData(dstBuffer, dstBufferSize);
}

bool
HdRprApiAov::GetData(void* dstBuffer, size_t dstBufferSize)
{
    if (GetDataImpl(dstBuffer, dstBufferSize)) {
        if (m_format == HdFormatInt32) {
            // RPR store integer ID values to RGB images using such formula:
            // c[i].x = i;
            // c[i].y = i/256;
            // c[i].z = i/(256*256);
            // i.e. saving little endian int24 to uchar3
            // That's why we interpret the value as int and filling the alpha channel with zeros
            auto primIdData = reinterpret_cast<int*>(dstBuffer);
            for (size_t i = 0; i < dstBufferSize / sizeof(int); ++i) {
                primIdData[i] = (primIdData[i] & 0xFFFFFF) - 1;
            }
        }

        return true;
    }

    return false;
}

void
HdRprApiAov::Resize(int width, int height, HdFormat format, float renderResolution)
{
    if (m_width != width || m_height != height || m_renderResolution != renderResolution) {
        m_width = width;
        m_height = height;
        m_renderResolution = renderResolution;
        m_dirtyBits |= ChangeTracker::DirtySize;
    }

    if (m_format != format) {
        m_format = format;
        m_dirtyBits |= ChangeTracker::DirtyFormat;
    }

    if (m_aov && m_aov->Resize(std::ceil(width * m_renderResolution), std::ceil(height * m_renderResolution))) {
        m_dirtyBits |= ChangeTracker::DirtySize;
    }

    if (m_resolved && m_resolved->Resize(std::ceil(width * m_renderResolution), std::ceil(height * m_renderResolution))) {
        m_dirtyBits |= ChangeTracker::DirtyFormat;
    }
}

void
HdRprApiAov::Update(HdRprApi const* rprApi, rif::Context* rifContext)
{
    if (m_dirtyBits & ChangeTracker::DirtyFormat) {
        OnFormatChange(rifContext);
    }

    if (m_dirtyBits & ChangeTracker::DirtySize) {
        OnSizeChange(rifContext);
    }

    m_dirtyBits = ChangeTracker::Clean;

    for (auto& filter : m_filters) {
        filter.second->Update();
    }
}

rif::Filter*
HdRprApiAov::FindFilter(FilterType type)
{
    auto it = std::find_if(m_filters.begin(), m_filters.end(), [type](auto& entry) {
        return entry.first == type;
    });

    if (it != m_filters.end()) {
        return it->second.get();
    } else {
        return nullptr;
    }
}

void 
HdRprApiAov::OnFormatChange(rif::Context* rifContext)
{
    if (rifContext && (m_format != HdFormatFloat32Vec4 || m_renderResolution != 1.0f)) {
        SetFilter(FilterType::kFilterResample, true);

        // Reset inputs
        m_dirtyBits |= ChangeTracker::DirtySize;
    }
}

void
HdRprApiAov::OnSizeChange(rif::Context* rifContext)
{
    if (m_filters.empty()) {
        return;
    }

    auto resizeFilter = [this](int width, int height, rif::Filter* filter, auto input) {
        filter->Resize(width, height);
        filter->SetInput(rif::Color, input);
        filter->SetOutput(rif::Image::GetDesc(width, height, m_format));
    };

    if (m_filters.size() == 1) {
        resizeFilter(m_width, m_height, m_filters.back().second.get(), GetRifInputFramebuffer());
    } else {
        std::int32_t intermediateWidth = std::ceil(m_width * m_renderResolution);
        std::int32_t intermediateHeight = std::ceil(m_height * m_renderResolution);
        
        // Process filter chain
        void* filterInput = GetRifInputFramebuffer();

        for (auto it = m_filters.begin(); it != std::prev(m_filters.end()); ++it) {
            if (it == m_filters.begin())
            {
                resizeFilter(intermediateWidth, intermediateHeight, it->second.get(), (HdRprApiFramebuffer*)filterInput);
            }
            else
            {
                resizeFilter(intermediateWidth, intermediateHeight, it->second.get(), (rif_image)filterInput);
            }
            filterInput = it->second->GetOutput();
        }

        // Process last filter
        auto& lastFilter = m_filters.back().second;
        resizeFilter(m_width, m_height, lastFilter.get(), (rif_image)filterInput);
    }
}

HdRprApiColorAov::HdRprApiColorAov(int width,
                                   int height,
                                   float renderResolution,
                                   HdFormat format,
                                   rif::Context* rifContext,
                                   rpr::Context* rprContext,
                                   RprUsdContextMetadata const& rprContextMetadata,
                                   std::shared_ptr<HdRprApiAov> rawColorAov)
    : HdRprApiAov(width, height, renderResolution, format, rifContext, HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kColorAlpha), true))
    , m_retainedRawColor(std::move(rawColorAov)) 
{}

void
HdRprApiAov::SetFilter(FilterType filter, bool enable)
{
    // Disable filter
    if (!enable) {
        m_filters.remove_if([filter](auto& entry) { return entry.first == filter; });
        return;
    }

    // Do not do anything if filter of that type already created
    if (FindFilter(filter)) {
        return;
    }

    // Enable filter
    std::unique_ptr<rif::Filter> createdFilter;

    switch (filter) {
    case FilterType::kFilterTonemap: {
            createdFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_PHOTO_LINEAR_TONEMAP, m_rifContext);
        }
        break;

    case FilterType::kFilterAIDenoise:
    case FilterType::kFilterEAWDenoise: {
            auto denoiseFilterType = (filter == FilterType::kFilterAIDenoise) ? rif::FilterType::AIDenoise : rif::FilterType::EawDenoise;
            createdFilter = rif::Filter::Create(denoiseFilterType, m_rifContext, m_width, m_height);
        }
        break;

    case FilterType::kFilterComposeOpacity: {
            createdFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_USER_DEFINED, m_rifContext);
            auto opacityComposingKernelCode = std::string(R"(
                           int2 coord;
                           GET_COORD_OR_RETURN(coord, GET_BUFFER_SIZE(inputImage));
                           vec4 alpha = ReadPixelTyped(alphaImage, coord.x, coord.y);
                           vec4 color = ReadPixelTyped(inputImage, coord.x, coord.y) * alpha.x;
                           WritePixelTyped(outputImage, coord.x, coord.y, make_vec4(color.x, color.y, color.z, alpha.x));
                       )");
            createdFilter->SetParam("code", opacityComposingKernelCode);
        }
        break;

    case FilterType::kFilterUpscale: {
            createdFilter = rif::Filter::Create(rif::FilterType::Upscale, m_rifContext, 1, 1);
        }
        break;

    case FilterType::kFilterResample: {
            createdFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, m_rifContext);
            createdFilter->SetParam("interpOperator", (int)RIF_IMAGE_INTERPOLATION_NEAREST);
            createdFilter->SetParam("outSize", GfVec2i(m_width, m_height));
        }
        break;

    default:
        throw std::runtime_error("Unsupported filter was set");
    }

    AddFilter(filter, std::move(createdFilter));
}

void 
HdRprApiAov::AddFilter(FilterType type, std::unique_ptr<rif::Filter> filter)
{
    // Do not do anything if filter of that type already created
    if (FindFilter(type)) {
        throw std::runtime_error("Trying to add existing filter type");
    }

    if (m_filters.empty()) {
        m_filters.emplace_back(type, std::move(filter));
    } else {
        std::int32_t newFilterPriority = static_cast<std::underlying_type_t<FilterType>>(type);
        auto it = m_filters.begin();

        for (; it != m_filters.end(); it++) {
            std::int32_t priority = static_cast<std::underlying_type_t<FilterType>>(it->first);
            if (newFilterPriority < priority) {
                break;
            }
        }

        m_filters.emplace(it, type, std::move(filter));
    }

    // Signal to update inputs
    m_dirtyBits |= ChangeTracker::DirtySize;
}

void
HdRprApiColorAov::SetOpacityAov(std::shared_ptr<HdRprApiAov> opacity)
{
    if (m_retainedOpacity != opacity) {
        m_retainedOpacity = opacity;
        SetFilter(FilterType::kFilterComposeOpacity, CanComposeAlpha());
    }
}

void
HdRprApiColorAov::InitAIDenoise(std::shared_ptr<HdRprApiAov> albedo,
                                std::shared_ptr<HdRprApiAov> normal,
                                std::shared_ptr<HdRprApiAov> linearDepth)
{
    if (FindFilter(FilterType::kFilterAIDenoise)) {
        return;
    }
    if (!albedo || !normal || !linearDepth) {
        TF_RUNTIME_ERROR("Failed to enable AI denoise: invalid parameters");
        return;
    }

    for (auto& retainedInput : m_retainedDenoiseInputs) {
        retainedInput = nullptr;
    }

    m_retainedDenoiseInputs[rif::Normal] = normal;
    m_retainedDenoiseInputs[rif::LinearDepth] = linearDepth;
    m_retainedDenoiseInputs[rif::Albedo] = albedo;

    m_denoiseFilterType = FilterType::kFilterAIDenoise;
}

void
HdRprApiColorAov::InitEAWDenoise(std::shared_ptr<HdRprApiAov> albedo,
                                 std::shared_ptr<HdRprApiAov> normal,
                                 std::shared_ptr<HdRprApiAov> linearDepth,
                                 std::shared_ptr<HdRprApiAov> objectId,
                                 std::shared_ptr<HdRprApiAov> worldCoordinate)
{
    if (FindFilter(FilterType::kFilterEAWDenoise)) {
        return;
    }

    if (!albedo || !normal || !linearDepth || !objectId || !worldCoordinate) {
        TF_RUNTIME_ERROR("Failed to enable EAW denoise: invalid parameters");
        return;
    }

    for (auto& retainedInput : m_retainedDenoiseInputs) {
        retainedInput = nullptr;
    }

    m_retainedDenoiseInputs[rif::Normal] = normal;
    m_retainedDenoiseInputs[rif::LinearDepth] = linearDepth;
    m_retainedDenoiseInputs[rif::ObjectId] = objectId;
    m_retainedDenoiseInputs[rif::Albedo] = albedo;
    m_retainedDenoiseInputs[rif::WorldCoordinate] = worldCoordinate;

    m_denoiseFilterType = FilterType::kFilterEAWDenoise;
}

void
HdRprApiColorAov::DeinitDenoise(rif::Context* rifContext)
{
    for (auto& retainedInput : m_retainedDenoiseInputs) {
        retainedInput = nullptr;
    }

    m_denoiseFilterType = FilterType::kFilterNone;

    SetFilter(FilterType::kFilterAIDenoise, false);
    SetFilter(FilterType::kFilterEAWDenoise, false);
    OnSizeChange(rifContext);
}

void
HdRprApiColorAov::SetDenoise(bool enable, HdRprApi const* rprApi, rif::Context* rifContext)
{
    if (m_denoiseFilterType != FilterType::kFilterNone) {
        SetFilter(m_denoiseFilterType, enable);
        SetFilter(m_denoiseFilterType == FilterType::kFilterAIDenoise ? FilterType::kFilterEAWDenoise : FilterType::kFilterAIDenoise, false);
    } else {
        SetFilter(FilterType::kFilterAIDenoise, false);
        SetFilter(FilterType::kFilterEAWDenoise, false);
    }

    SetFilter(FilterType::kFilterResample, m_format != HdFormatFloat32Vec4);

    Update(rprApi, rifContext);
}

void
HdRprApiColorAov::SetTonemap(TonemapParams const& params)
{
    bool isTonemapEnabled = FindFilter(FilterType::kFilterTonemap);
    bool tonemapEnableDirty = params.enable != isTonemapEnabled;

    SetFilter(FilterType::kFilterTonemap, params.enable);

    if (m_tonemap != params) {
        m_tonemap = params;

        if (!tonemapEnableDirty && isTonemapEnabled) {
            SetTonemapFilterParams(FindFilter(FilterType::kFilterTonemap));
        }
    }
}

void
HdRprApiColorAov::SetUpscale(UpscaleAndDenoiseParams const& params)
{
    SetFilter(FilterType::kFilterUpscale, params.enable);

    if (params.enable) {
        rif::Filter* filter = FindFilter(FilterType::kFilterUpscale);

        switch (params.mode) {
        case HdRprApiColorAov::UpscaleAndDenoiseParams::UpscalerMode::Best:
            filter->SetParam("mode", (int)RIF_AI_UPSCALE_MODE_BEST_2X);
            break;

        case HdRprApiColorAov::UpscaleAndDenoiseParams::UpscalerMode::Good:
            filter->SetParam("mode", (int)RIF_AI_UPSCALE_MODE_GOOD_2X);
            break;

        case HdRprApiColorAov::UpscaleAndDenoiseParams::UpscalerMode::Fast:
            filter->SetParam("mode", (int)RIF_AI_UPSCALE_MODE_FAST_2X);
            break;
        }
    }
}

void
HdRprApiColorAov::SetTonemapFilterParams(rif::Filter* filter)
{
    filter->SetParam("exposureTime", m_tonemap.exposureTime);
    filter->SetParam("sensitivity", m_tonemap.sensitivity);
    filter->SetParam("fstop", m_tonemap.fstop);
    filter->SetParam("gamma", m_tonemap.gamma);
}

bool
HdRprApiColorAov::CanComposeAlpha()
{
    // Compositing alpha into framebuffer with less than 4 components is a no-op
    return HdGetComponentCount(m_format) == 4 && m_retainedOpacity;
}

bool
HdRprApiColorAov::GetData(void* dstBuffer, size_t dstBufferSize)
{
    if (IsFiltersEmpty()) {
        if (auto resolvedRawColorFb = m_retainedRawColor->GetResolvedFb()) {
            return resolvedRawColorFb->GetData(dstBuffer, dstBufferSize);
        } else {
            return false;
        }
    } else {
        return HdRprApiAov::GetData(dstBuffer, dstBufferSize);
    }
}

void
HdRprApiColorAov::OnFormatChange(rif::Context* rifContext)
{
    SetFilter(FilterType::kFilterComposeOpacity, CanComposeAlpha());
    HdRprApiAov::OnFormatChange(rifContext);
}

void
HdRprApiColorAov::OnSizeChange(rif::Context* rifContext)
{
    if (auto filter = FindFilter(FilterType::kFilterAIDenoise) ? FindFilter(FilterType::kFilterAIDenoise) : FindFilter(FilterType::kFilterEAWDenoise)) {
        for (int i = 0; i < rif::MaxInput; ++i) {
            if (auto retainedInput = m_retainedDenoiseInputs[i].get()) {
                filter->SetInput(static_cast<rif::FilterInputType>(i), retainedInput->GetResolvedFb());
            }
        }
    }

    if (auto filter = FindFilter(FilterType::kFilterComposeOpacity)) {
        filter->SetInput("alphaImage", m_retainedOpacity->GetResolvedFb());
    }

    if (auto filter = FindFilter(FilterType::kFilterTonemap)) {
        SetTonemapFilterParams(filter);
    }

    HdRprApiAov::OnSizeChange(rifContext);
}

HdRprApiComputedAov::HdRprApiComputedAov(int width,
                                         int height,
                                         float renderResolution,
                                         HdFormat format,
                                         rif::Context* rifContext,
                                         HdRprAovDescriptor const& aovDescriptor)
    : HdRprApiAov(width, height, renderResolution, format, rifContext, aovDescriptor)
{}

HdRprApiNormalAov::HdRprApiNormalAov(int width,
                                     int height,
                                     float renderResolution,
                                     HdFormat format,
                                     rif::Context* rifContext,
                                     rpr::Context* rprContext,
                                     RprUsdContextMetadata const& rprContextMetadata)
    : HdRprApiAov(width, height, renderResolution, format, rifContext, RPR_AOV_SHADING_NORMAL, rprContext, rprContextMetadata)
{
    if (!rifContext) {
        RPR_THROW_ERROR_MSG("Can not create normal AOV: RIF context required");
    }

    auto filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_REMAP_RANGE, rifContext);
    filter->SetParam("srcRangeAuto", 0);
    filter->SetParam("dstLo", -1.0f);
    filter->SetParam("dstHi", 1.0f);

    AddFilter(FilterType::kFilterRemapRange, std::move(filter));
}

void
HdRprApiNormalAov::OnFormatChange(rif::Context* rifContext)
{
    HdRprApiAov::OnFormatChange(rifContext);
}

HdRprApiDepthAov::HdRprApiDepthAov(int width,
                                   int height,
                                   float renderResolution,
                                   HdFormat format,
                                   rif::Context* rifContext,
                                   rpr::Context* rprContext,
                                   RprUsdContextMetadata const& rprContextMetadata,
                                   std::shared_ptr<HdRprApiAov> worldCoordinateAov)
    : HdRprApiComputedAov(width, height, renderResolution, format, rifContext, HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kNdcDepth), true))
    , m_retainedWorldCoordinateAov(worldCoordinateAov)
{
    if (!rifContext) {
        RPR_THROW_ERROR_MSG("Can not create depth AOV: RIF context required");
    }

    auto ndcFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_NDC_DEPTH, rifContext);
    AddFilter(FilterType::kFilterNdcDepth, std::move(ndcFilter));

#if PXR_VERSION >= 2002
    auto remapFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_REMAP_RANGE, rifContext);
    remapFilter->SetParam("srcRangeAuto", 0);
    remapFilter->SetParam("srcLo", -1.0f);
    remapFilter->SetParam("srcHi", 1.0f);
    remapFilter->SetParam("dstLo", 0.0f);
    remapFilter->SetParam("dstHi", 1.0f);
    AddFilter(FilterType::kFilterRemapRange, std::move(remapFilter));
#endif
}

void
HdRprApiDepthAov::Update(HdRprApi const* rprApi, rif::Context* rifContext)
{
    auto viewProjectionMatrix = rprApi->GetCameraViewMatrix() * rprApi->GetCameraProjectionMatrix();
    FindFilter(FilterType::kFilterNdcDepth)->SetParam("viewProjMatrix", GfMatrix4f(viewProjectionMatrix.GetTranspose()));

    HdRprApiAov::Update(rprApi, rifContext);
}

HdRprApiIdMaskAov::HdRprApiIdMaskAov(int width,
                                     int height,
                                     float renderResolution,
                                     HdFormat format,
                                     rif::Context* rifContext,
                                     HdRprAovDescriptor const& aovDescriptor,
                                     rpr::Context* rprContext,
                                     RprUsdContextMetadata const& rprContextMetadata,
                                     std::shared_ptr<HdRprApiAov> const& baseIdAov)
    : HdRprApiComputedAov(width, height, renderResolution, format, rifContext, aovDescriptor)
    , m_baseIdAov(baseIdAov)
{
    if (!rifContext) {
        RPR_THROW_ERROR_MSG("Can not create id mask AOV: RIF context required");
    }

    auto filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_USER_DEFINED, rifContext);
    auto colorizeIdKernelCode = std::string(R"(
        int2 coord;
        GET_COORD_OR_RETURN(coord, GET_BUFFER_SIZE(inputImage));
        vec3 idEncoded = ReadPixelTyped(inputImage, coord.x, coord.y).xyz;
        unsigned int idDecoded = (unsigned int)(idEncoded.x*256) + (unsigned int)(idEncoded.y*256*256) + (unsigned int)(idEncoded.z*256*256*256);

        vec4 color;
        if (idDecoded) {
            unsigned int v0 = 0x123;
            unsigned int v1 = idDecoded;
            unsigned int s0 = 0;
            const unsigned int N = 4;
            for( unsigned int n = 0; n < N; n++ ) {
                s0 += 0x9e3779b9;
                v0 += ((v1<<4)+0xa341316c)^(v1+s0)^((v1>>5)+0xc8013ea4);
                v1 += ((v0<<4)+0xad90777d)^(v0+s0)^((v0>>5)+0x7e95761e);
            }
            color = make_vec4(v0&0xFFFF, v0>>16, v1&0xFFFF, 0xFFFF)/(float)(0xFFFF);
        } else {
            color = make_vec4(0.0f, 0.0f, 0.0f, 0.0f);
        }

        WritePixelTyped(outputImage, coord.x, coord.y, color);
    )");
    filter->SetParam("code", colorizeIdKernelCode);

    AddFilter(FilterType::kFilterUserDefined, std::move(filter));
}

HdRprApiAovBuilder&
HdRprApiAovBuilder::WithType(rpr_aov type)
{
    this->type = type;
    return *this;
}

HdRprApiAovBuilder&
HdRprApiAovBuilder::WithSize(int width, int height)
{
    this->width = width;
    this->height = height;
    return *this;
}

HdRprApiAovBuilder&
HdRprApiAovBuilder::WithFormat(HdFormat format)
{
    this->format = format;
    return *this;
}

HdRprApiAovBuilder&
HdRprApiAovBuilder::WithRprContext(rpr::Context* context)
{
    this->rprContext = context;
    return *this;
}

HdRprApiAovBuilder&
HdRprApiAovBuilder::WithRifContext(rif::Context* context)
{
    this->rifContext = context;
    return *this;
}

HdRprApiAovBuilder&
HdRprApiAovBuilder::WithRprContextMetadata(RprUsdContextMetadata* metadata)
{
    this->rprContextMetadata = metadata;
    return *this;
}

HdRprApiAovBuilder&
HdRprApiAovBuilder::WithRenderResolution(float renderResolution)
{
    this->renderResolution = renderResolution;
    return *this;
}

HdRprApiAovBuilder&
HdRprApiAovBuilder::WithRawColorAov(std::shared_ptr<HdRprApiAov> rawColorAov)
{
    this->rawColorAov = rawColorAov;
    return *this;
}

HdRprApiAovBuilder&
HdRprApiAovBuilder::WithWorldCoordinateAov(std::shared_ptr<HdRprApiAov> worldCoordinateAov)
{
    this->worldCoordinateAov = worldCoordinateAov;
    return *this;
}

HdRprApiAovBuilder&
HdRprApiAovBuilder::WithBaseIdAov(std::shared_ptr<HdRprApiAov> baseIdAov)
{
    this->baseIdAov = baseIdAov;
    return *this;
}

HdRprApiAovBuilder&
HdRprApiAovBuilder::WithAovDesc(HdRprAovDescriptor aovDesc)
{
    this->aovDesc = aovDesc;
    return *this;
}

HdRprApiAov*
HdRprApiAovBuilder::Build()
{
    if (!type.has_value()) {
        throw std::runtime_error("Type is required parameter");
    }

    switch (type.value()) {
    case rpr::Aov::RPR_AOV_COLOR:
        if (baseIdAov.has_value()) {
            return new HdRprApiIdMaskAov(
                width.value(), 
                height.value(), 
                renderResolution.value(),
                format.value(),
                rifContext.value(),
                aovDesc.value(), 
                rprContext.value(), 
                *rprContextMetadata.value(),
                baseIdAov.value()
            );
        } else if (worldCoordinateAov.has_value()) {
            return new HdRprApiDepthAov(
                width.value(),
                height.value(),
                renderResolution.value(),
                format.value(),
                rifContext.value(),
                rprContext.value(), 
                *rprContextMetadata.value(), 
                worldCoordinateAov.value()
            );
        } else if (rawColorAov.has_value()) {
            return new HdRprApiColorAov(
                width.value(),
                height.value(),
                renderResolution.value(),
                format.value(),
                rifContext.value(),
                rprContext.value(), 
                *rprContextMetadata.value(), 
                rawColorAov.value()
            );
        } else {
            return new HdRprApiAov(
                width.value(),
                height.value(),
                renderResolution.value(),
                format.value(),
                rifContext.value(),
                type.value(), 
                rprContext.value(), 
                *rprContextMetadata.value()
            );
        }

    case rpr::Aov::RPR_AOV_SHADING_NORMAL:
        return new HdRprApiNormalAov(
            width.value(), 
            height.value(), 
            renderResolution.value(),
            format.value(), 
            rifContext.value(), 
            rprContext.value(), 
            *rprContextMetadata.value()
        );

    case rpr::Aov::RPR_AOV_WORLD_COORDINATE:
    case rpr::Aov::RPR_AOV_OBJECT_ID:
    case rpr::Aov::RPR_AOV_VARIANCE:
    case rpr::Aov::RPR_AOV_DIFFUSE_ALBEDO:
    default:
        return new HdRprApiAov(
            width.value(),
            height.value(),
            renderResolution.value(),
            format.value(),
            rifContext.value(),
            type.value(),
            rprContext.value(),
            *rprContextMetadata.value()
        );
    }

    throw std::runtime_error("Can't build\n");
}

PXR_NAMESPACE_CLOSE_SCOPE
