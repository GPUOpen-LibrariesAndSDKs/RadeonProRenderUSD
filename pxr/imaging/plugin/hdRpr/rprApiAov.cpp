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

#include "pxr/imaging/rprUsd/contextMetadata.h"
#include "pxr/imaging/rprUsd/error.h"
#include "pxr/base/work/loops.h"

PXR_NAMESPACE_OPEN_SCOPE

void CpuRemapFilter(float* src, float* dest, size_t length, float srcLo, float srcHi, float dstLo, float dstHi) {
    WorkParallelForN(length,
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            dest[i] = ((src[i] - srcLo) / (srcHi - srcLo)) * (dstHi - dstLo) + dstLo;
        }});
}

void CpuVec4toVec3Filter(GfVec4f* src, GfVec3f* dest, size_t numPixels) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            dest[i][0] = src[i][0];
            dest[i][1] = src[i][1];
            dest[i][2] = src[i][2];
        }
    });
}

void CpuVec4toFloatFilter(GfVec4f* src, float* dest, size_t numPixels) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            dest[i] = src[i][0];
        }
    });
}

void CpuVec4toInt32Filter(GfVec4f* src, int32_t* dest, size_t numPixels) {
    char* destAsChar = (char*)dest;
    float* srcAsFloat = (float*)src;
    WorkParallelForN(numPixels * sizeof(int32_t),   // output as char, input as GfVec4f
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            if (i % 4 == 3)
            {
                destAsChar[i] = 0;
            }
            else
            {
                destAsChar[i] = (char)(srcAsFloat[i] * 255 + 0.5f);
            }
        }
    });
    
    int32_t* destAsInt = (int32_t*)dest;
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            destAsInt[i] -= 1;
        }
    });
}

void CpuFloatToInt32Filter(float* src, int32_t* dest, size_t length) {
    // RPR store integer ID values to RGB images using such formula:
    // c[i].x = i;
    // c[i].y = i/256;
    // c[i].z = i/(256*256);
    // i.e. saving little endian int24 to uchar3
    // That's why we interpret the value as int and filling the alpha channel with zeros
    int32_t* srcAsInt = (int32_t*)src;
    WorkParallelForN(length,
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            dest[i] = (srcAsInt[i] & 0xFFFFFF) - 1;
        }
    });
}

void CpuNdcFilter(GfVec4f* src, GfVec4f* dest, size_t numPixels, const GfMatrix4f& viewProjectionMatrix) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            float norm = std::max(src[i][3], 1.0f);
            GfVec4f pos(src[i][0] / norm, src[i][1] / norm, src[i][2] / norm, 1.0f);
            GfVec4f posResult = viewProjectionMatrix * pos;
            float depth = posResult[2] / posResult[3];
            dest[i][0] = depth;
            dest[i][1] = depth;
            dest[i][2] = depth;
            dest[i][3] = 1.0f;
        }
    });
}

void CpuOpacityFilter(GfVec4f* opacity, GfVec4f* srcdest, size_t numPixels) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            float op = opacity[i][0];
            srcdest[i][0] *= op;
            srcdest[i][1] *= op;
            srcdest[i][2] *= op;
            srcdest[i][3]  = op;
        }
    });
}

void CpuOpacityMaskFilter(GfVec4f* opacity, GfVec4f* srcdest, size_t numPixels) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            if (opacity[i][0] == 0.0f) {
                srcdest[i][0] = 1.0f;
                srcdest[i][1] = 1.0f;
                srcdest[i][1] = 1.0f;
                srcdest[i][1] = 1.0f;
            }
        }
    });
}

void CpuFillMaskFilter(GfVec4f* srcdest, size_t numPixels) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            unsigned int idDecoded = (unsigned int)(srcdest[i][0] * 256) + (unsigned int)(srcdest[i][1] * 256 * 256) + (unsigned int)(srcdest[i][2] * 256 * 256 * 256);
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
                srcdest[i][0] = (v0 & 0xFFFF) / (float)(0xFFFF);
                srcdest[i][1] = (v0 >> 16) / (float)(0xFFFF);
                srcdest[i][2] = (v1 & 0xFFFF) / (float)(0xFFFF);
                srcdest[i][3] = 1.0f;
            }
            else {
                srcdest[i][0] = srcdest[i][1] = srcdest[i][2] = srcdest[i][3] = 0;
            }
        }
    });
}

