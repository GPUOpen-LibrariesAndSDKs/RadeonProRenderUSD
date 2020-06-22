if(NOT RPR_LOCATION)
    set(RPR_LOCATION ${PROJECT_SOURCE_DIR}/deps/RPR/RadeonProRender)
endif()

macro(SET_RPR_VARIABLES dirName)
    if(NOT RPR_LOCATION_LIB)
        set(RPR_LOCATION_LIB ${RPR_LOCATION}/${dirName})
    endif()

    if(NOT RPR_LOCATION_INCLUDE)
        set(RPR_LOCATION_INCLUDE ${RPR_LOCATION}/inc)
    endif()
endmacro(SET_RPR_VARIABLES)

if(APPLE)
    SET_RPR_VARIABLES(binMacOS)
elseif(WIN32)
    SET_RPR_VARIABLES(libWin64)
elseif(RPR_SDK_PLATFORM STREQUAL "ubuntu18.04")
    SET_RPR_VARIABLES(binUbuntu18)
elseif(RPR_SDK_PLATFORM STREQUAL "centos7")
    SET_RPR_VARIABLES(binCentOS7)
else()
    message(FATAL_ERROR "Unknown platform: ${RPR_SDK_PLATFORM}")
endif()

find_library(RPR_LIBRARY
    NAMES  libRadeonProRender64 RadeonProRender64
    PATHS
        "${RPR_LOCATION_LIB}"
    DOC
        "Radeon ProRender library path"
    NO_DEFAULT_PATH
    NO_SYSTEM_ENVIRONMENT_PATH
)

find_library(RPR_LOADSTORE_LIBRARY
    NAMES  libRprLoadStore64 RprLoadStore64
    PATHS
        "${RPR_LOCATION_LIB}"
    DOC
        "Radeon ProRender loadstore library path"
    NO_DEFAULT_PATH
    NO_SYSTEM_ENVIRONMENT_PATH
)

if(WIN32)
    if(NOT RPR_BIN_LOCATION)
        set(RPR_BIN_LOCATION ${RPR_LOCATION}/binWin64)
    endif()

    if (EXISTS "${RPR_BIN_LOCATION}/Tahoe64.dll")
        set(RPR_TAHOE_BINARY ${RPR_BIN_LOCATION}/Tahoe64.dll)
    endif()

    if (EXISTS "${RPR_BIN_LOCATION}/Hybrid.dll")
        set(RPR_HYBRID_BINARY ${RPR_BIN_LOCATION}/Hybrid.dll)
    endif()

    if (EXISTS "${RPR_BIN_LOCATION}/Northstar64.dll")
        set(RPR_NORTHSTAR_BINARY ${RPR_BIN_LOCATION}/Northstar64.dll)
    endif()

    set(RPR_BINARIES
        ${RPR_BIN_LOCATION}/RadeonProRender64.dll
        ${RPR_BIN_LOCATION}/RprLoadStore64.dll
        ${RPR_TAHOE_BINARY}
        ${RPR_HYBRID_BINARY}
        ${RPR_NORTHSTAR_BINARY})
else()
    find_library(RPR_HYBRID_BINARY
        NAMES Hybrid${CMAKE_SHARED_LIBRARY_SUFFIX}
        PATHS
            "${RPR_LOCATION_LIB}"
        DOC
            "Radeon ProRender hybrid library path"
        NO_DEFAULT_PATH
        NO_SYSTEM_ENVIRONMENT_PATH
    )
    if(RPR_HYBRID_BINARY)
        set(RPR_PLUGIN_LIBRARIES ${RPR_HYBRID_BINARY})
    endif()

    find_library(RPR_TAHOE_BINARY
        NAMES libTahoe64 Tahoe64
        PATHS
            "${RPR_LOCATION_LIB}"
        DOC
            "Radeon ProRender tahoe library path"
        NO_DEFAULT_PATH
        NO_SYSTEM_ENVIRONMENT_PATH
    )
    if(RPR_TAHOE_BINARY)
        set(RPR_PLUGIN_LIBRARIES ${RPR_PLUGIN_LIBRARIES} ${RPR_TAHOE_BINARY})
    endif()
endif(WIN32)

if(NOT RPR_TAHOE_BINARY AND NOT RPR_HYBRID_BINARY AND NOT RPR_NORTHSTAR_BINARY)
    message(FATAL_ERROR "At least one RPR plugin required")
endif()

if(NOT DEFINED RPR_TOOLS_LOCATION)
    set(RPR_TOOLS_LOCATION ${RPR_LOCATION_INCLUDE}/../rprTools)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Rpr
    REQUIRED_VARS
        RPR_LOCATION_INCLUDE
        RPR_LOADSTORE_LIBRARY
        RPR_LIBRARY
)
