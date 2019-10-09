#ifndef RPRCPP_IMAGE_H
#define RPRCPP_IMAGE_H

#include "rprObject.h"
#include "rprError.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

class Context;

class Image : public Object {
public:
    Image(Context* context, const char* path);
    Image(Context* context, void const* encodedData, size_t dataSize);
    Image(Context* context, rpr_uint width, rpr_uint height, rpr_image_format format, void const* data);
    Image(Context* context, rpr_image_desc const& imageDesc, rpr_image_format format, void const* data);
    Image(Image&& image) noexcept;
    ~Image() override = default;

    Image& operator=(Image&& image) noexcept;

    rpr_image_format GetFormat() const;
    rpr_image_desc GetDesc() const;
    rpr_image GetHandle();

private:
    rpr_image GetHandle() const;
};

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRCPP_IMAGE_H
