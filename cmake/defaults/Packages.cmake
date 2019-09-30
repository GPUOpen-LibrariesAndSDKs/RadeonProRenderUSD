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

find_package(USD REQUIRED)
find_package(Rpr REQUIRED)

# Core USD Package Requirements 
# ----------------------------------------------

# Threads.  Save the libraries needed in PXR_THREAD_LIBS;  we may modify
# them later.  We need the threads package because some platforms require
# it when using C++ functions from #include <thread>.
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
find_package(Threads REQUIRED)
set(PXR_THREAD_LIBS "${CMAKE_THREAD_LIBS_INIT}")

if(RPR_BUILD_AS_HOUDINI_PLUGIN)
    if(WIN32)
        set(PYTHON_INCLUDE_DIRS ${HOUDINI_ROOT}/python27/include)
        set(PYTHON_LIBRARY ${HOUDINI_ROOT}/python27/libs/python27.lib)
    endif()

    find_package(PythonLibs 2.7 REQUIRED)
    find_package(PythonInterp 2.7 REQUIRED)
else(RPR_BUILD_AS_HOUDINI_PLUGIN)
    # --Python.  We are generally but not completely 2.6 compliant.
    # We don't need this flag if we are on 0.8.2.
    if (${USD_VERSION} VERSION_LESS "0.8.2")
        add_definitions(-DPXR_PYTHON_SUPPORT_ENABLED)
    endif ()
    find_package(PythonInterp 2.7 REQUIRED)
    find_package(PythonLibs 2.7 REQUIRED)

    find_package(Boost
        COMPONENTS
            python27
        REQUIRED
    )
endif()

set(hBoost_INCLUDE_DIRS ${HOUDINI_ROOT}/toolkit/include)

set(TBB_INCLUDE_DIR ${HOUDINI_INCLUDE_DIR})
set(TBB_LIBRARY ${HOUDINI_LIB})

find_package(TBB REQUIRED COMPONENTS tbb)
add_definitions(${TBB_DEFINITIONS})

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

if (NOT PXR_MALLOC_LIBRARY)
    if (NOT WIN32)
        message(STATUS "Using default system allocator because PXR_MALLOC_LIBRARY is unspecified")
    endif()
endif()


# Third Party Plugin Package Requirements
# ----------------------------------------------

if(RPR_ENABLE_OPENVDB_SUPPORT)
	find_package(OpenVDB REQUIRED)
endif()

if(RPR_ENABLE_RIF_SUPPORT)
	find_package(Rif REQUIRED)
endif()

# ----------------------------------------------

set(BUILD_SHARED_LIBS "${build_shared_libs}")
