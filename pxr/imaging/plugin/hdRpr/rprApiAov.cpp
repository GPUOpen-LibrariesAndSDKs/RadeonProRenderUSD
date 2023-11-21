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
#include "pxr/base/work/loops.h"

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

void CpuRemapFilter(float* src, float* dest, size_t length, float srcLo, float srcHi, float dstLo, float dstHi) {
    WorkParallelForN(length,
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            dest[i] = ((src[i] - srcLo) / (srcHi - srcLo)) * (dstHi - dstLo) + dstLo;
        }});
}

void CpuVec4toVec3Filter(float* src, float* dest, size_t length) {
    WorkParallelForN(length,
        [&](size_t begin, size_t end) {
        for (int i = 0; i < length; ++i) {
            dest[i * 3] = src[i * 4];
            dest[i * 3 + 1] = src[i * 4 + 1];
            dest[i * 3 + 2] = src[i * 4 + 2];
        }
    });
}

void CpuNdcFilter(float* src, float* dest, size_t length, const GfMatrix4f& viewProjectionMatrix) {
    WorkParallelForN(length,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            float norm = std::max(src[i * 4 + 3], 1.0f);
            GfVec4f pos(src[i * 4] / norm, src[i * 4 + 1] / norm, src[i * 4 + 2] / norm, 1.0f);
            GfVec4f posResult = viewProjectionMatrix.GetOrthonormalized() * pos;
            dest[i * 4] =     std::max(std::min(posResult[2] / posResult[3], 0.9f), -0.9f);
            dest[i * 4 + 1] = 0;// posResult[2] / posResult[3];
            dest[i * 4 + 2] = 0;// posResult[2] / posResult[3];
            dest[i * 4 + 3] = 1.0f;

            /*float norm = std::max(src[i * 4 + 3], 1.0f);
            GfVec4f pos(src[i * 4] / norm, src[i * 4 + 1] / norm, src[i * 4 + 2] / norm, 1.0f);
            GfVec4f posResult = viewProjectionMatrix * pos;
            float depth = posResult[2] / posResult[3];
            dest[i * 4]     = depth;
            dest[i * 4 + 1] = depth;
            dest[i * 4 + 2] = depth;
            dest[i * 4 + 3] = 1.0f;*/
        }
    });
}

void CpuOpacityFilter(float* opacity, float* srcdest, size_t length) {
    WorkParallelForN(length,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            float op = opacity[i * 4];
            srcdest[i * 4]     *= op;
            srcdest[i * 4 + 1] *= op;
            srcdest[i * 4 + 2] *= op;
            srcdest[i * 4 + 3]  = op;
        }
    });
}

void CpuOpacityMaskFilter(float* opacity, float* srcdest, size_t length) {
    WorkParallelForN(length,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            float op = opacity[i * 4];
            if (op == 0.0f) {
                srcdest[i * 4] = 1.0f;
                srcdest[i * 4 + 1] = 1.0f;
                srcdest[i * 4 + 1] = 1.0f;
                srcdest[i * 4 + 1] = 1.0f;
            }
        }
    });
}

void CpuFillMaskFilter(float* srcdest, size_t length) {
    WorkParallelForN(length,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            unsigned int idDecoded = (unsigned int)(srcdest[i * 4] * 256) + (unsigned int)(srcdest[i * 4 + 1] * 256 * 256) + (unsigned int)(srcdest[i * 4 + 2] * 256 * 256 * 256);
            if (idDecoded) {
                unsigned int v0 = 0x123;
                unsigned int v1 = idDecoded;
                unsigned int s0 = 0;
                const unsigned int N = 4;
                for (unsigned int n = 0; n < N; n++) {
                    s0 += 0x9e3779b9;
                    v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
                    v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
                }
                srcdest[i * 4]     = (v0 & 0xFFFF) / (float)(0xFFFF);
                srcdest[i * 4 + 1] = (v0 >> 16) / (float)(0xFFFF);
                srcdest[i * 4 + 2] = (v1 & 0xFFFF) / (float)(0xFFFF);
                srcdest[i * 4 + 3] = 1.0f;
            }
            else {
                srcdest[i * 4] = srcdest[i * 4 + 1] = srcdest[i * 4 + 2] = srcdest[i * 4 + 3] = 0;
            }
        }
    });
}

