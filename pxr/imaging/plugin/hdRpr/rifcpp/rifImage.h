#ifndef RIFCPP_IMAGE_H
#define RIFCPP_IMAGE_H

#include "rifObject.h"

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

class Image : public Object {
public:
    explicit Image(rif_image imageHandle) {
        SetHandle(imageHandle);
    }

    rif_image GetHandle() {
        return static_cast<rif_image>(m_rifObjectHandle);
    }

    void SetHandle(rif_image image) {
        m_rifObjectHandle = image;
    }
};

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE

#endif //  RIFCPP_IMAGE_H
