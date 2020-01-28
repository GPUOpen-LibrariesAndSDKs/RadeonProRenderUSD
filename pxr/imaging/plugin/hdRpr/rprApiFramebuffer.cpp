#include "rprApiFramebuffer.h"
#include "rpr/helpers.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprApiFramebuffer::HdRprApiFramebuffer(rpr::Context* context, uint32_t width, uint32_t height)
    : m_context(context)
    , m_rprFb(nullptr)
    , m_width(0)
    , m_height(0)
    , m_aov(kAovNone) {
    if (!m_context) {
        RPR_THROW_ERROR_MSG("Failed to create framebuffer: missing rpr context");
    }

    Create(width, height);
}

HdRprApiFramebuffer::HdRprApiFramebuffer(HdRprApiFramebuffer&& fb) noexcept {
    *this = std::move(fb);
}

HdRprApiFramebuffer::~HdRprApiFramebuffer() {
    Delete();
}

HdRprApiFramebuffer& HdRprApiFramebuffer::operator=(HdRprApiFramebuffer&& fb) noexcept {
    m_context = fb.m_context;
    m_width = fb.m_width;
    m_height = fb.m_height;
    m_aov = fb.m_aov;
    m_rprFb = fb.m_rprFb;

    fb.m_width = 0u;
    fb.m_height = 0u;
    fb.m_aov = kAovNone;
    fb.m_rprFb = nullptr;
    return *this;
}

void HdRprApiFramebuffer::AttachAs(rpr::Aov aov) {
    if (m_aov != kAovNone || aov == kAovNone) {
        RPR_ERROR_CHECK(m_context->SetAOV(m_aov, nullptr), "Failed to detach aov framebuffer");
    }

    if (aov != kAovNone) {
        RPR_ERROR_CHECK_THROW(m_context->SetAOV(aov, m_rprFb), "Failed to attach aov framebuffer");
    }

    m_aov = aov;
}

void HdRprApiFramebuffer::Clear() {
    if (m_width == 0 || m_height == 0) {
        return;
    }
    RPR_ERROR_CHECK(m_rprFb->Clear(), "Failed to clear framebuffer");
}

void HdRprApiFramebuffer::Resolve(HdRprApiFramebuffer* dstFrameBuffer) {
    if (!m_rprFb ||
        !dstFrameBuffer || !dstFrameBuffer->m_rprFb) {
        return;
    }
    RPR_ERROR_CHECK_THROW(m_context->ResolveFrameBuffer(m_rprFb, dstFrameBuffer->m_rprFb, true), "Failed to resolve framebuffer");
}

bool HdRprApiFramebuffer::Resize(uint32_t width, uint32_t height) {
    if (m_width == width && m_height == height) {
        return false;
    }

    rpr::Aov aov = m_aov;
    Delete();
    Create(width, height);

    if (aov != kAovNone) {
        AttachAs(aov);
    }

    return true;
}

bool HdRprApiFramebuffer::GetData(void* dstBuffer, size_t dstBufferSize) {
    if (m_width == 0 || m_height == 0 || !m_rprFb) {
        return false;
    }

    auto size = GetSize();
    if (size > dstBufferSize) {
        return false;
    }

    return !RPR_ERROR_CHECK(m_rprFb->GetInfo(RPR_FRAMEBUFFER_DATA, size, dstBuffer, nullptr), "Failed to get framebuffer data");
}

size_t HdRprApiFramebuffer::GetSize() const {
    return m_width * m_height * kNumChannels * sizeof(float);
}

rpr::FramebufferDesc HdRprApiFramebuffer::GetDesc() const {
    rpr::FramebufferDesc desc = {};
    desc.fb_width = m_width;
    desc.fb_height = m_height;
    return desc;
}

rpr_cl_mem HdRprApiFramebuffer::GetCLMem() {
    if (m_width == 0 || m_height == 0 || !m_rprFb) {
        return nullptr;
    }

    return rpr::GetInfo<rpr_cl_mem>(m_rprFb, rpr_framebuffer_info(RPR_CL_MEM_OBJECT));
}

void HdRprApiFramebuffer::Create(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }

    rpr::FramebufferFormat format = {};
    format.num_components = kNumChannels;
    format.type = RPR_COMPONENT_TYPE_FLOAT32;

    rpr::FramebufferDesc desc = {};
    desc.fb_width = width;
    desc.fb_height = height;

    rpr::Status status;
    m_rprFb = m_context->CreateFrameBuffer(format, desc, &status);
    if (!m_rprFb) {
        RPR_ERROR_CHECK_THROW(status, "Failed to create framebuffer");
    }

    m_width = width;
    m_height = height;
}

void HdRprApiFramebuffer::Delete() {
    if (m_aov != kAovNone) {
        AttachAs(kAovNone);
    }
    if (m_rprFb) {
        delete m_rprFb;
        m_rprFb = nullptr;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
