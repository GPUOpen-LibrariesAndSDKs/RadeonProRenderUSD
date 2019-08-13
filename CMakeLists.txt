project(usdai)

# >=3.1 is required for CMAKE_CXX_STANDARD
if (WIN32)
    cmake_minimum_required(VERSION 3.1.1)
else()
    cmake_minimum_required(VERSION 2.8.8)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/defaults
                      ${CMAKE_SOURCE_DIR}/cmake/modules
                      ${CMAKE_SOURCE_DIR}/cmake/macros)

include(Options)

if(PXR_BUILD_AS_HOUDINI_PLUGIN)
    if(DEFINED ENV{HOUDINI_ROOT})
        set(HOUDINI_ROOT $ENV{HOUDINI_ROOT})
    endif()
    if(NOT DEFINED HOUDINI_ROOT)
        message(FATAL_ERROR "Specify HOUDINI_ROOT")
    endif()

    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        message(STATUS "Setting CMAKE_INSTALL_PREFIX to HOUDINI_ROOT - ${HOUDINI_ROOT}")
        set(CMAKE_INSTALL_PREFIX "${HOUDINI_ROOT}" CACHE PATH "..." FORCE)
    endif()

    if(WIN32)
       set(HOUDINI_LIB ${HOUDINI_ROOT}/custom/houdini/dsolib)
    else()
       set(HOUDINI_LIB ${HOUDINI_ROOT}/dsolib)
    endif()
    add_definitions(-DBUILD_AS_HOUDINI_PLUGIN)
endif(PXR_BUILD_AS_HOUDINI_PLUGIN)

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

# add_compile_options(-Wall -DBUILD_OPTLEVEL_OPT -DBUILD_COMPONENT_SRC_PREFIX="")
set(CMAKE_CXX_STANDARD 11)


include_directories(${USD_INCLUDE_DIR})
link_directories(${USD_LIBRARY_DIR})
include_directories(${RPR_INCLUDE_DIR})
link_directories(${RPR_LIBRARY_DIR})

add_subdirectory(pxr/imaging/plugin/hdRpr)

# install(FILES README.md DESTINATION .)
# install(FILES LICENSE.md DESTINATION .)
