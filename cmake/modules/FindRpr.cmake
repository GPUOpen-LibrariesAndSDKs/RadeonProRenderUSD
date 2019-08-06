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
)

find_library(RPR_TAHOE_LIBRARY
    NAMES libTahoe64 Tahoe64
    PATHS
        "${RPR_LOCATION_LIB}"
    DOC
        "Radeon ProRender tahoe library path"
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Rpr
    REQUIRED_VARS
        RPR_LOCATION_INCLUDE
        RPR_LIBRARY
        RPR_TAHOE_LIBRARY
)