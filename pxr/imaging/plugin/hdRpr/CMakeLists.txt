set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdRpr)
add_custom_target(shared_libs)

set(OptLibs ${ARGN})
set(OptBin ${ARGN})
set(OptIncludeDir ${ARGN})
set(OptClass${ARGN})

if(RPR_ENABLE_OPENVDB_SUPPORT)
    add_definitions(-DUSE_VOLUME -DOPENVDB_DLL)
    
    set(OptLibs ${OptLibs} ${OpenVDB_LIBRARIES})
    set(OptBin ${OptBin} ${OpenVDB_BINARIES})
    set(OptIncludeDir ${OptIncludeDir} ${OpenVDB_INCLUDE_DIR})
    set(OptClass ${OptClass} field volume)
endif(RPR_ENABLE_OPENVDB_SUPPORT)

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
    usdLux
    usdUtils
    usdRender
    usdGeom
    pxOsd
    cameraUtil)
if(${USD_LIBRARY_MONOLITHIC})
    set(USD_LIBRARIES usd_ms)
endif()

set(_prefixedUsdLibraries "")
foreach(name ${USD_LIBRARIES})
    list(APPEND _prefixedUsdLibraries "${PXR_LIB_PREFIX}${name}")
endforeach()
set(USD_LIBRARIES "${_prefixedUsdLibraries}")

set(_sep ${PXR_RESOURCE_FILE_SRC_DST_SEPARATOR})
if(RPR_BUILD_AS_HOUDINI_PLUGIN)
    list(APPEND OptLibs Houdini)
    add_definitions(-DENABLE_RAT)
    add_definitions(-DHDRPR_DEFAULT_MATERIAL_NETWORK_SELECTOR="karma")
    set(RESTART_REQUIRED_RESOURCE_FILE images/restartRequired_Houdini.png${_sep}images/restartRequired.png)
else(RPR_BUILD_AS_HOUDINI_PLUGIN)
    add_definitions(-DENABLE_PREFERENCES_FILE)
    add_definitions(-DHDRPR_DEFAULT_MATERIAL_NETWORK_SELECTOR="rpr")
    set(RESTART_REQUIRED_RESOURCE_FILE images/restartRequired_Usdview.png${_sep}images/restartRequired.png)
endif(RPR_BUILD_AS_HOUDINI_PLUGIN)

set(GENERATED_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
    ${CMAKE_CURRENT_BINARY_DIR}/config.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Global.ds)
set(GEN_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/python/generateRenderSettingFiles.py)
add_custom_command(
    COMMAND ${PYTHON_EXECUTABLE} ${GEN_SCRIPT} ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${GEN_SCRIPT}
    OUTPUT ${GENERATED_FILES}
    COMMENT "Generating RenderSetting files")

file(GLOB_RECURSE RIF_MODEL_FILES "${RIF_MODELS_DIR}/*")
foreach(file ${RIF_MODEL_FILES})
    file(RELATIVE_PATH rel_file ${RIF_MODELS_DIR} ${file})
    list(APPEND RIF_MODEL_RESOURCE_FILES "${file}${_sep}rif_models/${rel_file}")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/rif_models.version ${RIF_VERSION_STRING})
list(APPEND RIF_MODEL_RESOURCE_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/rif_models.version${_sep}rif_models/rif_models.version")

pxr_plugin(hdRpr
   LIBRARIES
        ${USD_LIBRARIES}
        ${RPR_LIBRARY}
        ${RIF_LIBRARY}
        ${Boost_LIBRARIES}
        ${TBB_tbb_LIBRARY}
        ${GLEW_LIBRARY}
        ${OPENGL_LIBRARIES}
        ${PYTHON_LIBRARIES}
        ${OPENEXR_LIBRARIES}

        ${OptLibs}

    INCLUDE_DIRS
        ${RPR_LOCATION_INCLUDE}
        ${RIF_LOCATION_INCLUDE}
        ${Boost_INCLUDE_DIRS}
        ${TBB_INCLUDE_DIRS}
        ${GLEW_INCLUDE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
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
        mesh
        instancer
        material
        materialFactory
        materialAdapter
        lightBase
        domeLight
        rectLight
        distantLight
        sphereLight
        cylinderLight
        diskLight
        renderBuffer
        basisCurves
        imageCache
        camera
        debugCodes
        
        ${OptClass}

    PRIVATE_HEADERS
        boostIncludePath.h
        api.h
        materialFactory.h

    RESOURCE_FILES
        plugInfo.json
        ${RESTART_REQUIRED_RESOURCE_FILE}
        ${RIF_MODEL_RESOURCE_FILES}

    CPPFILES
        ${CMAKE_CURRENT_BINARY_DIR}/config.cpp
)

add_subdirectory(rprcpp)
add_subdirectory(rifcpp)
add_subdirectory(houdini)

# LINUX is not listed in default system variables (https://cmake.org/cmake/help/3.15/manual/cmake-variables.7.html)
if(NOT DEFINED LINUX)
    if(UNIX AND NOT APPLE AND NOT CYGWIN)
        set(LINUX TRUE)
    else(UNIX AND NOT APPLE AND NOT CYGWIN)
        SET(LINUX FALSE)
    endif(UNIX AND NOT APPLE AND NOT CYGWIN)
endif(NOT DEFINED LINUX)

if(WIN32 OR LINUX)
    add_definitions(-DUSE_GL_INTEROP)
endif(WIN32 OR LINUX)

if(RPR_BUILD_AS_HOUDINI_PLUGIN)
    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Global.ds
        DESTINATION "${HOUDINI_RESOURCE_DIR_RELPATH}/houdini/soho/parameters")
    install(
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/houdini/rprmeshproperties1.hda
        DESTINATION "${HOUDINI_RESOURCE_DIR_RELPATH}/houdini/otls")
else(RPR_BUILD_AS_HOUDINI_PLUGIN)
    _get_install_dir(lib/python/rpr installPrefix)

    install(
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/python/rpr.py
        DESTINATION ${installPrefix}
        RENAME "__init__.py")

    install(
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/python/plugInfo.json
        DESTINATION ${installPrefix})
    install(
        CODE
        "FILE(WRITE \"${CMAKE_INSTALL_PREFIX}/plugin/usd/plugInfo.json\"
        \"
    {
        \\\"Includes\\\": [ \\\"*/resources/\\\" ]
    }
        \")")
endif(RPR_BUILD_AS_HOUDINI_PLUGIN)

if(WIN32)
    install(
        FILES ${RPR_BINARIES} ${RIF_BINARIES} ${OptBin}
        DESTINATION bin)
else(WIN32)
    if(RPR_BUILD_AS_HOUDINI_PLUGIN)
        _get_install_dir("${HOUDINI_PLUGIN_INSTALL_RELPATH}" installPrefix)
    else()
        _get_install_dir("lib" installPrefix)
    endif()

    # install() does not follow symlinks, so we do it manually
    set(RESOLVED_LIBRARIES "")
    foreach (file ${RPR_LIBRARY} ${RPR_PLUGIN_LIBRARIES} ${RIF_LIBRARY} ${RIF_DEPENDENCY_LIBRARIES})
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
        DESTINATION ${installPrefix})
endif(WIN32)

add_subdirectory(package)
