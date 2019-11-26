#ifndef RIFCPP_IMAGE_H
#define RIFCPP_IMAGE_H

#include "rifObject.h"
#include "pxr/imaging/hd/types.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

class Image : public Object {
public:
    static rif_image_desc GetDesc(uint32_t width, uint32_t height, HdFormat format);

    explicit Image(rif_image imageHandle);

    rif_image GetHandle() { return static_cast<rif_image>(m_rifObjectHandle); }
};

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE

#endif //  RIFCPP_IMAGE_H
