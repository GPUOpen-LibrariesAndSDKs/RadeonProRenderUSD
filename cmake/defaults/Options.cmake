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
option(PXR_STRICT_BUILD_MODE "Turn on additional warnings. Enforce all warnings as errors." OFF)
option(PXR_BUILD_TESTS "Build tests" ON)
option(PXR_ENABLE_PYTHON_SUPPORT "Enable Python based components for USD" ON)
option(PXR_USE_PYTHON_3 "Build Python bindings for Python 3" OFF)
option(PXR_ENABLE_NAMESPACES "Enable C++ namespaces." ON)

option(PXR_SYMLINK_HEADER_FILES "Symlink the header files from, ie, pxr/base/lib/tf to CMAKE_DIR/pxr/base/tf, instead of copying; ensures that you may edit the header file in either location, and improves experience in IDEs which find normally the \"copied\" header, ie, CLion; has no effect on windows" OFF)

# Precompiled headers are a win on Windows, not on gcc.
set(pxr_enable_pch "OFF")
if(MSVC)
    set(pxr_enable_pch "ON")
endif()
option(PXR_ENABLE_PRECOMPILED_HEADERS "Enable precompiled headers." "${pxr_enable_pch}")
set(PXR_PRECOMPILED_HEADER_NAME "pch.h"
    CACHE
    STRING
    "Default name of precompiled header files"
)

set(PXR_INSTALL_LOCATION ""
    CACHE
    STRING
    "Intended final location for plugin resource files."
)

set(PXR_ALL_LIBS ""
    CACHE
    INTERNAL
    "Aggregation of all built libraries."
)
set(PXR_STATIC_LIBS ""
    CACHE
    INTERNAL
    "Aggregation of all built explicitly static libraries."
)
set(PXR_CORE_LIBS ""
    CACHE
    INTERNAL
    "Aggregation of all built core libraries."
)
set(PXR_OBJECT_LIBS ""
    CACHE
    INTERNAL
    "Aggregation of all core libraries built as OBJECT libraries."
)

set(PXR_LIB_PREFIX ${CMAKE_SHARED_LIBRARY_PREFIX}
    CACHE
    STRING
    "Prefix for build library name"
)

option(BUILD_SHARED_LIBS "Build shared libraries." ON)
option(PXR_BUILD_MONOLITHIC "Build a monolithic library." OFF)