void CpuResampleNearest(float* src, size_t srcWidth, size_t srcHeight, float* dest, size_t destWidth, size_t destHeight) {
    if (destWidth <= 1 || destHeight <= 1) {
        return;
    }

    float xratio = 1.0f * (srcWidth - 1.0f) / (destWidth - 1.0f);
    float yratio = 1.0f * (srcHeight - 1.0f) / (destHeight - 1.0f);

    WorkParallelForN(destHeight,
        [&](size_t begin, size_t end) {
        for (int y = begin; y < end; ++y) {
            for (int x = 0; x < destWidth; ++x) {
                int cx = xratio * x;
                int cy = xratio * y;
                dest[(y * destWidth + x) * 4] = src[(cy * srcWidth + cx) * 4];
                dest[(y * destWidth + x) * 4 + 1] = src[(cy * srcWidth + cx) * 4 + 1];
                dest[(y * destWidth + x) * 4 + 2] = src[(cy * srcWidth + cx) * 4 + 2];
                dest[(y * destWidth + x) * 4 + 3] = src[(cy * srcWidth + cx) * 4 + 3];
            }
        }});
}

HdRprApiAov::HdRprApiAov(rpr_aov rprAovType, int width, int height, HdFormat format,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, std::unique_ptr<rif::Filter> filter)
    : m_aovDescriptor(HdRprAovRegistry::GetInstance().GetAovDesc(rprAovType, false))
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
    if (!rifContext) {
            return nullptr;
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

    if (m_filterEnabled) {
        assert(m_outputBuffer.size() > 0);
        auto resolvedFb = GetResolvedFb();
        if (!resolvedFb || !resolvedFb->GetData(m_outputBuffer.data(), m_outputBuffer.size())) {
            return;
        }

        if (m_aov) {
            auto fbDesc = m_aov->GetDesc();
            CpuResampleNearest((float*)m_outputBuffer.data(), fbDesc.fb_width, fbDesc.fb_height, (float*)m_outputBuffer.data(), fbDesc.fb_width, fbDesc.fb_height);
        }
    }
}

void HdRprApiAov::Clear() {
    if (m_aov) {
        auto& v = m_aovDescriptor.clearValue;
        m_aov->Clear(v[0], v[1], v[2], v[3]);
    }
}

bool HdRprApiAov::GetDataImpl(void* dstBuffer, size_t dstBufferSize) {
    if (m_outputBuffer.size() > 0) {
        if (dstBufferSize != m_outputBuffer.size()) {
            return false;
        }
        memcpy(dstBuffer, m_outputBuffer.data(), dstBufferSize);
        return true;
    }

    auto resolvedFb = GetResolvedFb();
    if (!resolvedFb) {
        return false;
    }

    return resolvedFb->GetData(dstBuffer, dstBufferSize);
}

bool HdRprApiAov::GetData(void* dstBuffer, size_t dstBufferSize) {
    auto getBuffer = dstBuffer;
    if (!m_filterEnabled)
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
        if (!m_filterEnabled)
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
    if (m_dirtyBits & ChangeTracker::DirtySize) {
        m_filterEnabled = m_aov && m_format != HdFormatFloat32Vec4;
        if (m_filterEnabled) {
            auto fbDesc = m_aov->GetDesc();
            if (fbDesc.fb_width * fbDesc.fb_height * 4 * sizeof(float) != m_outputBuffer.size()) {
                m_outputBuffer.resize(fbDesc.fb_width * fbDesc.fb_height * 4 * sizeof(float));
            }
        }
        else {
            m_outputBuffer.clear();
        }
    }
    m_dirtyBits = ChangeTracker::Clean;
}

HdRprApiFramebuffer* HdRprApiAov::GetResolvedFb() {
    return (m_resolved ? m_resolved : m_aov).get();
}

HdRprApiColorAov::HdRprApiColorAov(HdFormat format, std::shared_ptr<HdRprApiAov> rawColorAov, rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata)
    : HdRprApiAov(HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kColorAlpha), true), format)
    , m_retainedRawColor(std::move(rawColorAov)) {

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
        OnSizeChange();
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

void HdRprApiColorAov::OnSizeChange() {
    if (!m_filter) {
        return;
    }

    auto fbDesc = m_retainedRawColor->GetAovFb()->GetDesc();
    if (m_auxFilters.empty()) {
        ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, m_mainFilterType, m_filter.get(), m_retainedRawColor->GetResolvedFb());
    }
    else {
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
}

HdRprApiNormalAov::HdRprApiNormalAov(
    int width, int height, HdFormat format,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext)
    : HdRprApiAov(RPR_AOV_SHADING_NORMAL, width, height, format, rprContext, rprContextMetadata, nullptr) {
}

void HdRprApiNormalAov::OnFormatChange(rif::Context* rifContext) {
    m_dirtyBits |= ChangeTracker::DirtySize;
}

void HdRprApiNormalAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_dirtyBits & ChangeTracker::DirtySize) {
        auto fbDesc = m_aov->GetDesc();
        if (fbDesc.fb_width * fbDesc.fb_height * 3 * sizeof(float) != m_outputBuffer.size()) {
            m_outputBuffer.resize(fbDesc.fb_width * fbDesc.fb_height * 3 * sizeof(float));
        }
        if (fbDesc.fb_width * fbDesc.fb_height * 4 != m_cpuFilterBuffer.size()) {
            m_cpuFilterBuffer.resize(fbDesc.fb_width * fbDesc.fb_height * 4);
        }
    }
    m_dirtyBits = ChangeTracker::Clean;
}

