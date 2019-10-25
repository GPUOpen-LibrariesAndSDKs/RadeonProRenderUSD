set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdRpr)
add_custom_target(shared_libs)

set(OptLibs ${ARGN})
set(OptBin ${ARGN})
set(OptIncludeDir ${ARGN})
set(OptClass${ARGN})

if(RPR_ENABLE_OPENVDB_SUPPORT)

	find_package_handle_standard_args(OpenVDB
    REQUIRED_VARS
        OpenVDB_INCLUDE_DIR
		OpenVDB_LIBRARY
		OpenVDB_LIBRARY_DIR
	)

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
    pxOsd)

if(RPR_BUILD_AS_HOUDINI_PLUGIN)
    set(USD_LIB_PREFIX pxr_)
    if(WIN32)
        set(USD_LIB_PREFIX libpxr_)
    endif()

    set(HOUDINI_USD_LIBRARIES "")
    foreach(name ${USD_LIBRARIES})
        list(APPEND HOUDINI_USD_LIBRARIES "${USD_LIB_PREFIX}${name}")
    endforeach()
    set(USD_LIBRARIES "${HOUDINI_USD_LIBRARIES}")

    list(APPEND OptLibs Houdini)
    add_definitions(-DENABLE_RAT)
else(RPR_BUILD_AS_HOUDINI_PLUGIN)
    if(${USD_LIBRARY_MONOLITHIC})
        set(USD_LIBRARIES usd_ms)
    endif()
endif(RPR_BUILD_AS_HOUDINI_PLUGIN)

set(GENERATED_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
    ${CMAKE_CURRENT_BINARY_DIR}/config.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Viewport.ds)
set(GEN_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/python/generateRenderSettingFiles.py)
add_custom_command(
    COMMAND ${PYTHON_EXECUTABLE} ${GEN_SCRIPT} ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${GEN_SCRIPT}
    OUTPUT ${GENERATED_FILES}
    COMMENT "Generating RenderSetting files")

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
		
		${OptLibs}
        Half

	INCLUDE_DIRS
        ${RPR_LOCATION_INCLUDE}
	    ${RIF_LOCATION_INCLUDE}
        ${Boost_INCLUDE_DIRS}
        ${TBB_INCLUDE_DIRS}
        ${GLEW_INCLUDE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
		
		${OptIncludeDir}

    PRIVATE_CLASSES
		rendererPlugin
        renderDelegate
		resourceRegistry
		renderPass
		rprApi
		mesh
		instancer
		material
		materialFactory
		materialAdapter
		lightBase
		domeLight
		rectLight
		sphereLight
        renderBuffer
		basisCurves
        imageCache
		
		${OptClass}

    PRIVATE_HEADERS
        boostIncludePath.h
        api.h
        renderParam.h
        materialFactory.h

    RESOURCE_FILES
        plugInfo.json

    CPPFILES
        ${CMAKE_CURRENT_BINARY_DIR}/config.cpp
)

add_subdirectory(rprcpp)
add_subdirectory(rifcpp)

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
        FILES ${CMAKE_CURRENT_BINARY_DIR}/HdRprPlugin_Viewport.ds
        DESTINATION "houdini/soho/parameters")

    set(RIF_MODELS_INSTALL_DIR "houdini/RIF_models")
    set(RIF_MODELS_VERSIONING_FILE ${CMAKE_CURRENT_BINARY_DIR}/${RIF_VERSION_STRING}.version)
    file(TOUCH ${RIF_MODELS_VERSIONING_FILE})
    install(
        FILES ${RIF_MODELS_VERSIONING_FILE}
        DESTINATION ${RIF_MODELS_INSTALL_DIR})
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

    GetAppDataPath(APP_DATA_PATH)
    set(HDRPR_DATA_PATH "${APP_DATA_PATH}/hdRpr")
    set(RIF_MODELS_INSTALL_DIR "${HDRPR_DATA_PATH}/RIF_models_${RIF_VERSION_STRING}")
endif(RPR_BUILD_AS_HOUDINI_PLUGIN)

install(
    DIRECTORY "${RIF_MODELS_DIR}/"
    DESTINATION ${RIF_MODELS_INSTALL_DIR})

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
    install(
        FILES ${RPR_LIBRARY} ${RPR_PLUGIN_LIBRARIES} ${RIF_LIBRARY}
        DESTINATION ${installPrefix})
endif(WIN32)
