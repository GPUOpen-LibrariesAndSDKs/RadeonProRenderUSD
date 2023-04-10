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

bool ReadRifImage(rif_image image, void* dstBuffer, size_t dstBufferSize) {
    if (!image || !dstBuffer) {
        return false;
    }

    size_t size;
    size_t dummy;
    auto rifStatus = rifImageGetInfo(image, RIF_IMAGE_DATA_SIZEBYTE, sizeof(size), &size, &dummy);
    if (rifStatus != RIF_SUCCESS || dstBufferSize < size) {
        return false;
    }

    void* data = nullptr;
    rifStatus = rifImageMap(image, RIF_IMAGE_MAP_READ, &data);
    if (rifStatus != RIF_SUCCESS) {
        return false;
    }

    std::memcpy(dstBuffer, data, size);

    rifStatus = rifImageUnmap(image, data);
    if (rifStatus != RIF_SUCCESS) {
        TF_WARN("Failed to unmap rif image");
    }

    return true;
}

} // namespace anonymous

HdRprApiAov::HdRprApiAov(rpr_aov rprAovType, int width, int height, HdFormat format,
                         rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, std::unique_ptr<rif::Filter> filter)
    : m_aovDescriptor(HdRprAovRegistry::GetInstance().GetAovDesc(rprAovType, false))
    , m_filter(std::move(filter))
    , m_upscaleFilter(nullptr)
    , m_format(format), m_width(width), m_height(height) {
    if (rif::Image::GetDesc(0, 0, format).type == 0) {
        RIF_THROW_ERROR_MSG("Unsupported format: " + TfEnum::GetName(format));
    }

    m_aov = pxr::make_unique<HdRprApiFramebuffer>(rprContext, width, height);
    m_aov->AttachAs(rprAovType);

    // XXX (Hybrid): Hybrid plugin does not support framebuffer resolving (rprContextResolveFrameBuffer)
    if (!RprUsdIsHybrid(rprContextMetadata.pluginType)) {
        m_resolved = pxr::make_unique<HdRprApiFramebuffer>(rprContext, width, height);
    }
}

HdRprApiAov::HdRprApiAov(rpr_aov rprAovType, int width, int height, HdFormat format,
                         rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext)
    : HdRprApiAov(rprAovType, width, height, format, rprContext, rprContextMetadata, [format, rifContext]() -> std::unique_ptr<rif::Filter> {
        if (format == HdFormatFloat32Vec4) {
            // RPR framebuffers by default with such format
            return nullptr;
        }
        if (!rifContext)
        {
            if (format == HdFormatFloat32) {
                return nullptr;
            }
            if (format == HdFormatInt32) {
                return nullptr;
            }
            RPR_THROW_ERROR_MSG("Only Float32Vec4, Float32, and Int32 data types are supported without rifContext.");
        }

        auto filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, rifContext);
        if (!filter) {
            RPR_THROW_ERROR_MSG("Failed to create resample filter");
        }

        filter->SetParam("interpOperator", (int) RIF_IMAGE_INTERPOLATION_NEAREST);
        return filter;
    }()) {
}

void HdRprApiAov::Resolve() {
    if (m_aov) {
        m_aov->Resolve(m_resolved.get());
    }

    if (m_filter) {
        m_filter->Resolve();
    }
    if (m_upscaleFilter) {
        m_upscaleFilter->Resolve();
    }
}

void HdRprApiAov::Clear() {
    if (m_aov) {
        auto& v = m_aovDescriptor.clearValue;
        m_aov->Clear(v[0], v[1], v[2], v[3]);
    }
}

bool HdRprApiAov::GetDataImpl(void* dstBuffer, size_t dstBufferSize) {
    if (m_filter) {
        return ReadRifImage(m_filter->GetOutput(), dstBuffer, dstBufferSize);
    }

    auto resolvedFb = GetResolvedFb();
    if (!resolvedFb) {
        return false;
    }

    return resolvedFb->GetData(dstBuffer, dstBufferSize);
}

bool HdRprApiAov::InitUpscaleFilter(rif::Context* rifContext) {
    if (!rifContext) {
        return false;
    }
    if (m_upscaleFilter) {
        return true;
    }
    m_upscaleFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_AI_UPSCALE, rifContext);
    if (!m_upscaleFilter) {
        return false;
    }
    m_upscaleFilter->SetParam("mode", (int) RIF_AI_UPSCALE_MODE_FAST_2X);
    m_upscaleFilter->SetParam("useHDR", 1);
    m_upscaleFilter->SetParam("modelPath", rifContext->GetModelPath().c_str());
    m_upscaleFilter->Resize(m_width, m_height);
    m_upscaleFilter->SetOutput(rif::Image::GetDesc(m_width * 2, m_height * 2, m_format));
    return true;
}

