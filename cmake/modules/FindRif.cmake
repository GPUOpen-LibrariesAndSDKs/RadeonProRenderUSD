macro(SET_RIF_VARIABLES dirName)
    if(NOT RIF_LOCATION_LIB)
        set(RIF_LOCATION_LIB ${RIF_LOCATION}/${dirName}/Bin/Release/x64)
    endif()

    if(NOT RIF_LOCATION_INCLUDE)
        set(RIF_LOCATION_INCLUDE ${RIF_LOCATION}/${dirName}/RadeonImageFilters)
    endif()
endmacro(SET_RIF_VARIABLES)

if(APPLE)
    SET_RIF_VARIABLES(OSX)
elseif(WIN32)
    SET_RIF_VARIABLES(Windows)
elseif(RPR_SDK_PLATFORM STREQUAL "ubuntu18.04")
    SET_RIF_VARIABLES(Ubuntu18)
elseif(RPR_SDK_PLATFORM STREQUAL "ubuntu16.04")
    SET_RIF_VARIABLES(Ubuntu)
elseif(RPR_SDK_PLATFORM STREQUAL "centos7")
    SET_RIF_VARIABLES(CentOS7)
endif()

find_library(RIF_LIBRARY
    NAMES  RadeonImageFilters64
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
        ${RIF_LOCATION_LIB}/RadeonImageFilters64.dll
        ${RIF_LOCATION_LIB}/RadeonML-MIOpen.dll
        ${RIF_LOCATION_LIB}/RadeonML-DirectML.dll)
else(WIN32)
    if(NOT APPLE)
        set(RIF_DEPENDENCY_LIBRARIES
            ${RIF_LOCATION_LIB}/libMIOpen.so
            ${RIF_LOCATION_LIB}/libRadeonML-MIOpen.so)
    endif(NOT APPLE)
endif(WIN32)

if(NOT DEFINED RIF_MODELS_DIR)
    set(RIF_MODELS_DIR "${RIF_LOCATION}/models")
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Rif
    REQUIRED_VARS
        RIF_LOCATION_INCLUDE
        RIF_LIBRARY
)

set(RIF_VERSION_FILE "${RIF_LOCATION_INCLUDE}/RadeonImageFilters.h")

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
