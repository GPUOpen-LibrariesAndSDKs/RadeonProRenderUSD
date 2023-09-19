set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdRpr)

set(OptLibs ${ARGN})
set(OptBin ${ARGN})
set(OptIncludeDir ${ARGN})
set(OptClass${ARGN})

if(OpenVDB_FOUND)
    add_definitions(-DUSE_VOLUME -DOPENVDB_DLL)
    
    set(OptLibs ${OptLibs} ${OpenVDB_LIBRARIES})
    set(OptBin ${OptBin} ${OpenVDB_BINARIES})
    set(OptIncludeDir ${OptIncludeDir} ${OpenVDB_INCLUDE_DIR})
    set(OptClass ${OptClass} field volume)
endif(OpenVDB_FOUND)

find_package(OpenMP)
if (OPENMP_FOUND)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

set(_sep ${PXR_RESOURCE_FILE_SRC_DST_SEPARATOR})
if(HoudiniUSD_FOUND)
    list(APPEND OptLibs Houdini)
    add_definitions(-DENABLE_MULTITHREADED_RENDER_BUFFER)
    add_definitions(-DHDRPR_DEFAULT_MATERIAL_NETWORK_SELECTOR="karma")
    set(RESTART_REQUIRED_RESOURCE_FILE images/restartRequired_Houdini.png${_sep}images/restartRequired.png)
    set(SCRIPT_RESOURCE_FILES scripts/rendersettings_OnLoaded.py${_sep}scripts/rendersettings_OnLoaded.py)
else(HoudiniUSD_FOUND)
    add_definitions(-DHDRPR_DEFAULT_MATERIAL_NETWORK_SELECTOR="rpr")
    set(RESTART_REQUIRED_RESOURCE_FILE images/restartRequired_Usdview.png${_sep}images/restartRequired.png)
    set(SCRIPT_RESOURCE_FILES)
endif(HoudiniUSD_FOUND)

set(GEN_SCRIPT_PYTHON ${PYTHON_EXECUTABLE})
set(GENERATION_SCRIPTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/python)
set(GEN_SCRIPT ${GENERATION_SCRIPTS_DIR}/generateFiles.py)
set(GEN_SCRIPT_ARGS \"${CMAKE_CURRENT_BINARY_DIR}\")
set(GENERATED_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
    ${CMAKE_CURRENT_BINARY_DIR}/config.cpp)
set(GENERATION_DEPENDENT_FILES ${GEN_SCRIPT}
    ${GENERATION_SCRIPTS_DIR}/commonSettings.py
    ${GENERATION_SCRIPTS_DIR}/generateLightSettingFiles.py
    ${GENERATION_SCRIPTS_DIR}/generateRenderSettingFiles.py
    ${GENERATION_SCRIPTS_DIR}/generateGeometrySettingFiles.py)
if(HoudiniUSD_FOUND)
    set(GEN_SCRIPT_ARGS --for_houdini ${GEN_SCRIPT_ARGS})
    set(GENERATED_FILES ${GENERATED_FILES}
        ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Light.ds
        ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Global.ds
        ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Viewport.ds
        ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Geometry.ds)
    set(GENERATION_DEPENDENT_FILES ${GENERATION_DEPENDENT_FILES}
        ${GENERATION_SCRIPTS_DIR}/houdiniDsGenerator.py)

    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Light.ds
            ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Global.ds
            ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Viewport.ds
            ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Geometry.ds
        DESTINATION "houdini/soho/parameters")

    install(
        CODE
        "FILE(WRITE \"${CMAKE_INSTALL_PREFIX}/houdini/dso/usd_plugins/plugInfo.json\"
        \"{
        \\\"Includes\\\": [ \\\"../../../plugin/\\\" ]
    }\")")
endif()

add_custom_command(
    COMMAND ${GEN_SCRIPT_PYTHON} ${GEN_SCRIPT} ${GEN_SCRIPT_ARGS}
    DEPENDS ${GENERATION_DEPENDENT_FILES}
    OUTPUT ${GENERATED_FILES}
    COMMENT "Generating files")

file(GLOB_RECURSE RIF_MODEL_FILES "${RIF_MODELS_DIR}/*")
foreach(file ${RIF_MODEL_FILES})
    file(RELATIVE_PATH rel_file ${RIF_MODELS_DIR} ${file})
    list(APPEND RIF_MODEL_RESOURCE_FILES "${file}${_sep}rif_models/${rel_file}")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/rif_models.version ${RIF_VERSION_STRING})
list(APPEND RIF_MODEL_RESOURCE_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/rif_models.version${_sep}rif_models/rif_models.version")

pxr_plugin(hdRpr
    DISABLE_PRECOMPILED_HEADERS

    LIBRARIES
        ar
        trace
        plug
        tf
        gf
        hf
        ndr
        usdVol
        usdLux
        usdUtils
        usdRender
        usdGeom
        usdImaging
        pxOsd
        cameraUtil
        rprUsd
        json
        murmurhash
        ${RIF_LIBRARY}
        ${OPENEXR_LIBRARIES}

        ${OptLibs}

    INCLUDE_DIRS
        ${RIF_LOCATION_INCLUDE}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
        ${OPENEXR_INCLUDE_DIRS}
        
        ${OptIncludeDir}
        ${PROJECT_SOURCE_DIR}/deps/ghc_filesystem/include

    PRIVATE_CLASSES
        aovDescriptor
        rendererPlugin
        renderDelegate
        renderPass
        renderThread
        renderParam
        rprApi
        rprApiAov
        rprApiFramebuffer
        mesh
        instancer
        material
        domeLight
        distantLight
        light
        renderBuffer
        basisCurves
        camera
        debugCodes
        primvarUtil
        points
        
        ${OptClass}

    PRIVATE_HEADERS
        baseRprim.h
        api.h

    RESOURCE_FILES
        plugInfo.json
        ${RESTART_REQUIRED_RESOURCE_FILE}
        ${RIF_MODEL_RESOURCE_FILES}
        ${SCRIPT_RESOURCE_FILES}

    CPPFILES
        ${CMAKE_CURRENT_BINARY_DIR}/config.cpp
        ndrDiscoveryPlugin.cpp
        ndrParserPlugin.cpp
)

if(RPR_EXR_EXPORT_ENABLED)
    target_compile_definitions(hdRpr PRIVATE -DRPR_EXR_EXPORT_ENABLED)
endif()

if(RPR_ENABLE_VULKAN_INTEROP_SUPPORT)
    target_link_libraries(hdRpr ${Vulkan_LIBRARY})
    target_include_directories(hdRpr PRIVATE ${Vulkan_INCLUDE_DIR})
    target_compile_definitions(hdRpr PUBLIC HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT)
endif()

target_sources(hdRpr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/notify/message.h)
if(APPLE)
    target_sources(hdRpr PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/notify/message.mm)
    target_link_libraries(hdRpr
        objc
        "-framework AppKit")
else()
    target_sources(hdRpr PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/notify/message.cpp)
endif()

add_subdirectory(rifcpp)
add_subdirectory(houdini)
if(NOT HoudiniUSD_FOUND)
    add_subdirectory(usdviewMenu)
endif()

GroupSources(hdRpr)

install(
    CODE
    "FILE(WRITE \"${CMAKE_INSTALL_PREFIX}/plugin/plugInfo.json\"
    \"{
    \\\"Includes\\\": [ \\\"usd/*/resources/\\\" ]
}\")")

if(WIN32)
    set(RPR_BINARIES ${RPR_BIN_LOCATION}/RadeonProRender64.dll)
    if(RPR_LOADSTORE_LIBRARY)
        list(APPEND RPR_BINARIES ${RPR_BIN_LOCATION}/RprLoadStore64.dll)
    endif()
    install(
        FILES ${RPR_BINARIES} ${RPR_PLUGINS} ${RIF_BINARIES} ${OptBin}
        DESTINATION lib)
else(WIN32)
    # install() does not follow symlinks, so we do it manually
    set(RESOLVED_LIBRARIES "")
    foreach (file ${RPR_LIBRARY} ${RPR_LOADSTORE_LIBRARY} ${RPR_PLUGINS} ${RIF_LIBRARY} ${RIF_DEPENDENCY_LIBRARIES})
        while(IS_SYMLINK ${file})
            file(READ_SYMLINK ${file} symfile)
            if(NOT IS_ABSOLUTE "${symfile}")
                get_filename_component(dir "${file}" DIRECTORY)
                set(symfile "${dir}/${symfile}")
            endif()
            list(APPEND RESOLVED_LIBRARIES ${file})
            set(file "${symfile}")
        endwhile()
        list(APPEND RESOLVED_LIBRARIES ${file})
    endforeach()

    install(
        FILES ${RESOLVED_LIBRARIES}
        DESTINATION lib)
endif(WIN32)

add_subdirectory(package)
