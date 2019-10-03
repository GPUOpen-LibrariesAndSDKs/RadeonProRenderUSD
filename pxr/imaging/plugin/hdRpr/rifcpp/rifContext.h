#ifndef RIFCPP_CONTEXT_H
#define RIFCPP_CONTEXT_H

#include <RadeonProRender.h>
#include "RadeonImageFilters.h"
#include "rifImage.h"

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

class FrameBuffer;

} // namespace rpr

namespace rif {

class Context {
public:
    static std::unique_ptr<Context> Create(rpr_context rprContext);

    virtual ~Context();

    rif_image_filter CreateImageFilter(rif_image_filter_type type);

    std::unique_ptr<Image> CreateImage(rif_image_desc const& desc);
    virtual std::unique_ptr<Image> CreateImage(rpr::FrameBuffer* rprFrameBuffer) = 0;

    void AttachFilter(rif_image_filter filter, rif_image inputImage, rif_image outputImage);
    void DettachFilter(rif_image_filter filter);

    virtual void UpdateInputImage(rpr::FrameBuffer* rprFrameBuffer, rif_image image);

    void ExecuteCommandQueue();

protected:
    Context() = default;

protected:
    rif_context m_context = nullptr;
    rif_command_queue m_commandQueue = nullptr;

private:
    int m_numAttachedFilters = 0;
};

PXR_NAMESPACE_CLOSE_SCOPE

} // namespace rif

#endif // RIFCPP_CONTEXT_H
