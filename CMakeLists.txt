project(hdRpr)

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

if(RPR_BUILD_AS_HOUDINI_PLUGIN)
    include(Houdini)
endif(RPR_BUILD_AS_HOUDINI_PLUGIN)

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
if(RPR_BUILD_AS_HOUDINI_PLUGIN)
	# We use HDK to load .rat images, it requires 14th standard
	set(CMAKE_CXX_STANDARD 14)
else()
	set(CMAKE_CXX_STANDARD 11)
endif()


include_directories(${USD_INCLUDE_DIR})
link_directories(${USD_LIBRARY_DIR})
include_directories(${RPR_INCLUDE_DIR})
link_directories(${RPR_LIBRARY_DIR})

add_subdirectory(pxr/imaging/plugin/hdRpr)

# install(FILES README.md DESTINATION .)
# install(FILES LICENSE.md DESTINATION .)
