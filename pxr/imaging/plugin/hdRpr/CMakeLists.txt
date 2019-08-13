set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdRpr)
add_custom_target(shared_libs)

set(OptLibs ${ARGN})
set(OptBin ${ARGN})
set(OptIncludeDir ${ARGN})
set(OptClass${ARGN})

if(PXR_ENABLE_OPENVDB_SUPPORT)

	find_package_handle_standard_args(OpenVDB
    REQUIRED_VARS
        OpenVDB_INCLUDE_DIR
		OpenVDB_LIBRARY
		OpenVDB_LIBRARY_DIR
	)

	add_definitions(-DUSE_VOLUME -DOPENVDB_3_ABI_COMPATIBLE -DOPENVDB_DLL)
	
	set(OptLibs ${OptLibs} ${OpenVDB_LIBRARIES})
    set(OptBin ${OptBin} ${OpenVDB_BINARIES})
	set(OptIncludeDir ${OptIncludeDir} ${OpenVDB_INCLUDE_DIR})
	set(OptClass ${OptClass} field volume)
	

endif(PXR_ENABLE_OPENVDB_SUPPORT)

if(PXR_ENABLE_RIF_SUPPORT)
	add_definitions(-DUSE_RIF )
	
	set(OptLibs  ${OptLibs} ${RIF_LIBRARY})
    set(OptBin ${OptBin} ${RIF_BINARIES})
	set(OptIncludeDir  ${OptIncludeDir} ${RIF_LOCATION_INCLUDE})
	set(OptClass  ${OptClass} ImageFilter)
	

endif(PXR_ENABLE_RIF_SUPPORT)

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

if(PXR_BUILD_AS_HOUDINI_PLUGIN)
    set(BUILD_TYPE_SPECIFIC_INCLUDE_DIRS
        ${hBoost_INCLUDE_DIRS})

    if(WIN32)
        set(HOUDINI_USD_LIBRARIES "")
        foreach(name ${USD_LIBRARIES})
            list(APPEND HOUDINI_USD_LIBRARIES "libpxr_${name}")
        endforeach()
        set(USD_LIBRARIES "${HOUDINI_USD_LIBRARIES}")
    else()
        message(FATAL_ERROR "Implement me")
    endif()
else(PXR_BUILD_AS_HOUDINI_PLUGIN)
    set(BUILD_TYPE_SPECIFIC_INCLUDE_DIRS
        ${Boost_INCLUDE_DIRS})

    if(${USD_LIBRARY_MONOLITHIC})
        set(USD_LIBRARIES usd_ms)
    endif()
endif(PXR_BUILD_AS_HOUDINI_PLUGIN)

pxr_plugin(hdRpr
   LIBRARIES
        ${USD_LIBRARIES}
        ${RPR_LIBRARY}
        ${Boost_LIBRARIES}
        ${TBB_LIBRARIES}
        ${GLEW_LIBRARY}
		${OPENGL_LIBRARIES}
		${PYTHON_LIBRARIES}
		
		${OptLibs}
	

	INCLUDE_DIRS
        ${RPR_LOCATION_INCLUDE}
        ${BUILD_TYPE_SPECIFIC_INCLUDE_DIRS}
        ${TBB_INCLUDE_DIRS}
        ${GLEW_INCLUDE_DIR}
		
		${OptIncludeDir}
				
    PUBLIC_CLASSES
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
		tokens
		
		${OptClass}

    PUBLIC_HEADERS
		api.h
        renderParam.h
		materialFactory.h

    RESOURCE_FILES
        plugInfo.json
)

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

if (APPLE)
    add_custom_command(TARGET hdRpr
        POST_BUILD COMMAND
        ${CMAKE_INSTALL_NAME_TOOL} -id @loader_path/libRadeonProRender64.dylib  ${RPR_LIBRARY})

    add_custom_command(TARGET hdRpr
        POST_BUILD COMMAND
        ${CMAKE_INSTALL_NAME_TOOL} -change @loader_path/libRadeonProRender64.dylib ${RPR_LIBRARY} $<TARGET_FILE:hdRpr> )
endif(APPLE)

if(NOT PXR_BUILD_AS_HOUDINI_PLUGIN)
_get_install_dir(lib/python/rpr installPrefix)

install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/python/rpr.py
    DESTINATION ${installPrefix}
    RENAME "__init__.py"
    )
    
install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/python/plugInfo.json
    DESTINATION ${installPrefix}
    )
install(
    CODE
    "FILE(WRITE \"${CMAKE_INSTALL_PREFIX}/plugin/usd/plugInfo.json\"
    \"
{
    \\\"Includes\\\": [ \\\"*/resources/\\\" ]
}
    \")")
endif(NOT PXR_BUILD_AS_HOUDINI_PLUGIN)

if(WIN32)
    install(
        FILES ${RPR_BINARIES} ${OptBin}
        DESTINATION bin)
else(WIN32)
    install(
        FILES ${RPR_LIBRARY} ${RPR_PLUGIN_LIBRARIES} ${OptLibs}
        DESTINATION lib )
endif(WIN32)
