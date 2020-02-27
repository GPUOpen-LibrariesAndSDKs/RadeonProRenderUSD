#ifndef HDRPR_RPR_API_FRAMEBUFFER_H
#define HDRPR_RPR_API_FRAMEBUFFER_H

#include "pxr/pxr.h"

#include <RadeonProRender.hpp>
#include <RadeonProRender_CL.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApiFramebuffer {
public:
    static constexpr rpr::Aov kAovNone = RPR_AOV_MAX;
    static constexpr uint32_t kNumChannels = 4;

    HdRprApiFramebuffer(rpr::Context* context, uint32_t width, uint32_t height);
    HdRprApiFramebuffer(HdRprApiFramebuffer&& fb) noexcept;
    ~HdRprApiFramebuffer();

    HdRprApiFramebuffer& operator=(HdRprApiFramebuffer&& fb) noexcept;

    void AttachAs(rpr::Aov aov);
    void Clear();
    void Resolve(HdRprApiFramebuffer* dstFrameBuffer);
    /// Return true if framebuffer was actually resized
    bool Resize(uint32_t width, uint32_t height);

    bool GetData(void* dstBuffer, size_t dstBufferSize);
    size_t GetSize() const;
    rpr::FramebufferDesc GetDesc() const;

    rpr_cl_mem GetCLMem();
    rpr::FrameBuffer* GetRprObject() { return m_rprFb; }

protected:
    HdRprApiFramebuffer() = default;
    void Delete();

protected:
    rpr::Context* m_context;
    rpr::FrameBuffer* m_rprFb;
    uint32_t m_width;
    uint32_t m_height;
    rpr::Aov m_aov;

private:
    void Create(uint32_t width, uint32_t height);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_FRAMEBUFFER_H