bool HdRprApiAov::GetUpscaledDataImpl(void* dstBuffer, size_t dstBufferSize, rif::Context* rifContext) {
    if (!m_upscaleFilter) {
        if (!InitUpscaleFilter(rifContext)) {
            return false;
        }
    }
    
    if (!ReadRifImage(m_upscaleFilter->GetOutput(), dstBuffer, dstBufferSize))
    {
        return false;
    }

    if (m_oddWidth) {
        size_t pixelSize = HdDataSizeOfFormat(m_format);
        for (size_t i = (size_t)m_height * 2 - 1; i > 0; --i) {
            memmove((char*)dstBuffer + i * ((size_t)m_width * 2 + 1) * pixelSize, (char*)dstBuffer + i * (size_t)m_width * 2 * pixelSize, (size_t)m_width * 2 * pixelSize);
        }
    }

    return true;
}

void HdRprApiAov::SetUpSizeAndBuffer(void*& getBuffer, size_t& dstBufferSize) {
    if (!m_filter)
    {
        bool needTmpBuffer = true;
        // Rpr always renders to HdFormatFloat32Vec4
        // If RIF is enabled then m_filter will cast to the desired type.
        // But if RIF isn't enabled we must do the cast ourselves here.
        //
        // When this function is called dstBufferSize is set to the desired format buffer size.
        // We must allocate m_tmpBuffer to size of a HdFormatFloat32Vec4 buffer.
        // For both Float32 and Int32 we do this by multiplying the desired format buffer size by 4.
        if (m_format == HdFormatFloat32) {
            dstBufferSize = dstBufferSize * 4;
        }
        else if (m_format == HdFormatInt32) {
            dstBufferSize = dstBufferSize * 4;
        }
        else {
            needTmpBuffer = false;
        }
        if (needTmpBuffer)
        {
            if (m_tmpBuffer.size() < dstBufferSize)
            {
                m_tmpBuffer.resize(dstBufferSize);
            }
            getBuffer = m_tmpBuffer.data();
        }
    }
}

void HdRprApiAov::ApplyFormatToOutput(void* getBuffer, void* dstBuffer, size_t dstBufferSize) {
    if (!m_filter)
    {
        if (m_format == HdFormatFloat32) {
            auto srcData = reinterpret_cast<const GfVec4f*>(getBuffer);
            auto dstData = reinterpret_cast<float*>(dstBuffer);
            for (size_t i = 0; i < dstBufferSize / sizeof(GfVec4f); ++i) {
                dstData[i] = srcData[i][0];
            }
        }
        if (m_format == HdFormatInt32) {
            auto srcData = reinterpret_cast<const float*>(getBuffer);
            auto dstData = reinterpret_cast<char*>(dstBuffer);
            for (size_t i = 0; i < dstBufferSize / sizeof(float); ++i)
            {
                if (i % 4 == 3)
                {
                    dstData[i] = 0;
                }
                else
                {
                    dstData[i] = (char)(srcData[i] * 255 + 0.5f);
                }
            }

            auto primIdData = reinterpret_cast<int*>(dstBuffer);
            for (size_t i = 0; i < dstBufferSize / sizeof(GfVec4f); ++i)
            {
                primIdData[i] -= 1;
            }
        }
    }
    else if (m_format == HdFormatInt32) {
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
}

bool HdRprApiAov::GetData(void* dstBuffer, size_t dstBufferSize) {
    auto getBuffer = dstBuffer;
    SetUpSizeAndBuffer(getBuffer, dstBufferSize);
    if (GetDataImpl(getBuffer, dstBufferSize)) {
        ApplyFormatToOutput(getBuffer, dstBuffer, dstBufferSize);
        return true;
    }

    return false;
}

bool HdRprApiAov::GetUpscaledData(void* dstBuffer, size_t dstBufferSize, rif::Context* rifContext) {
    auto getBuffer = dstBuffer;
    SetUpSizeAndBuffer(getBuffer, dstBufferSize);
    if (GetUpscaledDataImpl(dstBuffer, dstBufferSize, rifContext)) {
        ApplyFormatToOutput(getBuffer, dstBuffer, dstBufferSize);
        return true;
    }

    return false;
}

void HdRprApiAov::Resize(int width, int height, HdFormat format, bool oddWidth) {
    m_width = width;
    m_height = height;
    m_oddWidth = oddWidth;

    if (m_format != format) {
        m_format = format;
        m_dirtyBits |= ChangeTracker::DirtyFormat;
    }

    if (m_aov && m_aov->Resize(width, height)) {
        m_dirtyBits |= ChangeTracker::DirtySize;
    }

    if (m_resolved && m_resolved->Resize(width, height)) {
        m_dirtyBits |= ChangeTracker::DirtyFormat;
    }
}

void HdRprApiAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_dirtyBits & ChangeTracker::DirtyFormat) {
        OnFormatChange(rifContext);
    }
    if (m_dirtyBits & ChangeTracker::DirtySize) {
        OnSizeChange(rifContext);
    }
    m_dirtyBits = ChangeTracker::Clean;

    if (m_filter) {
        m_filter->Update();
    }

    if (m_upscaleFilter) {
        m_upscaleFilter->Update();
    }
}

