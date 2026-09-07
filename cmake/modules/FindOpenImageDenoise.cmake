# Optional OIDN 2 discovery. Prefer the libraries belonging to the selected
# Houdini installation; older Houdini and standalone USD builds may omit OIDN.
find_path(OpenImageDenoise_INCLUDE_DIR
    NAMES OpenImageDenoise/oidn.h
    HINTS "${HOUDINI_ROOT}/toolkit/include" "${OpenImageDenoise_ROOT}/include")
find_library(OpenImageDenoise_LIBRARY
    NAMES OpenImageDenoise
    HINTS "${Houdini_LIB_DIR}" "${HOUDINI_ROOT}/dsolib"
          "${OpenImageDenoise_ROOT}/lib" "${OpenImageDenoise_ROOT}/lib64")

set(OpenImageDenoise_VERSION "")
if(EXISTS "${OpenImageDenoise_INCLUDE_DIR}/OpenImageDenoise/config.h")
    file(STRINGS "${OpenImageDenoise_INCLUDE_DIR}/OpenImageDenoise/config.h"
        _oidn_version REGEX "^#define OIDN_VERSION_STRING ")
    if(_oidn_version)
        string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1"
            OpenImageDenoise_VERSION "${_oidn_version}")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenImageDenoise
    REQUIRED_VARS OpenImageDenoise_INCLUDE_DIR OpenImageDenoise_LIBRARY OpenImageDenoise_VERSION
    VERSION_VAR OpenImageDenoise_VERSION)
mark_as_advanced(OpenImageDenoise_INCLUDE_DIR OpenImageDenoise_LIBRARY)
