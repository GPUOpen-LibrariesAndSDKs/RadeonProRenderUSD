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
endif(WIN32)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Rif
    REQUIRED_VARS
        RIF_LOCATION_INCLUDE
        RIF_LIBRARY
)