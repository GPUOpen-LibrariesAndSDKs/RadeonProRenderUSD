#include "rprApiAov.h"
#include "rprApi.h"
#include "rifcpp/rifError.h"
#include "rprcpp/rprContext.h"
#include "rprcpp/rprFramebuffer.h"

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
                         rpr::Context* rprContext, std::unique_ptr<rif::Filter> filter)
    : m_format(format),
    m_filter(std::move(filter)) {
    auto componentType = HdGetComponentFormat(format);
    if (componentType != HdFormatUNorm8 &&
        componentType != HdFormatFloat16 &&
        componentType != HdFormatFloat32) {
        TF_CODING_ERROR("Unsupported component type: %d", componentType);
        m_format = HdFormatFloat32Vec4;
    }

    m_aov = pxr::make_unique<rpr::FrameBuffer>(rprContext->GetHandle(), width, height);
    m_aov->AttachAs(rprAovType);

    // XXX (Hybrid): Hybrid plugin does not support framebuffer resolving (rprContextResolveFrameBuffer)
    if (rprContext->GetActivePluginType() != rpr::PluginType::HYBRID) {
        m_resolved = pxr::make_unique<rpr::FrameBuffer>(rprContext->GetHandle(), width, height);
    }
}

HdRprApiAov::HdRprApiAov(rpr_aov rprAovType, int width, int height, HdFormat format,
                         rpr::Context* rprContext, rif::Context* rifContext)
    : HdRprApiAov(rprAovType, width, height, format, rprContext, [format, rifContext]() -> std::unique_ptr<rif::Filter> {
        if (format == HdFormatFloat32Vec4) {
            // RPR framebuffers by default with such format
            return nullptr;
        }

        auto filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, rifContext);
        if (filter) {
            filter->SetParam("interpOperator", RIF_IMAGE_INTERPOLATION_NEAREST);
        }
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
        m_aov->Clear();
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
                primIdData[i] &= 0xFFFFFF;
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

rpr::FrameBuffer* HdRprApiAov::GetResolvedFb() {
    return (m_resolved ? m_resolved : m_aov).get();
}

void HdRprApiAov::OnFormatChange(rif::Context* rifContext) {
    m_filter = nullptr;
    if (rifContext && m_format != HdFormatFloat32Vec4) {
        m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, rifContext);
        m_filter->SetParam("interpOperator", RIF_IMAGE_INTERPOLATION_NEAREST);

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

HdRprApiColorAov::HdRprApiColorAov(int width, int height, HdFormat format, rpr::Context* rprContext)
    : HdRprApiAov(RPR_AOV_COLOR, width, height, format, rprContext, nullptr) {

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
        SetFilter(kFilterComposeOpacity, m_retainedOpacity != nullptr);
    }
}

void HdRprApiColorAov::EnableAIDenoise(
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

    SetFilter(kFilterAIDenoise, true);
    SetFilter(kFilterEAWDenoise, false);
}

void HdRprApiColorAov::EnableEAWDenoise(
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

    SetFilter(kFilterEAWDenoise, true);
    SetFilter(kFilterAIDenoise, false);
}

void HdRprApiColorAov::DisableDenoise(rif::Context* rifContext) {
    SetFilter(kFilterEAWDenoise, false);
    SetFilter(kFilterAIDenoise, false);
    SetFilter(kFilterResample, m_format != HdFormatFloat32Vec4);

    for (auto& retainedInput : m_retainedDenoiseInputs) {
        retainedInput = nullptr;
    }
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

        m_filter = nullptr;
        m_auxFilters.clear();

        if ((m_enabledFilters & kFilterAIDenoise) ||
            (m_enabledFilters & kFilterEAWDenoise) ||
            (m_enabledFilters & kFilterComposeOpacity)) {

            if ((m_enabledFilters & kFilterAIDenoise) ||
                (m_enabledFilters & kFilterEAWDenoise)) {
                auto denoiseFilterType = (m_enabledFilters & kFilterAIDenoise) ? rif::FilterType::AIDenoise : rif::FilterType::EawDenoise;
                auto fbDesc = m_aov->GetDesc();
                m_filter = rif::Filter::Create(denoiseFilterType, rifContext, fbDesc.fb_width, fbDesc.fb_height);
                m_mainFilterType = (m_enabledFilters & kFilterAIDenoise) ? kFilterAIDenoise : kFilterEAWDenoise;
            }

            if (m_enabledFilters & kFilterComposeOpacity) {
                if (m_filter) {
                    m_auxFilters.emplace_back(m_mainFilterType, std::move(m_filter));
                }

                m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_USER_DEFINED, rifContext);
                auto opacityComposingKernelCode = std::string(R"(
                    int2 coord;
                    GET_COORD_OR_RETURN(coord, GET_BUFFER_SIZE(inputImage));
                    vec4 alpha = ReadPixelTyped(alphaImage, coord.x, coord.y);
                    vec4 color = ReadPixelTyped(inputImage, coord.x, coord.y) * alpha.x;
                    WritePixelTyped(outputImage, coord.x, coord.y, make_vec4(color.x, color.y, color.z, alpha.x));
                )");
                m_filter->SetParam("code", opacityComposingKernelCode);
                m_mainFilterType = kFilterComposeOpacity;
            }
        } else if (m_enabledFilters & kFilterResample) {
            m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, rifContext);
            m_filter->SetParam("interpOperator", RIF_IMAGE_INTERPOLATION_NEAREST);
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

void HdRprApiColorAov::Resolve() {
    HdRprApiAov::Resolve();

    for (auto& auxFilter : m_auxFilters) {
        auxFilter.second->Resolve();
    }
}

void HdRprApiColorAov::OnFormatChange(rif::Context* rifContext) {
    SetFilter(kFilterResample, m_format != HdFormatFloat32Vec4);
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
    }
}

