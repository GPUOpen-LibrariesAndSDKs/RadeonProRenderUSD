if(NOT RIF_LOCATION)
    set(RIF_LOCATION ${PROJECT_SOURCE_DIR}/deps/RIF)
endif()

macro(SET_RIF_VARIABLES dirName)
    if(NOT RIF_LOCATION_LIB)
        set(RIF_LOCATION_LIB ${RIF_LOCATION}/${dirName})
    endif()

    if(NOT RIF_LOCATION_INCLUDE)
        set(RIF_LOCATION_INCLUDE ${RIF_LOCATION}/include)
    endif()
endmacro(SET_RIF_VARIABLES)

if(APPLE)
    SET_RIF_VARIABLES(OSX)
elseif(WIN32)
    SET_RIF_VARIABLES(Windows)
elseif(RPR_SDK_PLATFORM STREQUAL "ubuntu18.04")
    SET_RIF_VARIABLES(Ubuntu18)
else()
    SET_RIF_VARIABLES(CentOS7)
endif()

find_library(RIF_LIBRARY
    NAMES  RadeonImageFilters
    HINTS
        "${RIF_LOCATION_LIB}"
    DOC
        "Radeon Image Filter library path"
    NO_DEFAULT_PATH
    NO_SYSTEM_ENVIRONMENT_PATH
)

if(WIN32)
    set(RIF_BINARIES
        ${RIF_LOCATION_LIB}/MIOpen.dll
        ${RIF_LOCATION_LIB}/RadeonImageFilters.dll
        ${RIF_LOCATION_LIB}/RadeonML_MIOpen.dll
        ${RIF_LOCATION_LIB}/RadeonML_DirectML.dll)
else(WIN32)
    if(APPLE)
        set(RIF_DEPENDENCY_LIBRARIES
            ${RIF_LOCATION_LIB}/libRadeonML_MPS.dylib)
    else()
        set(RIF_DEPENDENCY_LIBRARIES
            ${RIF_LOCATION_LIB}/libMIOpen.so
            ${RIF_LOCATION_LIB}/libRadeonML_MIOpen.so)
    endif(APPLE)
endif(WIN32)

if(NOT DEFINED RIF_MODELS_DIR)
    set(RIF_MODELS_DIR "${RIF_LOCATION}/models")
endif()

set(RIF_VERSION_FILE "${RIF_LOCATION_INCLUDE}/RadeonImageFilters_version.h")
if(NOT EXISTS ${RIF_VERSION_FILE})
    message(FATAL_ERROR "Invalid RIF SDK: missing ${RIF_VERSION_FILE} file")
endif()

file(STRINGS "${RIF_VERSION_FILE}" _rif_major_version_str
     REGEX "^#define[\t ]+RIF_VERSION_MAJOR[\t ]+.*")
file(STRINGS "${RIF_VERSION_FILE}" _rif_minor_version_str
     REGEX "^#define[\t ]+RIF_VERSION_MINOR[\t ]+.*")
file(STRINGS "${RIF_VERSION_FILE}" _rif_revision_version_str
     REGEX "^#define[\t ]+RIF_VERSION_REVISION[\t ]+.*")

string(REGEX REPLACE "^.*MAJOR[\t ]+([0-9]*).*$" "\\1"
       RIF_MAJOR_VERSION "${_rif_major_version_str}")
string(REGEX REPLACE "^.*MINOR[\t ]+([0-9]*).*$" "\\1"
       RIF_MINOR_VERSION "${_rif_minor_version_str}")
string(REGEX REPLACE "^.*REVISION[\t ]+([0-9]*).*$" "\\1"
       RIF_REVISION_VERSION "${_rif_revision_version_str}")

set(RIF_VERSION_STRING "${RIF_MAJOR_VERSION}.${RIF_MINOR_VERSION}.${RIF_REVISION_VERSION}")

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Rif
    REQUIRED_VARS
        RIF_VERSION_STRING
        RIF_LOCATION_INCLUDE
        RIF_LIBRARY
)
