#include "rprImage.h"
#include "rprContext.h"

#ifdef ENABLE_RAT
#include <IMG/IMG_File.h>
#include <PXL/PXL_Raster.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

namespace {

rpr_image_desc GetRprImageDesc(rpr_image_format format, rpr_uint width, rpr_uint height, rpr_uint depth = 1) {
    int bytesPerComponent = 1;
    if (format.type == RPR_COMPONENT_TYPE_FLOAT16) {
        bytesPerComponent = 2;
    } else if (format.type == RPR_COMPONENT_TYPE_FLOAT32) {
        bytesPerComponent = 4;
    }

    rpr_image_desc desc = {};
    desc.image_width = width;
    desc.image_height = height;
    desc.image_depth = depth;
    desc.image_row_pitch = width * format.num_components * bytesPerComponent;
    desc.image_slice_pitch = desc.image_row_pitch * height;

    return desc;
}

} // namespace anonymous

Image::Image(Context* context, const char* path) {
#ifdef ENABLE_RAT
    auto dot = strrchr(path, '.');
    if (dot && strcmp(dot, ".rat") == 0) {
        auto ratImage = std::unique_ptr<IMG_File>(IMG_File::open(path));
        if (!ratImage) {
            throw rpr::Error((std::string("Failed to load image: ") + path).c_str());
        }

        UT_Array<PXL_Raster*> images;
        if (!ratImage->readImages(images) ||
            images.isEmpty()) {
            for (auto image : images) {
                delete image;
            }
            throw rpr::Error((std::string("Failed to load image: ") + path).c_str());
        }

        // XXX: use the only first image, find out what to do with other images
        auto image = images[0];

        rpr_image_format format = {};
        if (image->getPacking() == PACK_SINGLE) {
            format.num_components = 1;
        } else if (image->getPacking() == PACK_DUAL) {
            format.num_components = 2;
        } else if (image->getPacking() == PACK_RGB) {
            format.num_components = 3;
        } else if (image->getPacking() == PACK_RGBA) {
            format.num_components = 4;
        } else {
            throw rpr::Error("Failed to create image: unsupported RAT format");
        }

        if (image->getFormat() == PXL_INT8) {
            format.type = RPR_COMPONENT_TYPE_UINT8;
        } else if (image->getFormat() == PXL_FLOAT16) {
            format.type = RPR_COMPONENT_TYPE_FLOAT16;
        } else if (image->getFormat() == PXL_FLOAT32) {
            format.type = RPR_COMPONENT_TYPE_FLOAT32;
        } else {
            throw rpr::Error("Failed to create image: unsupported RAT format");
        }

        rpr_image_desc desc = GetRprImageDesc(format, image->getXres(), image->getYres());
        if (desc.image_height < 1 ||
            desc.image_width < 1) {
            throw rpr::Error("Failed to create image: incorrect dimensions");
        }

        rpr_image rprImage = nullptr;
        RPR_ERROR_CHECK_THROW(rprContextCreateImage(context->GetHandle(), format, &desc, image->getRawPixels(), &rprImage), "Failed to create image");
        m_rprObjectHandle = rprImage;
        return;
    }
#endif
    rpr_image image = nullptr;
    RPR_ERROR_CHECK_THROW(rprContextCreateImageFromFile(context, path, &image), "Failed to create image from file");
    m_rprObjectHandle = image;
}

Image::Image(Context* context, void const* encodedData, size_t dataSize) {
    throw Error("Image::Image(Context*, void const*, size_t) not implemented. This functionality can be added only with RPR 1.34.3");
}

Image::Image(Context* context, rpr_uint width, rpr_uint height, rpr_image_format format, void const* data)
    : Image(context, GetRprImageDesc(format, width, height), format, data) {

}

Image::Image(Context* context, rpr_image_desc const& desc, rpr_image_format format, void const* data) {
    rpr_image image = nullptr;
    RPR_ERROR_CHECK_THROW(rprContextCreateImage(context->GetHandle(), format, &desc, data, &image), "Failed to create image");
    m_rprObjectHandle = image;
}

Image::Image(Image&& image) noexcept {
    *this = std::move(image);
}

Image& Image::operator=(Image&& image) noexcept {
    Object::operator=(std::move(image));
    return *this;
}

rpr_image_format Image::GetFormat() const {
    rpr_image_format format = {};
    size_t ret;
    RPR_ERROR_CHECK_THROW(rprImageGetInfo(GetHandle(), RPR_IMAGE_FORMAT, sizeof(format), &format, &ret), "Failed to get image format");
    return format;
}

rpr_image_desc Image::GetDesc() const {
    rpr_image_desc desc = {};
    size_t ret;
    RPR_ERROR_CHECK_THROW(rprImageGetInfo(GetHandle(), RPR_IMAGE_DESC, sizeof(desc), &desc, &ret), "Failed to get image desc");
    return desc;
}

rpr_image Image::GetHandle() {
    return static_cast<rpr_image>(m_rprObjectHandle);
}

rpr_image Image::GetHandle() const {
    return static_cast<rpr_image>(m_rprObjectHandle);
}

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE
