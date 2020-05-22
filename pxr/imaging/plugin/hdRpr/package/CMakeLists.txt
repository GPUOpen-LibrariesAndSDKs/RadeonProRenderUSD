if(HoudiniUSD_FOUND)
    install(FILES activateHoudiniPlugin.py DESTINATION .)
endif()

list(GET USD_LIBRARIES 0 USD_LIB)

set(USD_MINOR_VERSION "0")
set(USD_MAJOR_VERSION "0")
get_target_property(USD_INCLUDE_DIRS ${USD_LIB} INTERFACE_INCLUDE_DIRECTORIES)
if(USD_INCLUDE_DIRS)
    foreach(dir ${USD_INCLUDE_DIRS})
        if(EXISTS "${dir}/pxr/pxr.h")
            foreach(_usd_comp MAJOR MINOR PATCH)
                file(STRINGS
                    "${dir}/pxr/pxr.h"
                    _usd_tmp
                    REGEX "#define PXR_${_usd_comp}_VERSION .*$")
                string(REGEX MATCHALL "[0-9]+" USD_${_usd_comp}_VERSION ${_usd_tmp})
            endforeach()
            break()
        endif()
    endforeach()
endif()

if(DUMP_PACKAGE_FILE_NAME)
    if(APPLE)
        set(PACKAGE_PLATFORM "macOS")
    elseif(WIN32)
        set(PACKAGE_PLATFORM "Windows")
    else()
        set(PACKAGE_PLATFORM "${RPR_SDK_PLATFORM}")
    endif()

    set(PACKAGE_NAME hdRpr)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        set(PACKAGE_NAME ${PACKAGE_NAME}-${CMAKE_BUILD_TYPE})
    endif()

    set(PACKAGE_VERSION "${HD_RPR_MAJOR_VERSION}.${HD_RPR_MINOR_VERSION}.${HD_RPR_PATCH_VERSION}")

    if(HoudiniUSD_FOUND)
        set(TARGET "Houdini-${Houdini_VERSION}")
    else()
        set(TARGET "USD-${USD_MINOR_VERSION}.${USD_PATCH_VERSION}")
    endif()

    set(PACKAGE_FILE_NAME "${PACKAGE_NAME}-${PACKAGE_VERSION}-${TARGET}-${PACKAGE_PLATFORM}")

    message(STATUS "PACKAGE_FILE_NAME: ${PACKAGE_FILE_NAME}")
endif()
