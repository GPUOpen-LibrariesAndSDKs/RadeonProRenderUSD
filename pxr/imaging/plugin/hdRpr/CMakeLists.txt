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
    usdImaging
    pxOsd
    cameraUtil)
if(${USD_LIBRARY_MONOLITHIC})
    set(USD_LIBRARIES usd_ms)
endif()

set(_libPrefix ${PXR_LIB_PREFIX})
# UNIX compilers adding "lib" prefix implicitly
if(NOT WIN32)
    string(REGEX REPLACE "^lib" "" _libPrefix "${PXR_LIB_PREFIX}")
endif()

set(_prefixedUsdLibraries "")
foreach(name ${USD_LIBRARIES})
    list(APPEND _prefixedUsdLibraries "${_libPrefix}${name}")
endforeach()
set(USD_LIBRARIES "${_prefixedUsdLibraries}")

set(_sep ${PXR_RESOURCE_FILE_SRC_DST_SEPARATOR})
if(RPR_BUILD_AS_HOUDINI_PLUGIN)
    list(APPEND OptLibs Houdini)
    add_definitions(-DENABLE_RAT)
    set(RESTART_REQUIRED_RESOURCE_FILE images/restartRequired_Houdini.png${_sep}images/restartRequired.png)
else(RPR_BUILD_AS_HOUDINI_PLUGIN)
    add_definitions(-DENABLE_PREFERENCES_FILE)
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

# Currently RPR SDK does not distribute correct c++ wrapper, we do it by ourself instead
set(RPR_CPP_WRAPPER_LOCATION ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT DEFINED RPR_CPP_WRAPPER_LOCATION)
    set(RPR_CPP_WRAPPER_LOCATION ${RPR_TOOLS_LOCATION})
endif()

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
        ${RPR_CPP_WRAPPER_LOCATION}
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