void HdRprApiNormalAov::Resolve() {
    HdRprApiAov::Resolve();

    auto resolvedFb = GetResolvedFb();
    if (!resolvedFb || !resolvedFb->GetData(m_cpuFilterBuffer.data(), m_cpuFilterBuffer.size() * sizeof(float))) {
        return;
    }

    size_t numPixels = m_cpuFilterBuffer.size() / 4;
    CpuVec4toVec3Filter(m_cpuFilterBuffer.data(), (float*)m_outputBuffer.data(), numPixels);
    CpuRemapFilter((float*)m_outputBuffer.data(), (float*)m_outputBuffer.data(), numPixels * 3, 0.0, 1.0, -1.0, 1.0);
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

HdRprApiDepthAov::HdRprApiDepthAov(int width, int height, HdFormat format,
    std::shared_ptr<HdRprApiAov> worldCoordinateAov,
    std::shared_ptr<HdRprApiAov> opacityAov,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata)
    : HdRprApiComputedAov(HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kNdcDepth), true), width, height, format)
    , m_retainedWorldCoordinateAov(worldCoordinateAov)

    , m_retainedOpacityAov(opacityAov)
    , m_viewProjectionMatrix(GfMatrix4f()) {
    m_cpuFilterBuffer.resize(cpuFilterBufferSize());
}

void HdRprApiDepthAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
    m_viewProjectionMatrix = GfMatrix4f(rprApi->GetCameraViewMatrix() * rprApi->GetCameraProjectionMatrix()).GetTranspose();
}

bool HdRprApiDepthAov::GetDataImpl(void* dstBuffer, size_t dstBufferSize) {
    if (cpuFilterBufferSize() != m_cpuFilterBuffer.size()) {
        m_cpuFilterBuffer.resize(cpuFilterBufferSize());
    }
    static size_t numPixels = dstBufferSize / 4 / sizeof(float);
    if (m_cpuFilterBuffer.size() / 4 != numPixels)
    {
        return false;
    }

    auto coordinateFb = m_retainedWorldCoordinateAov->GetResolvedFb();
    if (!coordinateFb || !coordinateFb->GetData(m_cpuFilterBuffer.data(), m_cpuFilterBuffer.size() * sizeof(float))) {
        return false;
    }
    
    CpuNdcFilter(m_cpuFilterBuffer.data(), (float*)dstBuffer, numPixels, m_viewProjectionMatrix);
    //memcpy(dstBuffer, m_cpuFilterBuffer.data(), dstBufferSize);
    
    auto opacityFb = m_retainedOpacityAov->GetResolvedFb();
    if (!opacityFb || !opacityFb->GetData(m_cpuFilterBuffer.data(), m_cpuFilterBuffer.size() * sizeof(float))) {
        return false;
    }
    
    CpuOpacityMaskFilter(m_cpuFilterBuffer.data(), (float*)dstBuffer, numPixels);
    CpuRemapFilter((float*)dstBuffer, (float*)dstBuffer, numPixels * 4, -1, 1, 0, 1.0f);
    return true;
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
}

void HdRprApiIdMaskAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_dirtyBits & ChangeTracker::DirtySize) {
        auto fbDesc = m_baseIdAov->GetAovFb()->GetDesc();
        if (fbDesc.fb_width * fbDesc.fb_height * 4 * sizeof(float) != m_outputBuffer.size()) {
            m_outputBuffer.resize(fbDesc.fb_width * fbDesc.fb_height * 4 * sizeof(float));
        }
    }
    m_dirtyBits = ChangeTracker::Clean;
}

void HdRprApiIdMaskAov::Resolve() {
    auto resolvedFb = m_baseIdAov->GetResolvedFb();
    if (!resolvedFb || !resolvedFb->GetData(m_outputBuffer.data(), m_outputBuffer.size())) {
        return;
    }

    size_t numPixels = m_outputBuffer.size() / 4 / sizeof(float);
    CpuFillMaskFilter((float*)m_outputBuffer.data(), numPixels);
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
