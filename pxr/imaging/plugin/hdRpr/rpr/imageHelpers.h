#ifndef HDRPR_CORE_IMAGE_HELPERS_H
#define HDRPR_CORE_IMAGE_HELPERS_H

#include <RadeonProRender.hpp>

namespace rpr {

Image* CreateImage(Context* context, char const* path, bool forceLinearSpace = false);
Image* CreateImage(Context* context, uint32_t width, uint32_t height, ImageFormat format, void const* data, rpr::Status* status = nullptr);

ImageFormat GetImageFormat(Image* image);
ImageDesc GetImageDesc(Image* image);

} // namespace rpr

#endif // HDRPR_CORE_IMAGE_HELPERS_H