HdRprApiFramebuffer* HdRprApiAov::GetResolvedFb() {
    return (m_resolved ? m_resolved : m_aov).get();
}

void HdRprApiAov::OnFormatChange(rif::Context* rifContext) {
    m_filter = nullptr;
    if (rifContext && m_format != HdFormatFloat32Vec4) {
        m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, rifContext);
        m_filter->SetParam("interpOperator", (int) RIF_IMAGE_INTERPOLATION_NEAREST);

        // Reset inputs
        m_dirtyBits |= ChangeTracker::DirtySize;
    }
}

void HdRprApiAov::OnSizeChange(rif::Context* rifContext) {
    if (m_filter) {
        auto fbDesc = m_aov->GetDesc();
        m_filter->Resize(fbDesc.fb_width, fbDesc.fb_height);
        m_filter->SetInput(rif::Color, GetResolvedFb());
        m_filter->SetOutput(rif::Image::GetDesc(fbDesc.fb_width, fbDesc.fb_height, m_format));
        m_filter->SetParam("outSize", GfVec2i(fbDesc.fb_width, fbDesc.fb_height));
    }
    if (m_upscaleFilter) {
        auto fbDesc = m_aov->GetDesc();
        m_upscaleFilter->Resize(fbDesc.fb_width, fbDesc.fb_height);
        if (m_filter) {
            m_upscaleFilter->SetInput(rif::Color, m_filter->GetOutput());
        }
        else {
            m_upscaleFilter->SetInput(rif::Color, GetResolvedFb());
        }
        m_upscaleFilter->SetOutput(rif::Image::GetDesc(fbDesc.fb_width * 2, fbDesc.fb_height * 2, m_format));
    }
}

HdRprApiColorAov::HdRprApiColorAov(HdFormat format, std::shared_ptr<HdRprApiAov> rawColorAov, rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata)
    : HdRprApiAov(HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kColorAlpha), true), 0, 0, format)
    , m_retainedRawColor(std::move(rawColorAov)) {
}

void HdRprApiColorAov::SetFilter(Filter filter, bool enable) {
    bool isFilterEnabled = m_enabledFilters & filter;
    if (enable != isFilterEnabled) {
        if (enable) {
            m_enabledFilters |= filter;
        } else {
            m_enabledFilters &= ~filter;
        }
        m_isEnabledFiltersDirty = true;
    }
}

void HdRprApiColorAov::SetOpacityAov(std::shared_ptr<HdRprApiAov> opacity) {
    if (m_retainedOpacity != opacity) {
        m_retainedOpacity = opacity;
        SetFilter(kFilterComposeOpacity, CanComposeAlpha());
    }
}

