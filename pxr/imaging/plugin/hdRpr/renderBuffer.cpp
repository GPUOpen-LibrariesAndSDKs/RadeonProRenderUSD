#include "renderBuffer.h"

#include "pxr/imaging/hd/sceneDelegate.h"
#include "tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (aov_color) \
    (aov_normal) \
    (aov_depth) \
    (aov_primId) \
    (aov_primvars_st)
);

HdRprRenderBuffer::HdRprRenderBuffer(SdfPath const & id, HdRprApiSharedPtr rprApi) : HdRenderBuffer(id), m_numMappers(0)
{
    if (!rprApi)
    {
        TF_CODING_ERROR("RprApi is expired");
        return;
    }

    m_rprApiWeakPrt = rprApi;

    auto& idName = id.GetName();
    if (idName == _tokens->aov_color)
    {
        m_aovName = HdRprAovTokens->color;
        m_format = HdFormat::HdFormatUNorm8Vec4;
    }
    else if (idName == _tokens->aov_normal)
    {
        m_aovName = HdRprAovTokens->normal;
        m_format = HdFormat::HdFormatFloat32Vec3;
    }
    else if (idName == _tokens->aov_depth)
    {
        m_aovName = HdRprAovTokens->depth;
        m_format = HdFormat::HdFormatFloat32;
    }
    else if (idName == _tokens->aov_primId)
    {
        m_aovName = HdRprAovTokens->primId;
        m_format = HdFormat::HdFormatInt32;
    }
    else if (idName == _tokens->aov_primvars_st)
    {
        m_aovName = HdRprAovTokens->primvarsSt;
        m_format = HdFormat::HdFormatFloat32Vec3;
    }

    if (m_aovName.IsEmpty()) {
        TF_WARN("Incomplete HdRprRenderBuffer");
    }
}

HdDirtyBits HdRprRenderBuffer::GetInitialDirtyBitsMask() const
{
	return AllDirty;
}

bool HdRprRenderBuffer::Allocate(GfVec3i const& dimensions,
                      HdFormat format,
                      bool multiSampled) {
    // XXX: Disable until usdview fix kludges in AOV system
    return false;

    if (auto rprApi = m_rprApiWeakPrt.lock()) {
        if (!m_aovName.IsEmpty()) {
            rprApi->EnableAov(m_aovName);
            return true;
        } else {
            return false;
        }
    }

    TF_CODING_ERROR("RprApi is expired");
    return false;
}

unsigned int HdRprRenderBuffer::GetWidth() const
{
    // XXX: Disable until usdview fix kludges in AOV system
    return 0u;

    if (auto rprApi = m_rprApiWeakPrt.lock()) {
        GfVec2i fbSize;
        rprApi->GetFramebufferSize(&fbSize);
        return fbSize[0];
    }
    return 0u;
}

unsigned int HdRprRenderBuffer::GetHeight() const
{
    // XXX: Disable until usdview fix kludges in AOV system
    return 0u;
    if (auto rprApi = m_rprApiWeakPrt.lock()) {
        GfVec2i fbSize;
        rprApi->GetFramebufferSize(&fbSize);
        return fbSize[1];
    }
    return 0u;
}

unsigned int HdRprRenderBuffer::GetDepth() const
{
    return 1u;
}

HdFormat HdRprRenderBuffer::GetFormat() const {
    return m_format;
}

bool HdRprRenderBuffer::IsMultiSampled() const {
    return false;
}

void* HdRprRenderBuffer::Map() {
    ++m_numMappers;
    // XXX: RPR does not support framebuffer mapping, so here is at least correct mapping for reading
    return m_dataCache.get();
}

void HdRprRenderBuffer::Unmap() {
    if (m_numMappers > 0) {
        --m_numMappers;
    }
}

bool HdRprRenderBuffer::IsMapped() const {
    return m_numMappers.load() != 0;
}

void HdRprRenderBuffer::Resolve() {
    // XXX: Houdini Solaris workaround to track AOV selection
    if (auto rprApi = m_rprApiWeakPrt.lock()) {
        rprApi->EnableAov(m_aovName);
    }

    // XXX: Disable until usdview fix kludges in AOV system
    return;

    if (auto rprApi = m_rprApiWeakPrt.lock()) {
        m_dataCache = rprApi->GetFramebufferData(m_aovName, m_dataCache, &m_dataCacheSize);
        if (!m_dataCache) {
            return;
        }

        std::function<void(int pixelIdx)> convertPixel;
        GfVec2i fbSize;
        rprApi->GetFramebufferSize(&fbSize);
        switch (m_format) {
        case HdFormat::HdFormatUNorm8Vec4: {
            convertPixel = [this](int pixelIdx) {
                auto inPixel = &reinterpret_cast<float const*>(m_dataCache.get())[pixelIdx * 4];
                auto outPixel = &reinterpret_cast<uint8_t*>(m_dataCache.get())[pixelIdx * 4];
                outPixel[0] = static_cast<uint8_t>(255.0f * inPixel[0]);
                outPixel[1] = static_cast<uint8_t>(255.0f * inPixel[1]);
                outPixel[2] = static_cast<uint8_t>(255.0f * inPixel[2]);
                outPixel[3] = static_cast<uint8_t>(255.0f * inPixel[3]);
            };
            break;
        }
        case HdFormat::HdFormatFloat32: {
            convertPixel = [this](int pixelIdx) {
                auto inPixel = &reinterpret_cast<float const*>(m_dataCache.get())[pixelIdx * 4];
                auto outPixel = &reinterpret_cast<float*>(m_dataCache.get())[pixelIdx];
                outPixel[0] = inPixel[0];
            };
            break;
        }
        case HdFormat::HdFormatFloat32Vec3: {
            convertPixel = [this](int pixelIdx) {
                auto inPixel = &reinterpret_cast<float const*>(m_dataCache.get())[pixelIdx * 4];
                auto outPixel = &reinterpret_cast<float*>(m_dataCache.get())[pixelIdx * 3];
                outPixel[0] = inPixel[0];
                outPixel[1] = inPixel[1];
                outPixel[2] = inPixel[2];
            };
            break;
        }
        case HdFormat::HdFormatInt32: {
            convertPixel = [this](int pixelIdx) {
                auto inPixel = &reinterpret_cast<float const*>(m_dataCache.get())[pixelIdx * 4];
                auto outPixel = &reinterpret_cast<int32_t*>(m_dataCache.get())[pixelIdx];
                outPixel[0] = static_cast<int32_t>(255.0f * inPixel[0]);
            };
            break;
        }
        default:
            TF_WARN("Resolved unexpected renderBuffer format: %d", m_format);
            break;
        }

        if (convertPixel) {
            for (int y = 0; y < fbSize[1]; ++y) {
                for (int x = 0; x < fbSize[0]; ++x) {
                    convertPixel(x + y * fbSize[0]);
                }
            }
        }
    }
}

bool HdRprRenderBuffer::IsConverged() const {
    return false;
}

void HdRprRenderBuffer::_Deallocate() {
    if (auto rprApi = m_rprApiWeakPrt.lock()) {
        if (!m_aovName.IsEmpty()) {
            rprApi->DisableAov(m_aovName);
        }
    }

}

PXR_NAMESPACE_CLOSE_SCOPE
