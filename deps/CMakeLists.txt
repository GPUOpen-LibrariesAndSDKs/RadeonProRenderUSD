# MaterialX
# ----------------------------------------------

if(NOT RPR_DISABLE_CUSTOM_MATERIALX_LOADER)
    # If MaterialX was not explicitly provided, use the one from a submodule
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(MATERIALX_PYTHON_LTO OFF)
    set(MATERIALX_BUILD_RENDER OFF)
    set(MATERIALX_BUILD_GEN_GLSL OFF)
    set(MATERIALX_BUILD_GEN_OSL OFF)
    set(MATERIALX_BUILD_TESTS OFF)

    if(MATERIALX_BUILD_PYTHON AND MATERIALX_INSTALL_PYTHON)
        add_subdirectory(MaterialX)
    else()
        add_subdirectory(MaterialX EXCLUDE_FROM_ALL) # EXCLUDE_FROM_ALL allows us to skip installation of mtlx static libraries
    endif()

    set(MTLX_STDLIB_INSTALL_PATH ${CMAKE_INSTALL_PREFIX}/plugin/usd/rprUsd/resources/mtlx)
    install(
        FILES
            MaterialX/libraries/bxdf/standard_surface.mtlx
            MaterialX/libraries/bxdf/usd_preview_surface.mtlx
        DESTINATION
            ${MTLX_STDLIB_INSTALL_PATH}/libraries/bxdf)
    foreach(mtlx_lib stdlib pbrlib)
        install(
            FILES
                MaterialX/libraries/${mtlx_lib}/${mtlx_lib}_ng.mtlx
                MaterialX/libraries/${mtlx_lib}/${mtlx_lib}_defs.mtlx
            DESTINATION
                ${MTLX_STDLIB_INSTALL_PATH}/libraries/${mtlx_lib})
    endforeach()
endif()

# RPR C++ wrapper
# ----------------------------------------------

if(NOT RPR_TOOLS_LOCATION)
    set(RPR_TOOLS_LOCATION ${RPR_LOCATION_INCLUDE}/../rprTools)
endif()

if(NOT RPRMTLXLOADER_LOCATION)
    set(RPRMTLXLOADER_LOCATION rprMtlxLoader)
endif()

if(NOT RPR_CPP_WRAPPER_LOCATION)
    set(RPR_CPP_WRAPPER_LOCATION ${RPR_TOOLS_LOCATION})
endif()

add_library(cpprpr STATIC
    ${RPR_CPP_WRAPPER_LOCATION}/RadeonProRender.hpp
    ${RPR_CPP_WRAPPER_LOCATION}/RadeonProRenderCpp.cpp
    ${RPR_CPP_WRAPPER_LOCATION}/tinyxml2.cpp)
if(NOT RPR_DISABLE_CUSTOM_MATERIALX_LOADER)
    target_sources(cpprpr PRIVATE
        ${RPRMTLXLOADER_LOCATION}/rprMtlxLoader.h
        ${RPRMTLXLOADER_LOCATION}/rprMtlxLoader.cpp)
    target_link_libraries(cpprpr PUBLIC MaterialXCore MaterialXFormat)
    target_compile_definitions(cpprpr PUBLIC -DUSE_CUSTOM_MATERIALX_LOADER)
endif()
set_target_properties(cpprpr PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(cpprpr PUBLIC ${RPRMTLXLOADER_LOCATION} ${RPR_CPP_WRAPPER_LOCATION})
target_link_libraries(cpprpr PUBLIC rpr)
target_compile_definitions(cpprpr PUBLIC
    RPR_CPPWRAPER_DISABLE_MUTEXLOCK
    RPR_API_USE_HEADER_V2)

# json
# ----------------------------------------------

add_library(json INTERFACE)
target_include_directories(json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/json)

# filesystem
# ----------------------------------------------

if(HoudiniUSD_FOUND)
    # Currently we use it only in activateHoudiniPlugin utility
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ghc_filesystem)
endif()

# Murmurhash
# ----------------------------------------------

set(MURMURHASH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/murmurhash/src)
add_library(murmurhash STATIC
    ${MURMURHASH_DIR}/MurmurHash3.h
    ${MURMURHASH_DIR}/MurmurHash3.cpp)
target_include_directories(murmurhash PUBLIC ${MURMURHASH_DIR})
set_target_properties(murmurhash PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ----------------------------------------------