void HdRprApiColorAov::InitAIDenoise(
    std::shared_ptr<HdRprApiAov> albedo,
    std::shared_ptr<HdRprApiAov> normal,
    std::shared_ptr<HdRprApiAov> linearDepth) {
    if (m_enabledFilters & kFilterAIDenoise) {
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

    m_denoiseFilterType = kFilterAIDenoise;
}

void HdRprApiColorAov::InitEAWDenoise(
    std::shared_ptr<HdRprApiAov> albedo,
    std::shared_ptr<HdRprApiAov> normal,
    std::shared_ptr<HdRprApiAov> linearDepth,
    std::shared_ptr<HdRprApiAov> objectId,
    std::shared_ptr<HdRprApiAov> worldCoordinate) {
    if (m_enabledFilters & kFilterEAWDenoise) {
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

    m_denoiseFilterType = kFilterEAWDenoise;
}

void HdRprApiColorAov::DeinitDenoise(rif::Context* rifContext) {
    for (auto& retainedInput : m_retainedDenoiseInputs) {
        retainedInput = nullptr;
    }

    m_denoiseFilterType = kFilterNone;
}

void HdRprApiColorAov::SetDenoise(bool enable, HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_denoiseFilterType != kFilterNone) {
        SetFilter(m_denoiseFilterType, enable);
        SetFilter(m_denoiseFilterType == kFilterAIDenoise ? kFilterEAWDenoise : kFilterAIDenoise, false);
    } else {
        SetFilter(kFilterAIDenoise, false);
        SetFilter(kFilterEAWDenoise, false);
    }

    SetFilter(kFilterResample, m_format != HdFormatFloat32Vec4);

    Update(rprApi, rifContext);
}

void HdRprApiColorAov::SetTonemap(TonemapParams const& params) {
    bool isTonemapEnabled = m_enabledFilters & kFilterTonemap;
    bool tonemapEnableDirty = params.enable != isTonemapEnabled;

    SetFilter(kFilterTonemap, params.enable);

    if (m_tonemap != params) {
        m_tonemap = params;

        if (!tonemapEnableDirty && isTonemapEnabled) {
            if (m_mainFilterType == kFilterTonemap) {
                SetTonemapFilterParams(m_filter.get());
            } else {
                for (auto& entry : m_auxFilters) {
                    if (entry.first == kFilterTonemap) {
                        SetTonemapFilterParams(entry.second.get());
                        break;
                    }
                }
            }
        }
    }
}

void HdRprApiColorAov::SetGamma(GammaParams const& params) {
    bool isGammaEnabled = m_enabledFilters & kFilterGamma;
    bool gammaEnableDirty = params.enable != isGammaEnabled;
    
    SetFilter(kFilterGamma, params.enable);

    if (m_gamma != params) {
        m_gamma = params;

        if (!gammaEnableDirty && isGammaEnabled) {
            if (m_mainFilterType == kFilterGamma) {
                m_filter.get()->SetParam("gamma", m_gamma.value);
            } else {
                for (auto& entry : m_auxFilters) {
                    if (entry.first == kFilterGamma) {
                        entry.second.get()->SetParam("gamma", m_gamma.value);
                        break;
                    }
                }
            }
        }
    }
}

void HdRprApiColorAov::SetTonemapFilterParams(rif::Filter* filter) {
    filter->SetParam("exposureTime", m_tonemap.exposureTime);
    filter->SetParam("sensitivity", m_tonemap.sensitivity);
    filter->SetParam("fstop", m_tonemap.fstop);
    filter->SetParam("gamma", m_tonemap.gamma);
}

bool HdRprApiColorAov::CanComposeAlpha() {
    // Compositing alpha into framebuffer with less than 4 components is a no-op
    return HdGetComponentCount(m_format) == 4 && m_retainedOpacity;
}

void HdRprApiColorAov::Resize(int width, int height, HdFormat format, bool oddWidth) {
    if (m_width != width || m_height != height) {
        m_dirtyBits |= ChangeTracker::DirtySize;
    }

    HdRprApiAov::Resize(width, height, format, oddWidth);
}

void HdRprApiColorAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_dirtyBits & ChangeTracker::DirtyFormat) {
        OnFormatChange(rifContext);
    }

    if (m_isEnabledFiltersDirty) {
        m_isEnabledFiltersDirty = false;
        if (!rifContext && m_enabledFilters != kFilterNone) {
            TF_WARN("Can not enable %#x filters: rifContext required", m_enabledFilters);
            m_enabledFilters = kFilterNone;
        }

        // Reuse the previously created filters
        std::vector<std::pair<Filter, std::unique_ptr<rif::Filter>>> filterPool = std::move(m_auxFilters);
        if (m_filter) {
            filterPool.emplace_back(m_mainFilterType, std::move(m_filter));
        }

        if ((m_enabledFilters & kFilterAIDenoise) ||
            (m_enabledFilters & kFilterEAWDenoise) ||
            (m_enabledFilters & kFilterComposeOpacity) ||
            (m_enabledFilters & kFilterTonemap) ||
            (m_enabledFilters & kFilterGamma)) {

            auto addFilter = [this, &filterPool](Filter type, std::function<std::unique_ptr<rif::Filter>()> filterCreator) {
                std::unique_ptr<rif::Filter> filter;

                auto it = std::find_if(filterPool.begin(), filterPool.end(), [type](auto& entry) { return type == entry.first; });
                if (it != filterPool.end()) {
                    filter = std::move(it->second);
                } else {
                    filter = filterCreator();
                }

                if (m_filter) {
                    m_auxFilters.emplace_back(m_mainFilterType, std::move(m_filter));
                }

                m_filter = std::move(filter);
                m_mainFilterType = type;
            };

            if (m_enabledFilters & kFilterTonemap) {
                addFilter(kFilterTonemap,
                    [rifContext]() {
                        return rif::Filter::CreateCustom(RIF_IMAGE_FILTER_PHOTO_LINEAR_TONEMAP, rifContext);
                    }
                );
            }

            if (m_enabledFilters & kFilterGamma) {
                addFilter(kFilterGamma,
                    [rifContext]() {
                        return rif::Filter::CreateCustom(RIF_IMAGE_FILTER_GAMMA_CORRECTION, rifContext);
                    }
                );
            }

            if ((m_enabledFilters & kFilterAIDenoise) ||
                (m_enabledFilters & kFilterEAWDenoise)) {
                auto type = (m_enabledFilters & kFilterAIDenoise) ? kFilterAIDenoise : kFilterEAWDenoise;
                addFilter(type,
                    [this, rifContext]() {
                        auto denoiseFilterType = (m_enabledFilters & kFilterAIDenoise) ? rif::FilterType::AIDenoise : rif::FilterType::EawDenoise;
                        auto fbDesc = m_retainedRawColor->GetAovFb()->GetDesc();
                        auto filter = rif::Filter::Create(denoiseFilterType, rifContext, fbDesc.fb_width, fbDesc.fb_height);
                        return filter;
                    }
                );
            }

            if (m_enabledFilters & kFilterComposeOpacity) {
                addFilter(kFilterComposeOpacity,
                    [rifContext]() {
                        auto filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_USER_DEFINED, rifContext);
                        auto opacityComposingKernelCode = std::string(R"(
                            int2 coord;
                            GET_COORD_OR_RETURN(coord, GET_BUFFER_SIZE(inputImage));
                            vec4 alpha = ReadPixelTyped(alphaImage, coord.x, coord.y);
                            vec4 color = ReadPixelTyped(inputImage, coord.x, coord.y) * alpha.x;
                            WritePixelTyped(outputImage, coord.x, coord.y, make_vec4(color.x, color.y, color.z, alpha.x));
                        )");
                        filter->SetParam("code", opacityComposingKernelCode);
                        return filter;
                    }
                );
            }
        } else if (m_enabledFilters & kFilterResample) {
            m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, rifContext);
            m_filter->SetParam("interpOperator", (int) RIF_IMAGE_INTERPOLATION_NEAREST);
            m_mainFilterType = kFilterResample;
        }

        // Signal to update inputs
        m_dirtyBits |= ChangeTracker::DirtySize;
    }

    if (m_dirtyBits & ChangeTracker::DirtySize) {
        OnSizeChange(rifContext);
    }
    m_dirtyBits = ChangeTracker::Clean;

    for (auto& auxFilter : m_auxFilters) {
        auxFilter.second->Update();
    }
    if (m_filter) {
        m_filter->Update();
    }
    if (m_upscaleFilter) {
        if (m_filter) {
            m_upscaleFilter->SetInput(rif::Color, m_filter->GetOutput());
        }
        else {
            if (auto resolvedRawColorFb = m_retainedRawColor->GetResolvedFb()) {
                m_upscaleFilter->SetInput(rif::Color, resolvedRawColorFb);
            }
        }
        m_upscaleFilter->SetOutput(rif::Image::GetDesc(m_width * 2, m_height * 2, m_format));
        m_upscaleFilter->Update();
    }
}

