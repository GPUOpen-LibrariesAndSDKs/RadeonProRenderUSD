#ifndef RPRCPP_FRAMEBUFFER_H
#define RPRCPP_FRAMEBUFFER_H

#include "rprObject.h"
#include "rprError.h"

#include <RadeonProRender_CL.h>
#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

class FrameBuffer : public Object {
public:
    static constexpr rpr_aov kAovNone = RPR_AOV_MAX;
    static constexpr rpr_uint kNumChannels = 4;

    FrameBuffer(rpr_context context, rpr_uint width, rpr_uint height);
    FrameBuffer(FrameBuffer&& fb) noexcept;
    ~FrameBuffer() override;

    FrameBuffer& operator=(FrameBuffer&& fb) noexcept;

    void AttachAs(rpr_aov aov);
    void Clear();
    void Resolve(FrameBuffer* dstFrameBuffer);
    virtual void Resize(rpr_uint width, rpr_uint height);

    std::shared_ptr<char> GetData(std::shared_ptr<char> buffer = nullptr, size_t* bufferSize = nullptr);
    rpr_uint GetSize() const;
    rpr_framebuffer_desc GetDesc() const;

    rpr_cl_mem GetCLMem();
    rpr_framebuffer GetHandle();

protected:
    FrameBuffer() = default;
    void Delete();

protected:
    rpr_context m_context = nullptr;
    rpr_uint m_width = 0;
    rpr_uint m_height = 0;
    rpr_aov m_aov = kAovNone;

private:
    void Create();
};

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRCPP_FRAMEBUFFER_H