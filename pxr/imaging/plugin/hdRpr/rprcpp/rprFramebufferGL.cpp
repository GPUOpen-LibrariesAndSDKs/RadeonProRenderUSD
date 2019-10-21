#include "rprFramebufferGL.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

FrameBufferGL::FrameBufferGL(rpr_context context, rpr_uint width, rpr_uint height) {
    m_context = context;
    m_width = width;
    m_height = height;
    CreateGL();
}

FrameBufferGL::FrameBufferGL(FrameBufferGL&& fb) noexcept {
    *this = std::move(fb);
}

FrameBufferGL const& FrameBufferGL::operator=(FrameBufferGL&& fb) noexcept {
    m_textureId = fb.m_textureId;
    fb.m_textureId = 0;

    FrameBuffer::operator=(std::move(fb));
    return *this;
}

FrameBufferGL::~FrameBufferGL() {
    DeleteGL();
}

bool FrameBufferGL::Resize(rpr_uint width, rpr_uint height) {
    if (m_width == width && m_height == height) {
        return false;
    }

    rpr_aov aov = m_aov;
    DeleteGL();

    m_width = width;
    m_height = height;
    CreateGL();

    if (aov != kAovNone) {
        AttachAs(aov);
    }

    return true;
}

rpr_GLuint FrameBufferGL::GetGL() const {
    return m_textureId;
}

void FrameBufferGL::CreateGL() {
    if (m_width == 0 || m_height == 0) {
        return;
    }

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    rpr_framebuffer fb = nullptr;
    auto status = rprContextCreateFramebufferFromGLTexture2D(m_context, GL_TEXTURE_2D, 0, m_textureId, &fb);
    if (status != RPR_SUCCESS) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
        throw rpr::Error("Failed to create framebuffer from GL texture", status, m_context);
    }
    m_rprObjectHandle = fb;
}

void FrameBufferGL::DeleteGL() {
    FrameBuffer::Delete();
    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
}

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE
