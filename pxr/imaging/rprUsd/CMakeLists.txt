set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE rprUsd)

set(RPRUSD_SCHEMA_PYMODULE_CPPFILES
    wrapTokens.cpp
    wrapRendererSettingsAPI.cpp
    wrapObjectSettingsAPI.cpp
    wrapMeshSettingsAPI.cpp
    wrapMaterialSettingsAPI.cpp
    wrapDomeLightSettingsAPI.cpp)
set(RPRUSD_SCHEMA_PUBLIC_HEADERS
    api.h)
set(RPRUSD_SCHEMA_PUBLIC_CLASSES
    tokens
    rendererSettingsAPI
    objectSettingsAPI
    meshSettingsAPI
    materialSettingsAPI
    domeLightSettingsAPI)
set(RPRUSD_SCHEMA_RESOURCE_FILES
    plugInfo.json
    generatedSchema.usda)

foreach(class ${RPRUSD_SCHEMA_PUBLIC_CLASSES})
    list(APPEND RPRUSD_SCHEMA_PUBLIC_HEADERS ${class}.h)
    list(APPEND RPRUSD_SCHEMA_SOURCES  ${class}.cpp)
endforeach()
set(RPRUSD_SCHEMA_DIR ${CMAKE_CURRENT_BINARY_DIR}/schema/)
list(TRANSFORM RPRUSD_SCHEMA_PUBLIC_HEADERS PREPEND ${RPRUSD_SCHEMA_DIR})
list(TRANSFORM RPRUSD_SCHEMA_SOURCES PREPEND ${RPRUSD_SCHEMA_DIR})
list(TRANSFORM RPRUSD_SCHEMA_PYMODULE_CPPFILES PREPEND ${RPRUSD_SCHEMA_DIR})
list(TRANSFORM RPRUSD_SCHEMA_RESOURCE_FILES PREPEND ${RPRUSD_SCHEMA_DIR})

set(USD_SCHEMA_OUTPUT ${RPRUSD_SCHEMA_PUBLIC_HEADERS} ${RPRUSD_SCHEMA_SOURCES} ${RPRUSD_SCHEMA_PYMODULE_CPPFILES} ${RPRUSD_SCHEMA_RESOURCE_FILES})
add_custom_command(
    OUTPUT ${USD_SCHEMA_OUTPUT}
    COMMAND ${USD_SCHEMA_GENERATOR} ${CMAKE_CURRENT_SOURCE_DIR}/schema.usda
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/schema.usda
    WORKING_DIRECTORY ${RPRUSD_SCHEMA_DIR}
    VERBATIM)
add_custom_target(rprUsdSchema DEPENDS ${USD_SCHEMA_OUTPUT})

add_custom_command(
    OUTPUT ${RPRUSD_SCHEMA_DIR}/configured/plugInfo.json
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.cmake
    DEPENDS ${RPRUSD_SCHEMA_DIR}/plugInfo.json
    WORKING_DIRECTORY ${RPRUSD_SCHEMA_DIR}
    VERBATIM)
add_custom_target(rprUsdPlugInfo DEPENDS ${RPRUSD_SCHEMA_DIR}/configured/plugInfo.json)
add_dependencies(rprUsdPlugInfo rprUsdSchema)

pxr_library(rprUsd
    DISABLE_PRECOMPILED_HEADERS

    LIBRARIES
        gf
        tf
        vt
        hd
        usd
        glf
        sdf
        arch
        work
        cpprpr
        json

    PUBLIC_CLASSES
        util
        config
        contextHelpers
        coreImage
        debugCodes
        imageCache
        material
        materialMappings
        materialRegistry

    PUBLIC_HEADERS
        contextMetadata.h
        error.h
        helpers.h
        materialHelpers.h
        boostIncludePath.h
        materialNodes/rpr/materialXNode.h
        ${RPRUSD_SCHEMA_PUBLIC_HEADERS}

    PYTHON_CPPFILES
        moduleDeps.cpp

    PYMODULE_CPPFILES
        module.cpp
        wrapConfig.cpp
        wrapContextHelpers.cpp
        ${RPRUSD_SCHEMA_PYMODULE_CPPFILES}

    PYMODULE_FILES
        __init__.py
        devicesConfiguration.py
)

add_dependencies(rprUsd_headerfiles rprUsdSchema)

if(HoudiniUSD_FOUND)
    target_compile_definitions(rprUsd PUBLIC BUILD_AS_HOUDINI_PLUGIN)
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
    materialNodes/rpr/toonNode.cpp
    materialNodes/rpr/catcherNode.cpp
    materialNodes/rpr/displaceNode.cpp
    materialNodes/rpr/materialXNode.cpp
    materialNodes/rpr/arithmeticNode.h
    materialNodes/rpr/arithmeticNode.cpp
    materialNodes/rpr/combineShadersNode.cpp
    materialNodes/houdiniPrincipledShaderNode.h
    materialNodes/houdiniPrincipledShaderNode.cpp
    ${RPRUSD_SCHEMA_SOURCES}
    ${RPRUSD_SCHEMA_DIR}/configured/plugInfo.json)

add_subdirectory(materialNodes/mtlxFiles)

GroupSources(rprUsd)

if(RPR_ENABLE_VULKAN_INTEROP_SUPPORT)
    target_link_libraries(rprUsd ${Vulkan_LIBRARIES})
    target_include_directories(rprUsd PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_compile_definitions(rprUsd PUBLIC HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT)
endif()

install(
    FILES ${RPRUSD_SCHEMA_DIR}/generatedSchema.usda ${RPRUSD_SCHEMA_DIR}/configured/plugInfo.json
    DESTINATION plugin/usd/rprUsd/resources)