bool HdRprApiColorAov::GetData(void* dstBuffer, size_t dstBufferSize) {
    if (!m_filter) {
        if (auto resolvedRawColorFb = m_retainedRawColor->GetResolvedFb()) {
            return resolvedRawColorFb->GetData(dstBuffer, dstBufferSize);
        } else {
            return false;
        }
    } else {
        return HdRprApiAov::GetData(dstBuffer, dstBufferSize);
    }
}

void HdRprApiColorAov::Resolve() {
    HdRprApiAov::Resolve();

    for (auto& auxFilter : m_auxFilters) {
        auxFilter.second->Resolve();
    }
}

void HdRprApiColorAov::OnFormatChange(rif::Context* rifContext) {
    SetFilter(kFilterResample, m_format != HdFormatFloat32Vec4);
    SetFilter(kFilterComposeOpacity, CanComposeAlpha());
    m_dirtyBits |= ChangeTracker::DirtySize;
}

template <typename T>
void HdRprApiColorAov::ResizeFilter(int width, int height, Filter filterType, rif::Filter* filter, T input) {
    filter->Resize(width, height);
    filter->SetInput(rif::Color, input);
    filter->SetOutput(rif::Image::GetDesc(width, height, m_format));

    if (filterType == kFilterAIDenoise || filterType == kFilterEAWDenoise) {
        for (int i = 0; i < rif::MaxInput; ++i) {
            if (auto retainedInput = m_retainedDenoiseInputs[i].get()) {
                filter->SetInput(static_cast<rif::FilterInputType>(i), retainedInput->GetResolvedFb());
            }
        }
    } else if (filterType == kFilterComposeOpacity) {
        filter->SetInput("alphaImage", m_retainedOpacity->GetResolvedFb());
    } else if (filterType == kFilterResample) {
        filter->SetParam("outSize", GfVec2i(width, height));
    } else if (filterType == kFilterTonemap) {
        SetTonemapFilterParams(filter);
    } else if (filterType == kFilterGamma) {
        filter->SetParam("gamma", m_gamma.value);
    }
}

