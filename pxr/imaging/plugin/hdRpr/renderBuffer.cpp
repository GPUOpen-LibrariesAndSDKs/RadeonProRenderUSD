#include "renderBuffer.h"

#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (aov_color) \
    (aov_normal) \
    (aov_depth) \
    (aov_linear_depth) \
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
    m_isConverged.store(false);

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
    else if (idName == _tokens->aov_linear_depth)
    {
        m_aovName = HdRprAovTokens->linearDepth;
        m_format = HdFormat::HdFormatFloat32;
    }
    else if (idName == _tokens->aov_primId)
    {
        m_aovName = HdRprAovTokens->primId;
        m_format = HdFormat::HdFormatUNorm8Vec4;
    }
    else if (idName == _tokens->aov_primvars_st)
    {
        m_aovName = HdRprAovTokens->primvarsSt;
        m_format = HdFormat::HdFormatFloat32Vec3;
    }
}

HdDirtyBits HdRprRenderBuffer::GetInitialDirtyBitsMask() const
{
	return AllDirty;
}

bool HdRprRenderBuffer::Allocate(GfVec3i const& dimensions,
                      HdFormat format,
                      bool multiSampled) {
    if (m_aovName.IsEmpty()) {
        return false;
    }

    if (auto rprApi = m_rprApiWeakPrt.lock()) {
        if (rprApi->EnableAov(m_aovName, dimensions[0], dimensions[1], m_format)) {
            m_width = dimensions[0];
            m_height = dimensions[1];
            return true;
        }
    } else {
        TF_CODING_ERROR("RprApi is expired");
    }

    return false;
}

void HdRprRenderBuffer::_Deallocate() {
    if (auto rprApi = m_rprApiWeakPrt.lock()) {
        if (!m_aovName.IsEmpty()) {
            rprApi->DisableAov(m_aovName);
        }
    }

    m_format = HdFormatInvalid;
    m_width = 0u;
    m_height = 0u;
}

unsigned int HdRprRenderBuffer::GetWidth() const {
    return m_width;
}

unsigned int HdRprRenderBuffer::GetHeight() const {
    return m_height;
}

unsigned int HdRprRenderBuffer::GetDepth() const {
    return 1u;
}

HdFormat HdRprRenderBuffer::GetFormat() const {
    if (m_aovName == HdRprAovTokens->primId) {
        return HdFormatInt32;
    }
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
    if (auto rprApi = m_rprApiWeakPrt.lock()) {
        m_dataCache = rprApi->GetAovData(m_aovName, m_dataCache, &m_dataCacheSize);
        if (m_aovName == HdRprAovTokens->primId) {
            // RPR store integer ID values to RGB images using such formula:
            // c[i].x = i;
            // c[i].y = i/256;
            // c[i].z = i/(256*256);
            // i.e. saving little endian int24 to uchar3
            // That's why we interpret the value as int and filling the alpha channel with zeros
            auto primIdData = reinterpret_cast<int*>(m_dataCache.get());
            for (uint32_t y = 0; y < m_height; ++y) {
                uint32_t yOffset = y * m_width;
                for (uint32_t x = 0; x < m_width; ++x) {
                    primIdData[x + yOffset] &= 0xFFFFFF;
                }
            }
        }
    }
}

bool HdRprRenderBuffer::IsConverged() const {
    return m_isConverged.load();
}

void HdRprRenderBuffer::SetConverged(bool converged) {
    return m_isConverged.store(converged);
}

PXR_NAMESPACE_CLOSE_SCOPE