void CpuResampleNearest(GfVec4f* src, size_t srcWidth, size_t srcHeight, GfVec4f* dest, size_t destWidth, size_t destHeight) {
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
                int cy = yratio * y;
                dest[(y * destWidth + x)][0] = src[(cy * srcWidth + cx)][0];
                dest[(y * destWidth + x)][1] = src[(cy * srcWidth + cx)][1];
                dest[(y * destWidth + x)][2] = src[(cy * srcWidth + cx)][2];
                dest[(y * destWidth + x)][3] = src[(cy * srcWidth + cx)][3];
            }
        }});
}

void CpuGammaCorrection(GfVec4f* srcdest, size_t numPixels, float gamma) {
    if (gamma == 0) {
        return;
    }
    float _1_g = 1 / gamma;
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            srcdest[i][0] = std::powf(srcdest[i][0], _1_g);
            srcdest[i][1] = std::powf(srcdest[i][1], _1_g);
            srcdest[i][2] = std::powf(srcdest[i][2], _1_g);
            // skiping alpha
        }
    });
}

void CpuTonemap(GfVec4f* srcdest, size_t numPixels, float gamma, float exposureTime, float sensitivity, float fstop) {
    if (gamma == 0 || fstop == 0) {
        return;
    }
    float h = (0.65f * 21.61f * sensitivity * exposureTime) / (fstop * fstop);
    float _1_g = 1 / gamma;
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            srcdest[i][0] = std::powf(srcdest[i][0] * h, _1_g);
            srcdest[i][1] = std::powf(srcdest[i][1] * h, _1_g);
            srcdest[i][2] = std::powf(srcdest[i][2] * h, _1_g);
            // skiping alpha
        }
    });
}

HdRprApiAov::HdRprApiAov(rpr_aov rprAovType, int width, int height, HdFormat format,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata)
    : m_aovDescriptor(HdRprAovRegistry::GetInstance().GetAovDesc(rprAovType, false))
    , m_format(format) {
    
    m_aov = pxr::make_unique<HdRprApiFramebuffer>(rprContext, width, height);
    m_aov->AttachAs(rprAovType);

    // XXX (Hybrid): Hybrid plugin does not support framebuffer resolving (rprContextResolveFrameBuffer)
    if (!RprUsdIsHybrid(rprContextMetadata.pluginType)) {
        m_resolved = pxr::make_unique<HdRprApiFramebuffer>(rprContext, width, height);
    }
}

void HdRprApiAov::Resolve() {
    if (m_aov) {
        m_aov->Resolve(m_resolved.get());
    }
    ResolveImpl();
    UpdateTempBuffer();
}