void HdRprApiColorAov::OnSizeChange(rif::Context* rifContext) {
    auto fbDesc = m_retainedRawColor->GetAovFb()->GetDesc();

    if (!m_filter) {
        return;
    }

    if (m_auxFilters.empty()) {
        ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, m_mainFilterType, m_filter.get(), m_retainedRawColor->GetResolvedFb());
    } else {
        // Ideally we would use "Filter combining" functionality, but it does not work with user-defined filter
        // So we attach each filter separately
        
        auto filter = m_auxFilters.front().second.get();
        ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, m_auxFilters.front().first, filter, m_retainedRawColor->GetResolvedFb());
        for (int i = 1; i < m_auxFilters.size(); ++i) {
            auto filterInput = m_auxFilters[i - 1].second->GetOutput();
            ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, m_auxFilters[i].first, m_auxFilters[i].second.get(), filterInput);
        }
        ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, m_mainFilterType, m_filter.get(), m_auxFilters.back().second->GetOutput());
    }

    if (m_upscaleFilter) {
        m_upscaleFilter->Resize(fbDesc.fb_width, fbDesc.fb_height);
        m_upscaleFilter->SetOutput(rif::Image::GetDesc(fbDesc.fb_width * 2, fbDesc.fb_height * 2, m_format));
    }
}

HdRprApiNormalAov::HdRprApiNormalAov(
    int width, int height, HdFormat format,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext)
    : HdRprApiAov(RPR_AOV_SHADING_NORMAL, width, height, format, rprContext, rprContextMetadata, rif::Filter::CreateCustom(RIF_IMAGE_FILTER_REMAP_RANGE, rifContext)) {
    if (!rifContext) {
        RPR_THROW_ERROR_MSG("Can not create normal AOV: RIF context required");
    }

    m_filter->SetParam("srcRangeAuto", 0);
    m_filter->SetParam("dstLo", -1.0f);
    m_filter->SetParam("dstHi", 1.0f);
}

void HdRprApiNormalAov::OnFormatChange(rif::Context* rifContext) {
    m_dirtyBits |= ChangeTracker::DirtySize;
}

void HdRprApiNormalAov::OnSizeChange(rif::Context* rifContext) {
    auto fbDesc = m_aov->GetDesc();
    m_filter->Resize(fbDesc.fb_width, fbDesc.fb_height);
    m_filter->SetInput(rif::Color, GetResolvedFb());
    m_filter->SetOutput(rif::Image::GetDesc(fbDesc.fb_width, fbDesc.fb_height, m_format));

    if (m_upscaleFilter) {
        m_upscaleFilter->Resize(fbDesc.fb_width, fbDesc.fb_height);
        m_upscaleFilter->SetInput(rif::Color, m_filter->GetOutput());
        m_upscaleFilter->SetOutput(rif::Image::GetDesc(fbDesc.fb_width * 2, fbDesc.fb_height * 2, m_format));
    }
}

void HdRprApiComputedAov::Resize(int width, int height, HdFormat format, bool oddWidth) {
    if (m_format != format) {
        m_format = format;
        m_dirtyBits |= ChangeTracker::DirtyFormat;
    }

    if (m_width != width || m_height != height) {
        m_dirtyBits |= ChangeTracker::DirtySize;
    }

    HdRprApiAov::Resize(width, height, format, oddWidth);
}

HdRprApiDepthAov::HdRprApiDepthAov(
    int width, int height, HdFormat format,
    std::shared_ptr<HdRprApiAov> worldCoordinateAov,
    std::shared_ptr<HdRprApiAov> opacityAov,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext)
    : HdRprApiComputedAov(HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kNdcDepth), true), width, height, format)
    , m_retainedWorldCoordinateAov(worldCoordinateAov)
    , m_retainedOpacityAov(opacityAov){

    if (!rifContext) {
        RPR_THROW_ERROR_MSG("Can not create depth AOV: RIF context required");
    }

    m_retainedNDCFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_NDC_DEPTH, rifContext);
    m_ndcFilter = m_retainedNDCFilter.get();

    m_filter= rif::Filter::CreateCustom(RIF_IMAGE_FILTER_USER_DEFINED, rifContext);

    auto opacityComposingKernelCode = std::string(R"(
                            int2 coord;
                            GET_COORD_OR_RETURN(coord, GET_BUFFER_SIZE(inputImage));
                            vec4 alpha = ReadPixelTyped(alphaImage, coord.x, coord.y);
                            vec4 color = ReadPixelTyped(inputImage, coord.x, coord.y);
                            if (alpha.x == 0) {
                                color = make_vec4(1.f, 1.f, 1.f, 1.f);
                            }
                            WritePixelTyped(outputImage, coord.x, coord.y, color);
                        )");
    m_filter->SetParam("code", opacityComposingKernelCode);
    m_opacityFilter = m_filter.get();
    m_remapFilter = nullptr;

