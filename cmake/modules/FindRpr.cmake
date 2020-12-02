if(TARGET rpr)
    return()
endif()

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
    if(NOT RPR_BIN_LOCATION)
        set(RPR_BIN_LOCATION ${RPR_LOCATION}/binWin64)
    endif()
elseif(RPR_SDK_PLATFORM STREQUAL "ubuntu18.04")
    SET_RPR_VARIABLES(binUbuntu18)
else()
    SET_RPR_VARIABLES(binCentOS7)
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

foreach(entry "Tahoe64;TAHOE" "Northstar64;NORTHSTAR" "Hybrid;HYBRID")
    list(GET entry 0 libName)
    list(GET entry 1 libId)

    if(WIN32)
        set(libPath ${RPR_BIN_LOCATION}/${libName}.dll)
        if (EXISTS "${libPath}")
            set(RPR_${libId}_BINARY ${libPath})
        endif()
    else()
        find_library(RPR_${libId}_BINARY
            NAMES
                ${libName}
                ${libName}${CMAKE_SHARED_LIBRARY_SUFFIX}
            PATHS
                "${RPR_LOCATION_LIB}"
            DOC
                "Radeon ProRender ${libName} library path"
            NO_DEFAULT_PATH
            NO_SYSTEM_ENVIRONMENT_PATH
        )
    endif()

    if(RPR_${libId}_BINARY)
        set(RPR_PLUGINS ${RPR_PLUGINS} ${RPR_${libId}_BINARY})
    endif()
endforeach()

if(NOT RPR_PLUGINS)
    message(FATAL_ERROR "At least one RPR plugin required")
endif()

parseVersion("${RPR_LOCATION_INCLUDE}/RadeonProRender.h" RPR)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Rpr
    REQUIRED_VARS
        RPR_LOCATION_INCLUDE
        RPR_VERSION_STRING
        RPR_LOADSTORE_LIBRARY
        RPR_LIBRARY
)

add_library(rpr INTERFACE)
target_include_directories(rpr INTERFACE ${RPR_LOCATION_INCLUDE})
target_link_libraries(rpr INTERFACE ${RPR_LIBRARY} ${RPR_LOADSTORE_LIBRARY})
