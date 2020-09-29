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
endif()

if(HoudiniUSD_FOUND)
    # Currently we use it only in activateHoudiniPlugin utility
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/deps/ghc_filesystem)
endif()

add_subdirectory(pxr/imaging)

install(FILES README.md DESTINATION .)
install(FILES INSTALL.md DESTINATION .)
install(FILES LICENSE.md DESTINATION .)