#if PXR_VERSION >= 2002
    m_retainedOpacityFilter = std::move(m_filter);

    m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_REMAP_RANGE, rifContext);
    m_filter->SetParam("srcRangeAuto", 0);
    m_filter->SetParam("srcLo", -1.0f);
    m_filter->SetParam("srcHi", 1.0f);
    m_filter->SetParam("dstLo", 0.0f);
    m_filter->SetParam("dstHi", 1.0f);
    m_remapFilter = m_filter.get();
#endif
}

void HdRprApiDepthAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_dirtyBits & ChangeTracker::DirtyFormat ||
        m_dirtyBits & ChangeTracker::DirtySize) {

        m_ndcFilter->SetInput(rif::Color, m_retainedWorldCoordinateAov->GetResolvedFb());
        m_ndcFilter->SetOutput(rif::Image::GetDesc(m_width, m_height, m_format));
        m_opacityFilter->SetInput(rif::Color, m_ndcFilter->GetOutput());
        m_opacityFilter->SetInput("alphaImage", m_retainedOpacityAov->GetResolvedFb());
        m_opacityFilter->SetOutput(rif::Image::GetDesc(m_width, m_height, m_format));
        if (m_remapFilter) {
            m_remapFilter->SetInput(rif::Color, m_opacityFilter->GetOutput());
            m_remapFilter->SetOutput(rif::Image::GetDesc(m_width, m_height, m_format));
        }
    }
    m_dirtyBits = ChangeTracker::Clean;

    auto viewProjectionMatrix = rprApi->GetCameraViewMatrix() * rprApi->GetCameraProjectionMatrix();
    m_ndcFilter->SetParam("viewProjMatrix", GfMatrix4f(viewProjectionMatrix.GetTranspose()));

    m_ndcFilter->Update();
    m_opacityFilter->Update();
    if (m_remapFilter) {
        m_remapFilter->Update();
    }
    if (m_upscaleFilter) {
        m_upscaleFilter->SetInput(rif::Color, m_filter->GetOutput());
        m_upscaleFilter->SetOutput(rif::Image::GetDesc(m_width * 2, m_height * 2, m_format));
        m_upscaleFilter->Update();
    }
}

void HdRprApiDepthAov::Resolve() {
    if (m_ndcFilter) {
        m_ndcFilter->Resolve();
    }
    if (m_opacityFilter) {
        m_opacityFilter->Resolve();
    }
    if (m_remapFilter) {
        m_remapFilter->Resolve();
    }
    if (m_upscaleFilter) {
        m_upscaleFilter->Resolve();
    }
}

HdRprApiIdMaskAov::HdRprApiIdMaskAov(
    HdRprAovDescriptor const& aovDescriptor, std::shared_ptr<HdRprApiAov> const& baseIdAov,
    int width, int height, HdFormat format,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext)
    : HdRprApiComputedAov(aovDescriptor, width, height, format)
    , m_baseIdAov(baseIdAov) {
    if (!rifContext) {
        RPR_THROW_ERROR_MSG("Can not create id mask AOV: RIF context required");
    }

    m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_USER_DEFINED, rifContext);
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
    m_filter->SetParam("code", colorizeIdKernelCode);
}

void HdRprApiIdMaskAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_dirtyBits & ChangeTracker::DirtyFormat ||
        m_dirtyBits & ChangeTracker::DirtySize) {
        m_filter->SetInput(rif::Color, m_baseIdAov->GetResolvedFb());
        m_filter->SetOutput(rif::Image::GetDesc(m_width, m_height, m_format));
    }
    m_dirtyBits = ChangeTracker::Clean;

    if (m_filter) {
        m_filter->Update();
    }
}

HdRprApiScCompositeAOV::HdRprApiScCompositeAOV(int width, int height, HdFormat format,
    std::shared_ptr<HdRprApiAov> rawColorAov,
    std::shared_ptr<HdRprApiAov> opacityAov,
    std::shared_ptr<HdRprApiAov> scAov,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext)
    : HdRprApiAov(HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kScTransparentBackground), true), width, height, format)
    , m_retainedRawColorAov(rawColorAov)
    , m_retainedOpacityAov(opacityAov)
    , m_retainedScAov(scAov)
{
} 

