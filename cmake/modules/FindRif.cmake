if(NOT RIF_LOCATION)
    set(RIF_LOCATION ${PROJECT_SOURCE_DIR}/deps/RIF)
endif()

macro(SET_RIF_VARIABLES dirName)
    if(NOT RIF_LOCATION_LIB)
        set(RIF_LOCATION_LIB ${RIF_LOCATION}/${dirName}/Dynamic)
    endif()

    if(NOT RIF_LOCATION_INCLUDE)
        set(RIF_LOCATION_INCLUDE ${RIF_LOCATION}/include)
    endif()
endmacro(SET_RIF_VARIABLES)

if(APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
        SET_RIF_VARIABLES(OSX)
    else()
        SET_RIF_VARIABLES(MacOS_ARM)
    endif()
elseif(WIN32)
    SET_RIF_VARIABLES(Windows)
elseif(RPR_SDK_PLATFORM STREQUAL "ubuntu18.04")
    SET_RIF_VARIABLES(Ubuntu18)
elseif(RPR_SDK_PLATFORM STREQUAL "ubuntu20.04")
    SET_RIF_VARIABLES(Ubuntu20)
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
        ${RIF_LOCATION_LIB}/dxcompiler.dll
	${RIF_LOCATION_LIB}/dxil.dll
        ${RIF_LOCATION_LIB}/MIOpen.dll
        ${RIF_LOCATION_LIB}/RadeonImageFilters.dll
        ${RIF_LOCATION_LIB}/RadeonML.dll
        ${RIF_LOCATION_LIB}/RadeonML_MIOpen.dll
        ${RIF_LOCATION_LIB}/RadeonML_DirectML.dll)
else(WIN32)
    if(APPLE)
        set(RIF_DEPENDENCY_LIBRARIES
            ${RIF_LOCATION_LIB}/libRadeonML.dylib
            ${RIF_LOCATION_LIB}/libRadeonML_MPS.dylib)
    else()
        set(RIF_DEPENDENCY_LIBRARIES
            ${RIF_LOCATION_LIB}/libMIOpen.so
            ${RIF_LOCATION_LIB}/libRadeonML_MIOpen.so
            ${RIF_LOCATION_LIB}/libRadeonML.so.0)
    endif(APPLE)
endif(WIN32)

if(NOT DEFINED RIF_MODELS_DIR)
    set(RIF_MODELS_DIR "${RIF_LOCATION}/models")
endif()

parseVersion("${RIF_LOCATION_INCLUDE}/RadeonImageFilters_version.h" RIF)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Rif
    REQUIRED_VARS
        RIF_LOCATION_INCLUDE
        RIF_VERSION_STRING
        RIF_LIBRARY
)
