#ifndef RPRCPP_FRAMEBUFFER_GL_H
#define RPRCPP_FRAMEBUFFER_GL_H

#include "rprFramebuffer.h"

#include <RadeonProRender_GL.h>
#include <GL/glew.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

class FrameBufferGL : public FrameBuffer {
public:
    FrameBufferGL(rpr_context context, rpr_uint width, rpr_uint height);
    FrameBufferGL(FrameBufferGL&& fb) noexcept;
    ~FrameBufferGL() override;

    FrameBufferGL const& operator=(FrameBufferGL&& fb) noexcept;

    void Resize(rpr_uint width, rpr_uint height) override;

    rpr_GLuint GetGL() const;

private:
    void CreateGL();
    void DeleteGL();

private:
    rpr_GLuint m_textureId = 0;
};

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRCPP_FRAMEBUFFER_GL_H
