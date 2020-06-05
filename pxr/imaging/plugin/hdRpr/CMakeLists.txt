set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdRpr)
add_custom_target(shared_libs)

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

set(_sep ${PXR_RESOURCE_FILE_SRC_DST_SEPARATOR})
if(HoudiniUSD_FOUND)
    list(APPEND OptLibs Houdini)
    add_definitions(-DBUILD_AS_HOUDINI_PLUGIN)
    add_definitions(-DENABLE_RAT)
    add_definitions(-DHDRPR_DEFAULT_MATERIAL_NETWORK_SELECTOR="karma")
    set(RESTART_REQUIRED_RESOURCE_FILE images/restartRequired_Houdini.png${_sep}images/restartRequired.png)
else(HoudiniUSD_FOUND)
    add_definitions(-DENABLE_PREFERENCES_FILE)
    add_definitions(-DHDRPR_DEFAULT_MATERIAL_NETWORK_SELECTOR="rpr")
    set(RESTART_REQUIRED_RESOURCE_FILE images/restartRequired_Usdview.png${_sep}images/restartRequired.png)
endif(HoudiniUSD_FOUND)

set(GEN_SCRIPT_PYTHON ${PYTHON_EXECUTABLE})
set(GENERATION_SCRIPTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/python)
set(GEN_SCRIPT ${GENERATION_SCRIPTS_DIR}/generateFiles.py)
set(GEN_SCRIPT_ARGS \"${GENERATION_SCRIPTS_DIR}\" \"${CMAKE_CURRENT_BINARY_DIR}\")
set(GENERATED_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
    ${CMAKE_CURRENT_BINARY_DIR}/config.cpp)
set(GENERATION_DEPENDENT_FILES ${GEN_SCRIPT}
    ${GENERATION_SCRIPTS_DIR}/commonSettings.py
    ${GENERATION_SCRIPTS_DIR}/generateLightSettingFiles.py
    ${GENERATION_SCRIPTS_DIR}/generateRenderSettingFiles.py
    ${GENERATION_SCRIPTS_DIR}/generateGeometrySettingFiles.py)
if(HoudiniUSD_FOUND)
    set(GEN_SCRIPT_PYTHON ${HYTHON_EXECUTABLE})
    set(GEN_SCRIPT_ARGS --houdini_root \"${HOUDINI_ROOT}\" ${GEN_SCRIPT_ARGS})
    set(GENERATED_FILES ${GENERATED_FILES}
        ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Light.ds
        ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Global.ds
        ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Geometry.ds)
    set(GENERATION_DEPENDENT_FILES ${GENERATION_DEPENDENT_FILES}
        ${GENERATION_SCRIPTS_DIR}/houdiniDsGenerator.py)

    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Light.ds
            ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Global.ds
            ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Geometry.ds
        DESTINATION "houdini/soho/parameters")
    install(
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/houdini/rpr_exportRpr1.hda
        DESTINATION "houdini/otls")

    set(PLUG_INFO_ROOT "../../../plugin/usd/hdRpr")
    set(PLUG_INFO_LIBRARY_PATH "../hdRpr${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set(PLUG_INFO_RESOURCE_PATH "resources")
    configure_file(plugInfo.json ${CMAKE_CURRENT_BINARY_DIR}/houdiniPlugInfo.json)
    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/houdiniPlugInfo.json
        DESTINATION houdini/dso/usd_plugins
        RENAME plugInfo.json)
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

if(NOT DEFINED RPR_CPP_WRAPPER_LOCATION)
    set(RPR_CPP_WRAPPER_LOCATION ${RPR_TOOLS_LOCATION})
endif()

if(USDMonolithic_FOUND)
    set(USD_LIBRARIES usd_ms)
else()
    set(USD_LIBRARIES
        ar
        arch
        sdf
        trace
        plug
        tf
        vt
        gf
        glf
        work
        hf
        hd
        hdSt
        hdx
        usdVol
        usdLux
        usdUtils
        usdRender
        usdGeom
        usdImaging
        pxOsd
        cameraUtil)
endif()

pxr_plugin(hdRpr
   LIBRARIES
        ${RPR_LIBRARY}
        ${RPR_LOADSTORE_LIBRARY}
        ${RIF_LIBRARY}
        ${OPENEXR_LIBRARIES}
        ${USD_LIBRARIES}

        ${OptLibs}

    INCLUDE_DIRS
        ${RPR_LOCATION_INCLUDE}
        ${RPR_CPP_WRAPPER_LOCATION}
        ${RIF_LOCATION_INCLUDE}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
        ${OPENEXR_INCLUDE_DIRS}
        
        ${OptIncludeDir}

    PRIVATE_CLASSES
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
        materialFactory
        materialAdapter
        domeLight
        distantLight
        light
        renderBuffer
        basisCurves
        imageCache
        camera
        debugCodes
        primvarUtil
        points
        
        ${OptClass}

    PRIVATE_HEADERS
        boostIncludePath.h
        api.h

    RESOURCE_FILES
        plugInfo.json
        ${RESTART_REQUIRED_RESOURCE_FILE}
        ${RIF_MODEL_RESOURCE_FILES}

    CPPFILES
        ${CMAKE_CURRENT_BINARY_DIR}/config.cpp
)

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

target_compile_definitions(hdRpr PRIVATE
    RPR_CPPWRAPER_DISABLE_MUTEXLOCK
    RPR_API_USE_HEADER_V2)

target_sources(hdRpr PRIVATE
    ${RPR_CPP_WRAPPER_LOCATION}/RadeonProRender.hpp
    ${RPR_CPP_WRAPPER_LOCATION}/RadeonProRenderCpp.cpp)

add_subdirectory(rpr)
add_subdirectory(rifcpp)
add_subdirectory(houdini)
if(NOT HoudiniUSD_FOUND)
    add_subdirectory(usdviewMenu)
endif()

get_target_property(hdRpr_SOURCES hdRpr SOURCES)
foreach(FILE ${hdRpr_SOURCES}) 
    get_filename_component(ABS_FILE ${FILE} ABSOLUTE)
    file(RELATIVE_PATH relPath ${CMAKE_CURRENT_SOURCE_DIR} ${ABS_FILE})

    string(FIND "${relPath}" ".." out)
    if("${out}" EQUAL 0)
        source_group("external" FILES "${FILE}")
    else()
        get_filename_component(PARENT_DIR "${FILE}" DIRECTORY)
        string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")
        string(REPLACE "/" "\\" GROUP "${GROUP}")

        source_group("${GROUP}" FILES "${FILE}")
    endif()
endforeach()

install(
    CODE
    "FILE(WRITE \"${CMAKE_INSTALL_PREFIX}/plugin/plugInfo.json\"
    \"{
    \\\"Includes\\\": [ \\\"usd/*/resources/\\\" ]
}\")")

if(WIN32)
    install(
        FILES ${RPR_BINARIES} ${RIF_BINARIES} ${OptBin}
        DESTINATION bin)
else(WIN32)
    # install() does not follow symlinks, so we do it manually
    set(RESOLVED_LIBRARIES "")
    foreach (file ${RPR_LIBRARY} ${RPR_LOADSTORE_LIBRARY} ${RPR_PLUGIN_LIBRARIES} ${RIF_LIBRARY} ${RIF_DEPENDENCY_LIBRARIES})
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
