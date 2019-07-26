if (NOT RPR_LOCATION_INCLUDE)
    set (RPR_LOCATION_INCLUDE ${RPR_LOCATION}/inc)
endif ()

if (APPLE)
    if (NOT RPR_LOCATION_LIB)
        set (RPR_LOCATION_LIB ${RPR_LOCATION}/binMacOS)
    endif ()
    set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS} -undefined dynamic_lookup")
elseif (WIN32)
    if (NOT RPR_LOCATION_LIB)
        set (RPR_LOCATION_LIB ${RPR_LOCATION}/libWin64)
	endif ()
    add_definitions(-DWIN32)
elseif (UNIX)
    if (NOT RPR_LOCATION_LIB)
        set (RPR_LOCATION_LIB ${RPR_LOCATION}/binCentOS69)
    endif ()
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

if(WIN32)
    set(RPR_BIN_LOCATION ${RPR_LOCATION}/binWin64)

    if (EXISTS "${RPR_BIN_LOCATION}/Tahoe64.dll")
        set(RPR_TAHOE_BINARY ${RPR_BIN_LOCATION}/Tahoe64.dll)
    endif()

    if (EXISTS "${RPR_BIN_LOCATION}/Hybrid.dll")
        set(RPR_HYBRID_BINARY ${RPR_BIN_LOCATION}/Hybrid.dll)
    endif()

    set(RPR_BINARIES
        ${RPR_BIN_LOCATION}/RadeonProRender64.dll
        ${RPR_BIN_LOCATION}/RprLoadStore64.dll
        ${RPR_TAHOE_BINARY}
        ${RPR_HYBRID_BINARY})
else()
    find_library(RPR_HYBRID_BINARY
        NAMES libHybrid Hybrid
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

if(NOT RPR_TAHOE_BINARY AND NOT RPR_HYBRID_BINARY)
    message(FATAL_ERROR "At least one RPR plugin required")
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Rpr
    REQUIRED_VARS
        RPR_LOCATION_INCLUDE
        RPR_LIBRARY
)