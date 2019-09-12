#include "rifImage.h"

namespace rif {

void ImageUniquePtrDeleter(rif_image* imagePtr) {
    rifObjectDelete(*imagePtr);
}

} // namespace rif
