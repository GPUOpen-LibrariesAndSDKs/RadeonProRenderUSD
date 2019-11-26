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

    auto buffer = std::shared_ptr<char>(new char[size]);
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
        filter->SetParam("interpOperator", RIF_IMAGE_INTERPOLATION_NEAREST);
        return filter;
    }()) {

}

void HdRprApiAov::Resolve() {
    if (m_aov) {
        m_aov->Resolve(m_resolved.get());
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
    if (m_format != HdFormatFloat32Vec4) {
        m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, rifContext);
        m_filter->SetParam("interpOperator", RIF_IMAGE_INTERPOLATION_NEAREST);
    } else {
        m_filter = nullptr;
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

HdRprApiColorAov::HdRprApiColorAov(
    int width, int height, HdFormat format,
    rpr::Context* rprContext, rif::Context* rifContext)
    : HdRprApiAov(RPR_AOV_COLOR, width, height, format, rprContext, nullptr) {

}

void HdRprApiColorAov::EnableAIDenoise(
    std::shared_ptr<HdRprApiAov> albedo,
    std::shared_ptr<HdRprApiAov> normal,
    std::shared_ptr<HdRprApiAov> linearDepth) {
    if (m_currentFilter == rif::FilterType::AIDenoise) {
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

    m_currentFilter = rif::FilterType::AIDenoise;
    m_isCurrentFilterDirty = true;
}

void HdRprApiColorAov::EnableEAWDenoise(
    std::shared_ptr<HdRprApiAov> albedo,
    std::shared_ptr<HdRprApiAov> normal,
    std::shared_ptr<HdRprApiAov> linearDepth,
    std::shared_ptr<HdRprApiAov> objectId,
    std::shared_ptr<HdRprApiAov> worldCoordinate) {
    if (m_currentFilter == rif::FilterType::EawDenoise) {
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

    m_currentFilter = rif::FilterType::EawDenoise;
    m_isCurrentFilterDirty = true;
}

void HdRprApiColorAov::DisableDenoise(rif::Context* rifContext) {
    if (m_currentFilter == rif::FilterType::Resample || !m_filter) {
        return;
    }

    for (auto& retainedInput : m_retainedDenoiseInputs) {
        retainedInput = nullptr;
    }

    m_currentFilter = m_format == HdFormatFloat32Vec4 ? rif::FilterType::None : rif::FilterType::Resample;
    m_isCurrentFilterDirty = true;
}

void HdRprApiColorAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_dirtyBits & ChangeTracker::DirtyFormat) {
        OnFormatChange(rifContext);
    }
    if (m_isCurrentFilterDirty) {
        m_isCurrentFilterDirty = false;
        switch (m_currentFilter) {
            case rif::FilterType::None:
                m_filter = nullptr;
                break;
            case rif::FilterType::Resample:
                m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, rifContext);
                m_filter->SetParam("interpOperator", RIF_IMAGE_INTERPOLATION_NEAREST);
                break;
            case rif::FilterType::AIDenoise:
            case rif::FilterType::EawDenoise: {
                try {
                    auto fbDesc = m_aov->GetDesc();
                    m_filter = rif::Filter::Create(m_currentFilter, rifContext, fbDesc.fb_width, fbDesc.fb_height);
                } catch (rif::Error const& e) {
                    TF_RUNTIME_ERROR("Failed to create denoise filter: %s", e.what());
                    m_filter = nullptr;
                    m_currentFilter = rif::FilterType::None;
                }
                break;
            }
            default:
                break;
        }

        m_dirtyBits |= ChangeTracker::DirtySize;
    }
    if (m_dirtyBits & ChangeTracker::DirtySize) {
        OnSizeChange(rifContext);
    }
    m_dirtyBits = ChangeTracker::Clean;

    if (m_filter) {
        m_filter->Update();
    }
}

void HdRprApiColorAov::OnFormatChange(rif::Context* rifContext) {
    if (m_currentFilter == rif::FilterType::None && m_format != HdFormatFloat32Vec4) {
        m_currentFilter = rif::FilterType::Resample;
        m_isCurrentFilterDirty = true;
    } else if (m_currentFilter == rif::FilterType::Resample && m_format == HdFormatFloat32Vec4) {
        m_currentFilter = rif::FilterType::None;
        m_isCurrentFilterDirty = true;
    }

    m_dirtyBits |= ChangeTracker::DirtySize;
}

void HdRprApiColorAov::OnSizeChange(rif::Context* rifContext) {
    if (!m_filter) {
        return;
    }

    auto fbDesc = m_aov->GetDesc();
    m_filter->Resize(fbDesc.fb_width, fbDesc.fb_height);
    m_filter->SetInput(rif::Color, GetResolvedFb());
    m_filter->SetOutput(rif::Image::GetDesc(fbDesc.fb_width, fbDesc.fb_height, m_format));

    if (m_currentFilter == rif::FilterType::Resample) {
        m_filter->SetParam("outSize", GfVec2i(fbDesc.fb_width, fbDesc.fb_height));
    } else if (m_currentFilter == rif::FilterType::AIDenoise ||
               m_currentFilter == rif::FilterType::EawDenoise) {
        for (int i = 0; i < rif::MaxInput; ++i) {
            if (auto retainedInput = m_retainedDenoiseInputs[i].get()) {
                m_filter->SetInput(static_cast<rif::FilterInputType>(i), retainedInput->GetResolvedFb());
            }
        }
    }
}

HdRprApiNormalAov::HdRprApiNormalAov(
    int width, int height, HdFormat format,
    rpr::Context* rprContext, rif::Context* rifContext)
    : HdRprApiAov(RPR_AOV_SHADING_NORMAL, width, height, format, rprContext, rif::Filter::CreateCustom(RIF_IMAGE_FILTER_REMAP_RANGE, rifContext)) {
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
    m_filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_NDC_DEPTH, rifContext);
    auto fbDesc = m_retainedWorldCoordinateAov->GetAovFb()->GetDesc();
    m_width = fbDesc.fb_width;
    m_height = fbDesc.fb_height;
    m_format = format;
}

void HdRprApiDepthAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_dirtyBits & ChangeTracker::DirtyFormat ||
        m_dirtyBits & ChangeTracker::DirtySize) {

        m_filter->SetInput(rif::Color, m_retainedWorldCoordinateAov->GetResolvedFb());
        m_filter->SetOutput(rif::Image::GetDesc(m_width, m_height, m_format));
    }
    m_dirtyBits = ChangeTracker::Clean;

    auto viewProjectionMatrix = rprApi->GetCameraViewMatrix() * rprApi->GetCameraProjectionMatrix();
    m_filter->SetParam("viewProjMatrix", GfMatrix4f(viewProjectionMatrix.GetTranspose()));

    m_filter->Update();
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
