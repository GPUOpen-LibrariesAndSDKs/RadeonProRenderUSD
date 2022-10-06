/************************************************************************
Copyright 2020 Advanced Micro Devices, Inc
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
************************************************************************/

#include "rifImage.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

Image::Image(rif_image imageHandle)
    : Object(imageHandle) {

}

rif_image_desc Image::GetDesc(uint32_t width, uint32_t height, HdFormat format) {
    if (format == HdFormatInt32Vec4) {
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
    imageDesc.image_row_pitch = 0;
    imageDesc.image_slice_pitch = 0;

    return imageDesc;
}

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE
