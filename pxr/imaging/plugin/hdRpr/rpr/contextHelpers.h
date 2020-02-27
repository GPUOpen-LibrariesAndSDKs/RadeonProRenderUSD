#ifndef HDRPR_CORE_CONTEXT_HELPERS_H
#define HDRPR_CORE_CONTEXT_HELPERS_H

#include "contextMetadata.h"

namespace rpr {

class Context;

Context* CreateContext(char const* cachePath, ContextMetadata* metadata);

} // namespace rpr

#endif // HDRPR_CORE_CONTEXT_HELPERS_H
