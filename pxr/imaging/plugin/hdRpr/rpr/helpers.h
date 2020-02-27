#ifndef HDRPR_CORE_HELPERS_H
#define HDRPR_CORE_HELPERS_H

#include "error.h"

namespace rpr {

template <typename T, typename U, typename R>
T GetInfo(U* object, R info) {
    T value = {};
    size_t dummy;
    RPR_ERROR_CHECK_THROW(object->GetInfo(info, sizeof(value), &value, &dummy), "Failed to get object info");
    return value;
}

} // namespace rpr

#endif // HDRPR_CORE_HELPERS_H