void HdRprApiColorAov::OnSizeChange(rif::Context* rifContext) {
    if (!m_filter) {
        return;
    }

    auto fbDesc = m_aov->GetDesc();
    if (m_auxFilters.empty()) {
        ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, m_mainFilterType, m_filter.get(), GetResolvedFb());
    } else {
        // Ideally we would use "Filter combining" functionality, but it does not work with user-defined filter
        // So we attach each filter separately

        auto filter = m_auxFilters.front().second.get();
        ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, m_auxFilters.front().first, filter, GetResolvedFb());
        for (int i = 1; i < m_auxFilters.size(); ++i) {
            auto filterInput = m_auxFilters[i - 1].second->GetOutput();
            ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, m_auxFilters[i].first, m_auxFilters[i].second.get(), filterInput);
        }
        ResizeFilter(fbDesc.fb_width, fbDesc.fb_height, m_mainFilterType, m_filter.get(), m_auxFilters.back().second->GetOutput());
    }
}

HdRprApiNormalAov::HdRprApiNormalAov(
    int width, int height, HdFormat format,
    rpr::Context* rprContext, rif::Context* rifContext)
    : HdRprApiAov(RPR_AOV_SHADING_NORMAL, width, height, format, rprContext, rif::Filter::CreateCustom(RIF_IMAGE_FILTER_REMAP_RANGE, rifContext)) {
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

HdRprApiDepthAov::HdRprApiDepthAov(
    HdFormat format,
    std::shared_ptr<HdRprApiAov> worldCoordinateAov,
    rpr::Context* rprContext, rif::Context* rifContext)
    : m_retainedWorldCoordinateAov(worldCoordinateAov) {
    if (!rifContext) {
        RPR_THROW_ERROR_MSG("Can not create depth AOV: RIF context required");
    }

    m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_NDC_DEPTH, rifContext);
    m_ndcFilter = m_filter.get();
    m_remapFilter = nullptr;

#if PXR_VERSION >= 2002
    m_retainedFilter = std::move(m_filter);

    m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_REMAP_RANGE, rifContext);
    m_filter->SetParam("srcRangeAuto", 0);
    m_filter->SetParam("srcLo", -1.0f);
    m_filter->SetParam("srcHi", 1.0f);
    m_filter->SetParam("dstLo", 0.0f);
    m_filter->SetParam("dstHi", 1.0f);
    m_remapFilter = m_filter.get();
#endif

    auto fbDesc = m_retainedWorldCoordinateAov->GetAovFb()->GetDesc();
    m_width = fbDesc.fb_width;
    m_height = fbDesc.fb_height;
    m_format = format;
}

void HdRprApiDepthAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_dirtyBits & ChangeTracker::DirtyFormat ||
        m_dirtyBits & ChangeTracker::DirtySize) {

        if (m_remapFilter) {
            m_ndcFilter->SetInput(rif::Color, m_retainedWorldCoordinateAov->GetResolvedFb());
            m_ndcFilter->SetOutput(rif::Image::GetDesc(m_width, m_height, m_format));
            m_remapFilter->SetInput(rif::Color, m_ndcFilter->GetOutput());
            m_remapFilter->SetOutput(rif::Image::GetDesc(m_width, m_height, m_format));
        } else {
            m_ndcFilter->SetInput(rif::Color, m_retainedWorldCoordinateAov->GetResolvedFb());
            m_ndcFilter->SetOutput(rif::Image::GetDesc(m_width, m_height, m_format));
        }
    }
    m_dirtyBits = ChangeTracker::Clean;

    auto viewProjectionMatrix = rprApi->GetCameraViewMatrix() * rprApi->GetCameraProjectionMatrix();
    m_ndcFilter->SetParam("viewProjMatrix", GfMatrix4f(viewProjectionMatrix.GetTranspose()));

    if (m_remapFilter) {
        m_remapFilter->Update();
    }
    m_ndcFilter->Update();
}

void HdRprApiDepthAov::Resize(int width, int height, HdFormat format) {
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

PXR_NAMESPACE_CLOSE_SCOPE