void HdRprApiAov::ResolveImpl() {


    if (m_filterEnabled) {
        assert(m_outputBuffer.size() > 0);
        auto resolvedFb = GetResolvedFb();
        if (!resolvedFb || !resolvedFb->GetData(m_outputBuffer.data(), m_outputBuffer.size() * sizeof(float))) {
            return;
        }

        if (m_aov) {
            auto fbDesc = m_aov->GetDesc();
            CpuResampleNearest((GfVec4f*)m_outputBuffer.data(), fbDesc.fb_width, fbDesc.fb_height, (GfVec4f*)m_outputBuffer.data(), fbDesc.fb_width, fbDesc.fb_height);
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
    if (m_tmpBuffer.size() > 0) {
        if (dstBufferSize != m_tmpBuffer.size()) {
            return false;
        }
        memcpy(dstBuffer, m_tmpBuffer.data(), dstBufferSize);
        return true;
    }
    
    if (m_outputBuffer.size() > 0) {
        if (dstBufferSize != m_outputBuffer.size() * sizeof(float)) {
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
    return GetDataImpl(dstBuffer, dstBufferSize);
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

    UpdateTempBufferSize(width, height, format);
}

void HdRprApiAov::Update(HdRprApi const* rprApi) {
    if (m_requiredTempBufferSize != m_tmpBuffer.size()) {
        if (m_requiredTempBufferSize > 0) {
            m_tmpBuffer.resize(m_requiredTempBufferSize);
        }
        else {
            m_tmpBuffer.clear();
        }
    }
    UpdateImpl(rprApi);
}

void HdRprApiAov::UpdateImpl(HdRprApi const* rprApi) {
    if (m_dirtyBits & ChangeTracker::DirtyFormat) {
        m_filterEnabled = (m_format != HdFormatFloat32Vec4);
        if (m_filterEnabled) {
            m_dirtyBits |= ChangeTracker::DirtySize;
        }
    }
    if (m_dirtyBits & ChangeTracker::DirtySize) {
        if (m_filterEnabled) {
            auto fbDesc = m_aov->GetDesc();
            if (fbDesc.fb_width * fbDesc.fb_height * 4 != m_outputBuffer.size()) {
                m_outputBuffer.resize(fbDesc.fb_width * fbDesc.fb_height * 4);
            }
        }
    }
    m_dirtyBits = ChangeTracker::Clean;
}

HdRprApiFramebuffer* HdRprApiAov::GetResolvedFb() {
    return (m_resolved ? m_resolved : m_aov).get();
}

void HdRprApiAov::UpdateTempBufferSize(int width, int height, HdFormat format) {
    switch (m_format)
    {
    case HdFormatFloat32: m_requiredTempBufferSize = width * height * sizeof(float); break;
    case HdFormatInt32: m_filterEnabled ? m_requiredTempBufferSize = width * height * sizeof(int32_t) : 0; break;   // when m_filterEnabled is true inplace conversion is used
    case HdFormatFloat32Vec3: m_requiredTempBufferSize = width * height * sizeof(float) * 3; break;
    case HdFormatFloat32Vec4: m_requiredTempBufferSize = 0; break;      // conversion is not needed
    default: 0; break;                                                  // pass data as is
    }
    if (m_requiredTempBufferSize != m_outputBuffer.size()) {
        m_dirtyBits |= ChangeTracker::DirtySize;
    }
}

void HdRprApiAov::UpdateTempBuffer() {
    if (m_requiredTempBufferSize == 0) {
        return;
    }

    if (!m_filterEnabled)
    {
        if (m_format == HdFormatFloat32) {
            CpuVec4toFloatFilter((GfVec4f*)m_outputBuffer.data(), (float*)m_tmpBuffer.data(), m_tmpBuffer.size() / sizeof(float));
        }
        else if (m_format == HdFormatInt32) {
            CpuVec4toInt32Filter((GfVec4f*)m_outputBuffer.data(), (int32_t*)m_tmpBuffer.data(), m_tmpBuffer.size() / sizeof(int32_t));
        }
        else if (m_format == HdFormatFloat32Vec3) {
            CpuVec4toVec3Filter((GfVec4f*)m_outputBuffer.data(), (GfVec3f*)m_tmpBuffer.data(), m_outputBuffer.size() / 4);
        }
    }
    else if (m_format == HdFormatInt32) {
        CpuFloatToInt32Filter(m_outputBuffer.data(), (int32_t*)m_outputBuffer.data(), m_outputBuffer.size());
    }
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
    m_tonemap = params;
}

void HdRprApiColorAov::SetGamma(GammaParams const& params) {
    bool isGammaEnabled = m_enabledFilters & kFilterGamma;
    bool gammaEnableDirty = params.enable != isGammaEnabled;

    SetFilter(kFilterGamma, params.enable);
    m_gamma = params;
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

void HdRprApiColorAov::UpdateImpl(HdRprApi const* rprApi) {
    if (m_dirtyBits & ChangeTracker::DirtyFormat) {
        OnFormatChange();
    }

    if (m_isEnabledFiltersDirty) {
        m_isEnabledFiltersDirty = false;
        m_dirtyBits |= ChangeTracker::DirtySize;
    }

    if (m_dirtyBits & ChangeTracker::DirtySize) {
        auto fbDesc = m_retainedRawColor->GetAovFb()->GetDesc();
        if (fbDesc.fb_width * fbDesc.fb_height * 4 != m_outputBuffer.size()) {
            m_outputBuffer.resize(fbDesc.fb_width * fbDesc.fb_height * 4);
        }
        if (m_enabledFilters & kFilterComposeOpacity) {
            if (fbDesc.fb_width * fbDesc.fb_height * 4 != m_opacityBuffer.size()) {
                m_opacityBuffer.resize(fbDesc.fb_width * fbDesc.fb_height * 4);
            }
        }
        else {
            m_opacityBuffer.clear();
        }
    }
    m_dirtyBits = ChangeTracker::Clean;
}

void HdRprApiColorAov::ResolveImpl() {
    auto resolvedFb = m_retainedRawColor->GetResolvedFb();
    if (!resolvedFb || !resolvedFb->GetData(m_outputBuffer.data(), m_outputBuffer.size() * sizeof(float))) {
        return;
    }

    size_t numPixels = m_outputBuffer.size() / 4;
    if ((m_enabledFilters & kFilterComposeOpacity) ||
        (m_enabledFilters & kFilterTonemap) ||
        (m_enabledFilters & kFilterGamma)) {

        if (m_enabledFilters & kFilterTonemap) {
            CpuTonemap((GfVec4f*)m_outputBuffer.data(), numPixels, m_tonemap.gamma, m_tonemap.exposureTime, m_tonemap.sensitivity, m_tonemap.fstop);
        }
        if (m_enabledFilters & kFilterGamma) {
            CpuGammaCorrection((GfVec4f*)m_outputBuffer.data(), numPixels, m_gamma.value);
        }
        if (m_enabledFilters & kFilterComposeOpacity) {
            auto resolvedOpFb = m_retainedOpacity->GetResolvedFb();
            if (!resolvedOpFb || !resolvedOpFb->GetData(m_opacityBuffer.data(), m_opacityBuffer.size() * sizeof(float))) {
                return;
            }

            CpuOpacityFilter((GfVec4f*)m_opacityBuffer.data(), (GfVec4f*)m_outputBuffer.data(), numPixels);
        }
    }
    else if (m_enabledFilters & kFilterResample) {
        auto fbDesc = m_retainedRawColor->GetAovFb()->GetDesc();
        CpuResampleNearest((GfVec4f*)m_outputBuffer.data(), fbDesc.fb_width, fbDesc.fb_height, (GfVec4f*)m_outputBuffer.data(), fbDesc.fb_width, fbDesc.fb_height);
    }
}

void HdRprApiColorAov::OnFormatChange() {
    SetFilter(kFilterResample, m_format != HdFormatFloat32Vec4);
    SetFilter(kFilterComposeOpacity, CanComposeAlpha());
    m_dirtyBits |= ChangeTracker::DirtySize;
}

HdRprApiNormalAov::HdRprApiNormalAov(
    int width, int height, HdFormat format,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata)
    : HdRprApiAov(RPR_AOV_SHADING_NORMAL, width, height, format, rprContext, rprContextMetadata) {
}

void HdRprApiNormalAov::UpdateImpl(HdRprApi const* rprApi) {
    if (m_dirtyBits & ChangeTracker::DirtySize) {
        auto fbDesc = m_aov->GetDesc();
        if (fbDesc.fb_width * fbDesc.fb_height * 3 != m_outputBuffer.size()) {
            m_outputBuffer.resize(fbDesc.fb_width * fbDesc.fb_height * 4);
        }
    }
    m_dirtyBits = ChangeTracker::Clean;
}

void HdRprApiNormalAov::ResolveImpl() {
    auto resolvedFb = GetResolvedFb();
    if (!resolvedFb || !resolvedFb->GetData(m_outputBuffer.data(), m_outputBuffer.size() * sizeof(float))) {
        return;
    }

    size_t numPixels = m_outputBuffer.size() / 4;
    CpuRemapFilter(m_outputBuffer.data(), m_outputBuffer.data(), numPixels * 4, 0.0, 1.0, -1.0, 1.0);
}

void HdRprApiComputedAov::Resize(int width, int height, HdFormat format) {
    if (m_width != width || m_height != height) {
        m_width = width;
        m_height = height;
        m_dirtyBits |= ChangeTracker::DirtySize;
    }

    HdRprApiAov::Resize(width, height, format);
}

HdRprApiDepthAov::HdRprApiDepthAov(int width, int height, HdFormat format,
    std::shared_ptr<HdRprApiAov> worldCoordinateAov,
    std::shared_ptr<HdRprApiAov> opacityAov,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata)
    : HdRprApiComputedAov(HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kNdcDepth), true), width, height, format)
    , m_retainedWorldCoordinateAov(worldCoordinateAov)

    , m_retainedOpacityAov(opacityAov)
    , m_viewProjectionMatrix(GfMatrix4f()) {
    // m_cpuFilterBuffer.resize(cpuFilterBufferSize());
}

void HdRprApiDepthAov::UpdateImpl(HdRprApi const* rprApi) {
    m_viewProjectionMatrix = GfMatrix4f(rprApi->GetCameraViewMatrix() * rprApi->GetCameraProjectionMatrix()).GetTranspose();
    if (m_dirtyBits & ChangeTracker::DirtySize) {
        auto fbDesc = m_retainedWorldCoordinateAov->GetAovFb()->GetDesc();
        if (fbDesc.fb_width * fbDesc.fb_height * 4 != m_cpuFilterBuffer.size()) {
            m_cpuFilterBuffer.resize(fbDesc.fb_width * fbDesc.fb_height * 4);
        }
        if (fbDesc.fb_width * fbDesc.fb_height * 4 != m_outputBuffer.size()) {
            m_outputBuffer.resize(fbDesc.fb_width * fbDesc.fb_height * 4);
        }
    }
    m_dirtyBits = ChangeTracker::Clean;
}

void HdRprApiDepthAov::ResolveImpl() {
    static size_t numPixels = m_cpuFilterBuffer.size() / 4;
    
    auto coordinateFb = m_retainedWorldCoordinateAov->GetResolvedFb();
    if (!coordinateFb || !coordinateFb->GetData(m_outputBuffer.data(), m_outputBuffer.size() * sizeof(float))) {
        return;
    }
    
    CpuNdcFilter((GfVec4f*)m_outputBuffer.data(), (GfVec4f*)m_outputBuffer.data(), numPixels, m_viewProjectionMatrix);
    
    auto opacityFb = m_retainedOpacityAov->GetResolvedFb();
    if (!opacityFb || !opacityFb->GetData(m_cpuFilterBuffer.data(), m_cpuFilterBuffer.size() * sizeof(float))) {
        return;
    }
    
    CpuOpacityMaskFilter((GfVec4f*)m_cpuFilterBuffer.data(), (GfVec4f*)m_outputBuffer.data(), numPixels);
    CpuRemapFilter(m_outputBuffer.data(), m_outputBuffer.data(), numPixels * 4, -1, 1, 0, 1.0f);
}

HdRprApiIdMaskAov::HdRprApiIdMaskAov(
    HdRprAovDescriptor const& aovDescriptor, std::shared_ptr<HdRprApiAov> const& baseIdAov,
    int width, int height, HdFormat format,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata)
    : HdRprApiComputedAov(aovDescriptor, width, height, format)
    , m_baseIdAov(baseIdAov) {
}

void HdRprApiIdMaskAov::UpdateImpl(HdRprApi const* rprApi) {
    if (m_dirtyBits & ChangeTracker::DirtySize) {
        auto fbDesc = m_baseIdAov->GetAovFb()->GetDesc();
        if (fbDesc.fb_width * fbDesc.fb_height * 4 != m_outputBuffer.size()) {
            m_outputBuffer.resize(fbDesc.fb_width * fbDesc.fb_height * 4);
        }
    }
    m_dirtyBits = ChangeTracker::Clean;
}

void HdRprApiIdMaskAov::ResolveImpl() {
    auto resolvedFb = m_baseIdAov->GetResolvedFb();
    if (!resolvedFb || !resolvedFb->GetData(m_outputBuffer.data(), m_outputBuffer.size() * sizeof(float))) {
        return;
    }

    size_t numPixels = m_outputBuffer.size() / 4;
    CpuFillMaskFilter((GfVec4f*)m_outputBuffer.data(), numPixels);
}

PXR_NAMESPACE_CLOSE_SCOPE
