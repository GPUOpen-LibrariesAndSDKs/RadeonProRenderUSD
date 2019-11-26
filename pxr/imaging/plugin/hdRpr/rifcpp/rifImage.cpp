#include "rifImage.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

Image::Image(rif_image imageHandle)
    : Object(imageHandle) {

}

rif_image_desc Image::GetDesc(uint32_t width, uint32_t height, HdFormat format) {
    if (format == HdFormatInt32) {
        // Emulate integer images using 4 component unsigned char images
        format = HdFormatUNorm8Vec4;
    }

    rif_image_desc imageDesc = {};
    imageDesc.num_components = HdGetComponentCount(format);
    switch (HdGetComponentFormat(format)) {
        case HdFormatUNorm8:
            imageDesc.type = RIF_COMPONENT_TYPE_UINT8;
            break;
        case HdFormatFloat16:
            imageDesc.type = RIF_COMPONENT_TYPE_FLOAT16;
            break;
        case HdFormatFloat32:
            imageDesc.type = RIF_COMPONENT_TYPE_FLOAT32;
            break;
        default:
            imageDesc.type = 0;
            break;
    }
    imageDesc.image_width = width;
    imageDesc.image_height = height;
    imageDesc.image_depth = 1;
    imageDesc.image_row_pitch = width;
    imageDesc.image_slice_pitch = width * height;

    return imageDesc;
}

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE
