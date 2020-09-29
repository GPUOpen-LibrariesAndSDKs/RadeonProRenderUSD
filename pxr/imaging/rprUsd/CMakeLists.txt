set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE rprUsd)

pxr_library(rprUsd
    DISABLE_PRECOMPILED_HEADERS

    LIBRARIES
        gf
        tf
        vt
        hd
        glf
        sdf
        arch
        work
        cpprpr
        MaterialXCore
        MaterialXFormat

    PUBLIC_CLASSES
        util
        contextHelpers
        coreImage
        debugCodes
        imageCache
        material
        materialMappings
        materialRegistry

    PUBLIC_HEADERS
        api.h
        contextMetadata.h
        error.h
        helpers.h
        materialHelpers.h

    RESOURCE_FILES
)

if(NOT RPRMTLXLOADER_PATH)
    set(RPRMTLXLOADER_PATH ${RPR_TOOLS_LOCATION})
endif()

target_sources(rprUsd PRIVATE
    materialNodes/materialNode.h
    materialNodes/usdNode.h
    materialNodes/usdNode.cpp
    materialNodes/mtlxNode.h
    materialNodes/mtlxNode.cpp
    materialNodes/rpr/baseNode.h
    materialNodes/rpr/baseNode.cpp
    materialNodes/rpr/nodeInfo.h
    materialNodes/rpr/catcherNode.cpp
    materialNodes/rpr/displaceNode.cpp
    materialNodes/rpr/materialXNode.cpp
    materialNodes/rpr/arithmeticNode.h
    materialNodes/rpr/arithmeticNode.cpp
    materialNodes/rpr/combineShadersNode.cpp
    materialNodes/houdiniPrincipledShaderNode.h
    materialNodes/houdiniPrincipledShaderNode.cpp
    ${RPRMTLXLOADER_PATH}/rprMtlxLoader.h
    ${RPRMTLXLOADER_PATH}/rprMtlxLoader.cpp)
target_include_directories(rprUsd PRIVATE ${RPRMTLXLOADER_PATH})

add_subdirectory(materialNodes/mtlxFiles)

GroupSources(rprUsd)
