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
#include "cpuFilters.h"

#include "pxr/imaging/rprUsd/contextMetadata.h"
#include "pxr/imaging/rprUsd/error.h"

PXR_NAMESPACE_OPEN_SCOPE

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
