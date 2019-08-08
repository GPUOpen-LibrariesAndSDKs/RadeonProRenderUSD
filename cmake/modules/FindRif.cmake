
if (APPLE)
    set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS} -undefined dynamic_lookup")
elseif (WIN32)
    add_definitions(-DWIN32)
endif()






if (APPLE)
    if (NOT RIF_LOCATION_LIB)
        set (RIF_LOCATION_LIB ${RIF_LOCATION}/OSX/Bin/Release/x64)
    endif ()
    set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS} -undefined dynamic_lookup")

    if (NOT RIF_LOCATION_INCLUDE)
        set (RIF_LOCATION_INCLUDE ${RIF_LOCATION}/OSX/RadeonImageFilters)
    endif ()
elseif (WIN32)
    if (NOT RIF_LOCATION_LIB)
        set (RIF_LOCATION_LIB ${RIF_LOCATION}/Windows/Bin/Release/x64)
	endif ()
	
	if (NOT RIF_LOCATION_INCLUDE)
        set (RIF_LOCATION_INCLUDE ${RIF_LOCATION}/Windows/RadeonImageFilters)
    endif ()
	
    add_definitions(-DWIN32)
elseif (UNIX)
    if (NOT RIF_LOCATION_LIB)
        set (RIF_LOCATION_LIB ${RIF_LOCATION}/CentOS7/Bin/Release/x64)
    endif ()

    if (NOT RIF_LOCATION_INCLUDE)
        set (RIF_LOCATION_INCLUDE ${RIF_LOCATION}/CentOS7/RadeonImageFilters)
    endif ()
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