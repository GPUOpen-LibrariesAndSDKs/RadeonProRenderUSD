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
#include "cpuDenoiser.h"
#include "rifcpp/rifError.h"

#include <algorithm>
#include <exception>
#include <limits>

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
    , m_format(format) {
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

    filter->SetParam("interpOperator", (int)RIF_IMAGE_INTERPOLATION_NEAREST);
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

bool HdRprApiAov::GetData(void* dstBuffer, size_t dstBufferSize) {
    auto getBuffer = dstBuffer;
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
    if (GetDataImpl(getBuffer, dstBufferSize)) {
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

        return true;
    }

    return false;
}

void HdRprApiAov::Resize(int width, int height, HdFormat format) {
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
}

HdRprApiFramebuffer* HdRprApiAov::GetResolvedFb() {
    return (m_resolved ? m_resolved : m_aov).get();
}

void HdRprApiAov::OnFormatChange(rif::Context* rifContext) {
    m_filter = nullptr;
    if (rifContext && m_format != HdFormatFloat32Vec4) {
        m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, rifContext);
        m_filter->SetParam("interpOperator", (int)RIF_IMAGE_INTERPOLATION_NEAREST);

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
}

HdRprApiColorAov::HdRprApiColorAov(HdFormat format, std::shared_ptr<HdRprApiAov> rawColorAov, rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata)
    : HdRprApiAov(HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kColorAlpha), true), format)
    , m_retainedRawColor(std::move(rawColorAov))
    , m_isNorthstar(rprContextMetadata.pluginType == kPluginNorthstar) {

}

HdRprApiColorAov::~HdRprApiColorAov() {
    // Detach consumers before destroying the CPU-denoised RIF source image.
    m_filter.reset();
    m_auxFilters.clear();
}

bool HdRprApiColorAov::IsDenoisingAvailable() {
    return HdRprCpuDenoiser::IsAvailable();
}

void HdRprApiColorAov::SetDenoising(bool enable, uint32_t minSamples,
                                    std::shared_ptr<HdRprApiAov> albedo,
                                    std::shared_ptr<HdRprApiAov> normal) {
    if (enable && m_isNorthstar && !IsDenoisingAvailable() && !m_denoisingUnavailableWarned) {
        TF_WARN("Northstar denoising requires optional Open Image Denoise 2 support; using raw beauty");
        m_denoisingUnavailableWarned = true;
    }
    enable = enable && m_isNorthstar && IsDenoisingAvailable();
    minSamples = std::max(uint32_t(1), minSamples);
    if (!enable) {
        albedo.reset();
        normal.reset();
    }
    if (enable != m_denoisingEnabled || minSamples != m_denoisingMinSamples ||
        albedo != m_denoisingAlbedo || normal != m_denoisingNormal) {
        m_denoisingEnabled = enable;
        m_denoisingMinSamples = minSamples;
        m_denoisingAlbedo = std::move(albedo);
        m_denoisingNormal = std::move(normal);
        m_dirtyBits |= ChangeTracker::DirtySize;
        ResetDenoising();
        if (!enable) {
            m_cpuDenoiser.reset();
            std::vector<float>().swap(m_denoisingColorBuffer);
            std::vector<float>().swap(m_denoisingAlbedoBuffer);
            std::vector<float>().swap(m_denoisingNormalBuffer);
            std::vector<float>().swap(m_denoisedColorBuffer);
        }
    }
}

void HdRprApiColorAov::SetDenoisingSampleCount(uint32_t samples) {
    m_denoisingSampleCount = samples;
}

void HdRprApiColorAov::ResetDenoising() {
    m_denoisingOutputValid = false;
    m_denoisingFailed = false;
    m_denoisingSampleCount = 0;
}

void HdRprApiColorAov::SetFilter(Filter filter, bool enable) {
    bool isFilterEnabled = m_enabledFilters & filter;
    if (enable != isFilterEnabled) {
        if (enable) {
            m_enabledFilters |= filter;
        }
        else {
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

void HdRprApiColorAov::SetTonemap(TonemapParams const& params) {
    bool isTonemapEnabled = m_enabledFilters & kFilterTonemap;
    bool tonemapEnableDirty = params.enable != isTonemapEnabled;

    SetFilter(kFilterTonemap, params.enable);

    if (m_tonemap != params) {
        m_tonemap = params;

        if (!tonemapEnableDirty && isTonemapEnabled) {
            if (m_mainFilterType == kFilterTonemap) {
                SetTonemapFilterParams(m_filter.get());
            }
            else {
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
            }
            else {
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

void HdRprApiColorAov::Resize(int width, int height, HdFormat format) {
    if (m_width != width || m_height != height) {
        m_width = width;
        m_height = height;
        m_dirtyBits |= ChangeTracker::DirtySize;
    }

    HdRprApiAov::Resize(width, height, format);
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

        if ((m_enabledFilters & kFilterComposeOpacity) ||
            (m_enabledFilters & kFilterTonemap) ||
            (m_enabledFilters & kFilterGamma)) {

            auto addFilter = [this, &filterPool](Filter type, std::function<std::unique_ptr<rif::Filter>()> filterCreator) {
                std::unique_ptr<rif::Filter> filter;

                auto it = std::find_if(filterPool.begin(), filterPool.end(), [type](auto& entry) { return type == entry.first; });
                if (it != filterPool.end()) {
                    filter = std::move(it->second);
                }
                else {
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
        }
        else if (m_enabledFilters & kFilterResample) {
            m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, rifContext);
            m_filter->SetParam("interpOperator", (int)RIF_IMAGE_INTERPOLATION_NEAREST);
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
}

bool HdRprApiColorAov::GetData(void* dstBuffer, size_t dstBufferSize) {
    if (m_denoisingEnabled && m_denoisingOutputValid && !m_filter) {
        const size_t bytes = m_denoisedColorBuffer.size() * sizeof(float);
        if (!dstBuffer || m_format != HdFormatFloat32Vec4 || dstBufferSize < bytes) return false;
        std::memcpy(dstBuffer, m_denoisedColorBuffer.data(), bytes);
        return true;
    }
    if (m_denoisingEnabled && !m_denoisingOutputValid) {
        auto rawColor = m_retainedRawColor->GetResolvedFb();
        return rawColor && rawColor->GetData(dstBuffer, dstBufferSize);
    }
    if (!m_filter) {
        if (auto resolvedRawColorFb = m_retainedRawColor->GetResolvedFb()) {
            return resolvedRawColorFb->GetData(dstBuffer, dstBufferSize);
        }
        else {
            return false;
        }
    }
    else {
        return HdRprApiAov::GetData(dstBuffer, dstBufferSize);
    }
}

void HdRprApiColorAov::Resolve() {
    if (m_denoisingEnabled) {
        ResolveDenoising();
    }
    HdRprApiAov::Resolve();

    for (auto& auxFilter : m_auxFilters) {
        auxFilter.second->Resolve();
    }
}

void HdRprApiColorAov::ResolveDenoising() {
    // Raw AOVs have already been resolved before computed color AOVs. Cache only
    // completed denoising; ResetDenoising invalidates this on scene/camera edits.
    if (m_denoisingOutputValid && !m_denoisingFailed &&
        m_denoisingSampleCount >= m_denoisingMinSamples &&
        m_denoisingOutputSampleCount == m_denoisingSampleCount) return;
    m_denoisingOutputValid = false;
    auto rawColor = m_retainedRawColor->GetResolvedFb();
    if (!rawColor) return;
    const auto desc = rawColor->GetDesc();
    const size_t width = desc.fb_width, height = desc.fb_height;
    if (!width || !height ||
        width > std::numeric_limits<size_t>::max() / height / 4 / sizeof(float)) return;
    const size_t count = width * height * 4;
    const size_t bytes = count * sizeof(float);
    try {
        m_denoisingColorBuffer.resize(count);
        if (!rawColor->GetData(m_denoisingColorBuffer.data(), bytes)) return;
        m_denoisedColorBuffer = m_denoisingColorBuffer;
        if (!m_denoisingFailed && m_denoisingSampleCount >= m_denoisingMinSamples) {
            auto readGuide = [=](std::shared_ptr<HdRprApiAov> const& aov,
                                 std::vector<float>& buffer) -> bool {
                if (!aov) return true;
                auto framebuffer = aov->GetResolvedFb();
                if (!framebuffer || !aov->GetAovFb()) return false;
                const auto guideDesc = framebuffer->GetDesc();
                if (guideDesc.fb_width != width || guideDesc.fb_height != height) return false;
                // Normals are marked single-sampled in the registry. Refresh the
                // underlying float4 guide at the current accumulated sample count.
                aov->GetAovFb()->Resolve(framebuffer);
                buffer.resize(count);
                return framebuffer->GetData(buffer.data(), bytes);
            };
            std::string error;
            if (!readGuide(m_denoisingAlbedo, m_denoisingAlbedoBuffer) ||
                !readGuide(m_denoisingNormal, m_denoisingNormalBuffer)) {
                error = "Cannot read denoising guide AOV";
            } else {
                if (!m_cpuDenoiser) m_cpuDenoiser.reset(new HdRprCpuDenoiser);
                if (!m_cpuDenoiser->Execute(m_denoisingColorBuffer.data(),
                        m_denoisingAlbedo ? m_denoisingAlbedoBuffer.data() : nullptr,
                        m_denoisingNormal ? m_denoisingNormalBuffer.data() : nullptr,
                        m_denoisedColorBuffer.data(), width, height, &error) && error.empty()) {
                    error = "Open Image Denoise failed";
                }
            }
            if (!error.empty()) {
                TF_WARN("Northstar denoising failed: %s; using raw beauty until accumulation resets", error.c_str());
                m_denoisingFailed = true;
                m_denoisedColorBuffer = m_denoisingColorBuffer;
            }
        }
        // Only post-processing needs a RIF image. Float4 output without display
        // filters can use the CPU result directly, including CPU-only rendering.
        if (m_denoisedImage) {
            void* mapped = nullptr;
            RIF_ERROR_CHECK_THROW(rifImageMap(m_denoisedImage->GetHandle(), RIF_IMAGE_MAP_WRITE, &mapped),
                                  "Cannot map denoised beauty image");
            std::memcpy(mapped, m_denoisedColorBuffer.data(), bytes);
            RIF_ERROR_CHECK_THROW(rifImageUnmap(m_denoisedImage->GetHandle(), mapped),
                                  "Cannot unmap denoised beauty image");
        }
        m_denoisingOutputSampleCount = m_denoisingSampleCount;
        m_denoisingOutputValid = true;
    } catch (std::exception const& e) {
        if (!m_denoisingFailed) TF_WARN("Northstar denoising failed: %s; using raw beauty", e.what());
        m_denoisingFailed = true;
    }
}

void HdRprApiColorAov::OnFormatChange(rif::Context* rifContext) {
    SetFilter(kFilterResample, m_format != HdFormatFloat32Vec4);
    SetFilter(kFilterComposeOpacity, CanComposeAlpha());
    m_dirtyBits |= ChangeTracker::DirtySize;
}

template <typename T>
void HdRprApiColorAov::ResizeFilter(int width, int height, Filter filterType, rif::Filter* filter, T input, HdFormat outputFormat) {
    filter->Resize(width, height);
    filter->SetInput(rif::Color, input);
    filter->SetOutput(rif::Image::GetDesc(width, height, outputFormat));

    if (filterType == kFilterComposeOpacity) {
        filter->SetInput("alphaImage", m_retainedOpacity->GetResolvedFb());
    }
    else if (filterType == kFilterResample) {
        filter->SetParam("outSize", GfVec2i(width, height));
    }
    else if (filterType == kFilterTonemap) {
        SetTonemapFilterParams(filter);
    }
    else if (filterType == kFilterGamma) {
        filter->SetParam("gamma", m_gamma.value);
    }
}

void HdRprApiColorAov::OnSizeChange(rif::Context* rifContext) {
    m_denoisingOutputValid = false;
    m_denoisingFailed = false;
    m_denoisedImage.reset();
    if (!m_filter) {
        return;
    }

    auto fbDesc = m_retainedRawColor->GetAovFb()->GetDesc();
    if (m_denoisingEnabled && rifContext && fbDesc.fb_width && fbDesc.fb_height) {
        try {
            m_denoisedImage = rifContext->CreateImage(
                rif::Image::GetDesc(fbDesc.fb_width, fbDesc.fb_height, HdFormatFloat32Vec4));
        } catch (std::exception const& e) {
            TF_WARN("Cannot allocate denoised beauty image: %s; using raw beauty", e.what());
            m_denoisingEnabled = false;
        }
    }
    auto resizeFirstFilter = [&](Filter filterType, rif::Filter* filter, HdFormat outputFormat) {
        if (m_denoisedImage) {
            ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, filterType, filter,
                         m_denoisedImage->GetHandle(), outputFormat);
        } else {
            ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, filterType, filter,
                         m_retainedRawColor->GetResolvedFb(), outputFormat);
        }
    };
    if (m_auxFilters.empty()) {
        resizeFirstFilter(m_mainFilterType, m_filter.get(), m_format);
    }
    else {
        // Ideally we would use "Filter combining" functionality, but it does not work with user-defined filter
        // So we attach each filter separately

        auto filter = m_auxFilters.front().second.get();
        // Preserve HDR until tone mapping, and cast only the final display output.
        const HdFormat intermediateFormat = m_denoisingEnabled ? HdFormatFloat32Vec4 : m_format;
        resizeFirstFilter(m_auxFilters.front().first, filter, intermediateFormat);
        for (int i = 1; i < m_auxFilters.size(); ++i) {
            auto filterInput = m_auxFilters[i - 1].second->GetOutput();
            ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, m_auxFilters[i].first, m_auxFilters[i].second.get(), filterInput, intermediateFormat);
        }
        ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, m_mainFilterType, m_filter.get(), m_auxFilters.back().second->GetOutput(), m_format);
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
}

void HdRprApiComputedAov::Resize(int width, int height, HdFormat format) {
    if (m_format != format) {
        m_format = format;
        m_dirtyBits |= ChangeTracker::DirtyFormat;
    }

    if (m_width != width || m_height != height) {
        m_width = width;
        m_height = height;
        m_dirtyBits |= ChangeTracker::DirtySize;
    }
}

HdRprApiDepthAov::HdRprApiDepthAov(
    int width, int height, HdFormat format,
    std::shared_ptr<HdRprApiAov> worldCoordinateAov,
    std::shared_ptr<HdRprApiAov> opacityAov,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext)
    : HdRprApiComputedAov(HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kNdcDepth), true), width, height, format)
    , m_retainedWorldCoordinateAov(worldCoordinateAov)
    , m_retainedOpacityAov(opacityAov) {

    if (!rifContext) {
        RPR_THROW_ERROR_MSG("Can not create depth AOV: RIF context required");
    }

    m_retainedNDCFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_NDC_DEPTH, rifContext);
    m_ndcFilter = m_retainedNDCFilter.get();

    m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_USER_DEFINED, rifContext);

    auto opacityComposingKernelCode = std::string(R"(
                            int2 coord;
                            GET_COORD_OR_RETURN(coord, GET_BUFFER_SIZE(inputImage));
                            vec4 alpha = ReadPixelTyped(alphaImage, coord.x, coord.y);
                            vec4 color = ReadPixelTyped(inputImage, coord.x, coord.y);
                            if (alpha.x == 0) {
                                // Using 1.0 does not work with compositing in viewport
                                color = make_vec4(0.99f, 0.99f, 0.99f, 0.99f);
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
    : HdRprApiAov(HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kScTransparentBackground), true), format)
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

PXR_NAMESPACE_CLOSE_SCOPE