bool HdRprApiScCompositeAOV::GetDataImpl(void* dstBuffer, size_t dstBufferSize) {
    if (m_tempColorBuffer.size() < dstBufferSize / sizeof(GfVec4f)) {
        m_tempColorBuffer.resize(dstBufferSize / sizeof(GfVec4f));
    }
    if (m_tempOpacityBuffer.size() < dstBufferSize / sizeof(GfVec4f)) {
        m_tempOpacityBuffer.resize(dstBufferSize / sizeof(GfVec4f));
    }
    if (m_tempScBuffer.size() < dstBufferSize / sizeof(GfVec4f)) {
        m_tempScBuffer.resize(dstBufferSize / sizeof(GfVec4f));
    }

    if (!m_retainedRawColorAov->GetDataImpl((void*)m_tempColorBuffer.data(), dstBufferSize)) {
        return false;
    }
    if (!m_retainedOpacityAov->GetDataImpl((void*)m_tempOpacityBuffer.data(), dstBufferSize)) {
        return false;
    }
    if (!m_retainedScAov->GetDataImpl((void*)m_tempScBuffer.data(), dstBufferSize)) {
        return false;
    }

    auto dstValue = reinterpret_cast<GfVec4f*>(dstBuffer);

    // On this stage format is always HdFormatFloat32Vec4
    #pragma omp parallel for
    for (int i = 0; i < dstBufferSize / sizeof(GfVec4f); i++) 
    {  
        float opacity = m_tempOpacityBuffer[i][0];
        float sc = m_tempScBuffer[i][0];
        constexpr float OneMinusEpsilon = 1.0f - 1e-5f;

        if (opacity > OneMinusEpsilon)
        {
            dstValue[i] = { m_tempColorBuffer[i][0], m_tempColorBuffer[i][1], m_tempColorBuffer[i][2], opacity };
		}
        else
        {
            // Add shadows from the shadow catcher to the final image + Make the background transparent;
            dstValue[i] = { 0.0f, 0.0f, 0.0f, sc };         
        }
    }

    return true;
}

bool HdRprApiScCompositeAOV::InitUpscaleFilter(rif::Context* rifContext) {
    if (!m_retainedRawColorAov || !m_retainedOpacityAov || !m_retainedScAov) {
        return false;
    }
    return m_retainedRawColorAov->InitUpscaleFilter(rifContext)
        && m_retainedOpacityAov->InitUpscaleFilter(rifContext)
        && m_retainedScAov->InitUpscaleFilter(rifContext);
}

bool HdRprApiScCompositeAOV::GetUpscaledDataImpl(void* dstBuffer, size_t dstBufferSize, rif::Context* rifContext) {
    if (m_tempColorBuffer.size() < dstBufferSize / sizeof(GfVec4f)) {
        m_tempColorBuffer.resize(dstBufferSize / sizeof(GfVec4f));
    }
    if (m_tempOpacityBuffer.size() < dstBufferSize / sizeof(GfVec4f)) {
        m_tempOpacityBuffer.resize(dstBufferSize / sizeof(GfVec4f));
    }
    if (m_tempScBuffer.size() < dstBufferSize / sizeof(GfVec4f)) {
        m_tempScBuffer.resize(dstBufferSize / sizeof(GfVec4f));
    }

    if (!m_retainedRawColorAov->GetUpscaledDataImpl((void*)m_tempColorBuffer.data(), dstBufferSize, rifContext)) {
        return false;
    }
    if (!m_retainedOpacityAov->GetUpscaledDataImpl((void*)m_tempOpacityBuffer.data(), dstBufferSize, rifContext)) {
        return false;
    }
    if (!m_retainedScAov->GetUpscaledDataImpl((void*)m_tempScBuffer.data(), dstBufferSize, rifContext)) {
        return false;
    }

    auto dstValue = reinterpret_cast<GfVec4f*>(dstBuffer);

    // On this stage format is always HdFormatFloat32Vec4
    #pragma omp parallel for
    for (int i = 0; i < dstBufferSize / sizeof(GfVec4f); i++)
    {
        float opacity = m_tempOpacityBuffer[i][0];
        float sc = m_tempScBuffer[i][0];
        constexpr float OneMinusEpsilon = 1.0f - 1e-5f;

        if (opacity > OneMinusEpsilon)
        {
            dstValue[i] = { m_tempColorBuffer[i][0], m_tempColorBuffer[i][1], m_tempColorBuffer[i][2], opacity };
        }
        else
        {
            // Add shadows from the shadow catcher to the final image + Make the background transparent;
            dstValue[i] = { 0.0f, 0.0f, 0.0f, sc };
        }
    }

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
