#
# Copyright 2016 Pixar
#
# Licensed under the Apache License, Version 2.0 (the "Apache License")
# with the following modification; you may not use this file except in
# compliance with the Apache License and the following modification to it:
# Section 6. Trademarks. is deleted and replaced with:
#
# 6. Trademarks. This License does not grant permission to use the trade
#    names, trademarks, service marks, or product names of the Licensor
#    and its affiliates, except as required to comply with Section 4(c) of
#    the License and to reproduce the content of the NOTICE file.
#
# You may obtain a copy of the Apache License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the Apache License with the above modification is
# distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the Apache License for the specific
# language governing permissions and limitations under the Apache License.
#

# Save the current value of BUILD_SHARED_LIBS and restore it at
# the end of this file, since some of the Find* modules invoked
# below may wind up stomping over this value.
set(build_shared_libs "${BUILD_SHARED_LIBS}")

# USD Arnold Requirements
# ----------------------------------------------

# Try to find monolithic USD
find_package(USDMonolithic QUIET)

if(NOT USDMonolithic_FOUND)
    find_package(pxr CONFIG)

    if(NOT pxr_FOUND)
        # Try to find USD as part of Houdini.
        find_package(HoudiniUSD)

        if(HoudiniUSD_FOUND)
            message(STATUS "Configuring Houdini plugin")
        endif()
    else()
        message(STATUS "Configuring usdview plugin")
    endif()
else()
    message(STATUS "Configuring usdview plugin: monolithic USD")
endif()

if(NOT pxr_FOUND AND NOT HoudiniUSD_FOUND AND NOT USDMonolithic_FOUND)
    message(FATAL_ERROR "Required: USD install or Houdini with included USD.")
endif()

if (NOT PYTHON_EXECUTABLE)
    set(build_schema_python_exec "python")
else()
    set(build_schema_python_exec ${PYTHON_EXECUTABLE})
endif()

if(NOT HoudiniUSD_FOUND)
    list(APPEND CMAKE_PREFIX_PATH ${pxr_DIR})
    find_program(USD_SCHEMA_GENERATOR
        NAMES usdGenSchema.py usdGenSchema
        PATHS ${pxr_DIR}/bin
        REQUIRED
        NO_DEFAULT_PATH)
    if(USD_SCHEMA_GENERATOR)
        list(PREPEND USD_SCHEMA_GENERATOR ${build_schema_python_exec})
    endif()
endif()

if(NOT USD_SCHEMA_GENERATOR)
    message(FATAL_ERROR "Failed to find usd schema generator - usdGenSchema")
endif()

find_package(Rpr REQUIRED)
find_package(Rif REQUIRED)

# Core USD Package Requirements
# ----------------------------------------------

# Threads.  Save the libraries needed in PXR_THREAD_LIBS;  we may modify
# them later.  We need the threads package because some platforms require
# it when using C++ functions from #include <thread>.
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
find_package(Threads REQUIRED)
set(PXR_THREAD_LIBS "${CMAKE_THREAD_LIBS_INIT}")

if(HoudiniUSD_FOUND)
    set(HOUDINI_ROOT "$ENV{HFS}" CACHE PATH "Houdini installation dir")
    set(HOUDINI_CONFIG_DIR ${HOUDINI_ROOT}/toolkit/cmake)
    if(APPLE)
        file(COPY
                ${HOUDINI_ROOT}/toolkit/cmake/HoudiniConfigVersion.cmake
            DESTINATION
                ${CMAKE_CURRENT_BINARY_DIR})
        file(READ ${HOUDINI_ROOT}/toolkit/cmake/HoudiniConfig.cmake HOUDINI_CONFIG)
        string(REPLACE "_houdini_use_framework TRUE" "_houdini_use_framework FALSE" HOUDINI_CONFIG "${HOUDINI_CONFIG}")
        string(REPLACE "\${CMAKE_CURRENT_LIST_DIR}" "\${HOUDINI_ROOT}/toolkit/cmake" HOUDINI_CONFIG "${HOUDINI_CONFIG}")
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/HoudiniConfig.cmake "${HOUDINI_CONFIG}")
        set(HOUDINI_CONFIG_DIR ${CMAKE_CURRENT_BINARY_DIR})
    endif()
    find_package(Houdini REQUIRED CONFIG PATHS ${HOUDINI_CONFIG_DIR})

    set(OPENEXR_LOCATION ${Houdini_USD_INCLUDE_DIR})
    set(OPENEXR_LIB_LOCATION ${Houdini_LIB_DIR})
else()
    # We are using python to generate source files
    find_package(PythonInterp 3.7)

    # If it's not provided externally, consider that it's default USD build and OpenEXR could be found at root
    if (NOT OPENEXR_LOCATION)
        set(OPENEXR_LOCATION ${USD_INCLUDE_DIR}/../)
    endif()
endif()

if (NOT PXR_MALLOC_LIBRARY)
    if (NOT WIN32)
        message(STATUS "Using default system allocator because PXR_MALLOC_LIBRARY is unspecified")
    endif()
endif()

if(NOT MaterialX_FOUND)
    find_package(MaterialX QUIET)
endif()

if(MaterialX_FOUND)
    set(RPR_DISABLE_CUSTOM_MATERIALX_LOADER ON)
endif()

if(RPR_ENABLE_VULKAN_INTEROP_SUPPORT)
    find_package(Vulkan REQUIRED)
endif()

# Third Party Plugin Package Requirements
# ----------------------------------------------

if(HoudiniUSD_FOUND)
    find_package(OpenVDB REQUIRED)
else()
    find_package(OpenVDB QUIET)
endif()

macro(find_exr)
    if(NOT OpenEXR_FOUND)
        set(SIDEFX_COMPONENTS ${ARGV})
        list(TRANSFORM SIDEFX_COMPONENTS APPEND "_sidefx")

        if(HoudiniUSD_FOUND)
            find_package(OpenEXR QUIET COMPONENTS ${SIDEFX_COMPONENTS})
        endif()

        if(NOT OpenEXR_FOUND)
            find_package(OpenEXR QUIET COMPONENTS ${ARGV})
        endif()
    endif()
endmacro()

if (NOT MAYAUSD_OPENEXR_STATIC)
    find_exr(Half IlmImf Iex)
else()
    find_exr(Half IlmImf Iex IlmThread zlib IMath)
endif()

set(RPR_EXR_EXPORT_ENABLED TRUE)

if(HoudiniUSD_FOUND)
    find_exr(OpenEXR OpenEXRCore Iex)
endif()

if(NOT OpenEXR_FOUND)
    find_exr(Half IlmImf Iex)
    if(NOT OpenEXR_FOUND)
        set(RPR_EXR_EXPORT_ENABLED FALSE)
    endif()

    find_exr(Half)

    if(NOT OpenEXR_FOUND)
        message(FATAL_ERROR "Failed to find Half library")
    endif()
endif()

# ----------------------------------------------

set(BUILD_SHARED_LIBS "${build_shared_libs}")
