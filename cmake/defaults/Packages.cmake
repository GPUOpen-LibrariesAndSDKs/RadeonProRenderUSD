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
    find_package(Houdini REQUIRED CONFIG PATHS ${HOUDINI_ROOT}/toolkit/cmake)

    set(OPENEXR_LOCATION ${Houdini_USD_INCLUDE_DIR})
    set(OPENEXR_LIB_LOCATION ${Houdini_LIB_DIR})
else()
    # We are using python to generate source files
    find_package(PythonInterp 2.7 REQUIRED)
endif()

if (NOT PXR_MALLOC_LIBRARY)
    if (NOT WIN32)
        message(STATUS "Using default system allocator because PXR_MALLOC_LIBRARY is unspecified")
    endif()
endif()

find_package(MaterialX QUIET)

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

if(OpenVDB_FOUND)
    if(HoudiniUSD_FOUND)
        find_package(OpenEXR QUIET COMPONENTS Half_sidefx)
    endif()

    if(NOT OpenEXR_FOUND)
        find_package(OpenEXR QUIET COMPONENTS Half)
    endif()

    if(NOT OpenEXR_FOUND)
        message(FATAL_ERROR "Failed to find Half library")
    endif()
else()
    message(STATUS "Skipping OpenVDB support")
endif()

# ----------------------------------------------

set(BUILD_SHARED_LIBS "${build_shared_libs}")
