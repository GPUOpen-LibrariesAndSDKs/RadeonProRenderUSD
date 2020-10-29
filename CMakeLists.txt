project(hdRpr)

cmake_minimum_required(VERSION 3.12)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/defaults
                      ${CMAKE_SOURCE_DIR}/cmake/modules
                      ${CMAKE_SOURCE_DIR}/cmake/macros)

include(Options)
include(ProjectDefaults)
include(Packages)

# This has to be defined after Packages is included, because it relies on the
# discovered path to the python executable.
set(PXR_PYTHON_SHEBANG "${PYTHON_EXECUTABLE}"
    CACHE
    STRING
    "Replacement path for Python #! line."
)

# CXXDefaults will set a variety of variables for the project.
# Consume them here. This is an effort to keep the most common
# build files readable.
include(CXXDefaults)
add_definitions(${_PXR_CXX_DEFINITIONS})
set(CMAKE_CXX_FLAGS "${_PXR_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")

include(Public)

set(CMAKE_CXX_STANDARD 14)

if(NOT MaterialX_FOUND)
    # If MaterialX was not explicitly provided, use the one from a submodule
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    if(HoudiniUSD_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Houdini builds with the old ABI. We need to match.
    	set(EXTERNAL_COMPILE_FLAGS "-D_GLIBCXX_USE_CXX11_ABI=0")
    endif()
    set(MATERIALX_PYTHON_LTO OFF)
    set(MATERIALX_INSTALL_PYTHON OFF)
    set(MATERIALX_BUILD_RENDER OFF)
    set(MATERIALX_BUILD_GEN_GLSL OFF)
    set(MATERIALX_BUILD_GEN_OSL OFF)
    set(MATERIALX_BUILD_TESTS OFF)
    add_subdirectory(deps/MaterialX EXCLUDE_FROM_ALL) # EXCLUDE_FROM_ALL allows us to skip installation of mtlx static libraries

    install(
        FILES
            deps/MaterialX/libraries/bxdf/standard_surface.mtlx
            deps/MaterialX/libraries/bxdf/usd_preview_surface.mtlx
        DESTINATION
            libraries/bxdf)
    foreach(mtlx_lib stdlib pbrlib)
        install(
            FILES
                deps/MaterialX/libraries/${mtlx_lib}/${mtlx_lib}_ng.mtlx
                deps/MaterialX/libraries/${mtlx_lib}/${mtlx_lib}_defs.mtlx
            DESTINATION
                libraries/${mtlx_lib})
    endforeach()
endif()

# RPR C++ wrapper
# ----------------------------------------------

if(NOT RPR_TOOLS_LOCATION)
    set(RPR_TOOLS_LOCATION ${RPR_LOCATION_INCLUDE}/../rprTools)
endif()

if(NOT RPRMTLXLOADER_LOCATION)
    set(RPRMTLXLOADER_LOCATION deps/rprMtlxLoader)
endif()

if(NOT RPR_CPP_WRAPPER_LOCATION)
    set(RPR_CPP_WRAPPER_LOCATION ${RPR_TOOLS_LOCATION})
endif()

add_library(cpprpr STATIC
    ${RPRMTLXLOADER_LOCATION}/rprMtlxLoader.h
    ${RPRMTLXLOADER_LOCATION}/rprMtlxLoader.cpp
    ${RPR_CPP_WRAPPER_LOCATION}/RadeonProRender.hpp
    ${RPR_CPP_WRAPPER_LOCATION}/RadeonProRenderCpp.cpp)
set_target_properties(cpprpr PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(cpprpr PUBLIC ${RPR_CPP_WRAPPER_LOCATION} ${RPRMTLXLOADER_LOCATION})
target_link_libraries(cpprpr PUBLIC rpr MaterialXCore MaterialXFormat)
target_compile_definitions(cpprpr PUBLIC
    RPR_CPPWRAPER_DISABLE_MUTEXLOCK
    RPR_API_USE_HEADER_V2)

# ----------------------------------------------

if(HoudiniUSD_FOUND)
    # Currently we use it only in activateHoudiniPlugin utility
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/deps/ghc_filesystem)
endif()

add_subdirectory(pxr/imaging)

install(FILES README.md DESTINATION .)
install(FILES INSTALL.md DESTINATION .)
install(FILES LICENSE.md DESTINATION .)
