#include "rprFramebuffer.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

FrameBuffer::FrameBuffer(rpr_context context, rpr_uint width, rpr_uint height)
    : m_context(context)
    , m_width(width)
    , m_height(height)
    , m_aov(kAovNone) {
    if (!m_context) {
        throw rpr::Error("Failed to create framebuffer", RPR_ERROR_INVALID_CONTEXT, nullptr);
    }
    Create();
}

FrameBuffer::FrameBuffer(FrameBuffer&& fb) noexcept {
    *this = std::move(fb);
}

FrameBuffer::~FrameBuffer() {
    Delete();
}

FrameBuffer& FrameBuffer::operator=(FrameBuffer&& fb) noexcept {
    m_context = fb.m_context;
    m_width = fb.m_width;
    m_height = fb.m_height;
    m_aov = fb.m_aov;
    m_rprObjectHandle = fb.m_rprObjectHandle;

    fb.m_rprObjectHandle = nullptr;
    fb.m_aov = kAovNone;
    return *this;
}

void FrameBuffer::AttachAs(rpr_aov aov) {
    if (m_aov != kAovNone || aov == kAovNone) {
        RPR_ERROR_CHECK(rprContextSetAOV(m_context, m_aov, nullptr), "Failed to detach aov framebuffer", m_context);
    }

    if (aov != kAovNone) {
        RPR_ERROR_CHECK(rprContextSetAOV(m_context, aov, GetHandle()), "Failed to attach aov framebuffer", m_context);
        m_aov = aov;
    }
}

void FrameBuffer::Clear() {
    if (m_width == 0 || m_height == 0) {
        return;
    }
    RPR_ERROR_CHECK_THROW(rprFrameBufferClear(GetHandle()), "Failed to attach aov framebuffer", m_context);
}

void FrameBuffer::Resolve(FrameBuffer* dstFrameBuffer) {
    if (!m_rprObjectHandle ||
        !dstFrameBuffer || !dstFrameBuffer->m_rprObjectHandle) {
        return;
    }
    RPR_ERROR_CHECK_THROW(rprContextResolveFrameBuffer(m_context, GetHandle(), dstFrameBuffer->m_rprObjectHandle, true), "Failed to resolve framebuffer", m_context);
}

void FrameBuffer::Resize(rpr_uint width, rpr_uint height) {
    if (m_width == width && m_height == height) {
        return;
    }

    rpr_aov aov = m_aov;
    Delete();

    m_width = width;
    m_height = height;
    Create();

    if (aov != kAovNone) {
        AttachAs(aov);
    }
}

std::shared_ptr<char> FrameBuffer::GetData(std::shared_ptr<char> buffer, size_t* bufferSize) {
    if (m_width == 0 || m_height == 0 || !m_rprObjectHandle) {
        return nullptr;
    }

    auto size = GetSize();
    if (!buffer) {
        buffer = std::shared_ptr<char>(new char[size]);
        if (bufferSize) {
            *bufferSize = size;
        }
    } else if (bufferSize) {
        if (*bufferSize != size) {
            buffer = std::shared_ptr<char>(new char[size]);
            *bufferSize = size;
        }
    }

    if (RPR_ERROR_CHECK(rprFrameBufferGetInfo(GetHandle(), RPR_FRAMEBUFFER_DATA, size, buffer.get(), nullptr), "Failed to get framebuffer data", m_context)) {
        return nullptr;
    }

    return buffer;
}

rpr_uint FrameBuffer::GetSize() const {
    return m_width * m_height * kNumChannels * sizeof(float);
}

rpr_framebuffer_desc FrameBuffer::GetDesc() const {
    rpr_framebuffer_desc desc = {};
    desc.fb_width = m_width;
    desc.fb_height = m_height;
    return desc;
}

rpr_cl_mem FrameBuffer::GetCLMem() {
    if (m_width == 0 || m_height == 0 || !m_rprObjectHandle) {
        return nullptr;
    }

    rpr_cl_mem clMem = nullptr;
    RPR_ERROR_CHECK_THROW(rprFrameBufferGetInfo(GetHandle(), RPR_CL_MEM_OBJECT, sizeof(clMem), &clMem, nullptr), "Failed to get cl_mem object", m_context);
    return clMem;
}

rpr_framebuffer FrameBuffer::GetHandle() {
    return (rpr_framebuffer)m_rprObjectHandle;
}

void FrameBuffer::Create() {
    if (m_width == 0 || m_height == 0) {
        return;
    }

    rpr_framebuffer_format format = {};
    format.num_components = kNumChannels;
    format.type = RPR_COMPONENT_TYPE_FLOAT32;

    rpr_framebuffer_desc desc = {};
    desc.fb_width = m_width;
    desc.fb_height = m_height;

    rpr_framebuffer fb = nullptr;
    RPR_ERROR_CHECK_THROW(rprContextCreateFrameBuffer(m_context, format, &desc, &fb), "Failed to create framebuffer", m_context);
    m_rprObjectHandle = fb;
}

void FrameBuffer::Delete() {
    if (m_aov != kAovNone) {
        AttachAs(kAovNone);
    }
    Object::Delete();
}

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE
