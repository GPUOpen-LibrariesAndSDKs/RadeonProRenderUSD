set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdRprUtils)

add_library(hdRprUtils INTERFACE HdRprOptional.hpp)
target_include_directories(hdRprUtils INTERFACE .)
