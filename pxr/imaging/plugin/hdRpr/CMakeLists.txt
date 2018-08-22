set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdRpr)
add_custom_target(shared_libs)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Rpr
    REQUIRED_VARS
        RPR_LOCATION_INCLUDE
		RPR_LIBRARY
		RPR_SUPPORT_LIBRARY
)
		
pxr_plugin(hdRpr
   LIBRARIES
		arch
		sdf
		trace
        plug
        tf
        vt
        gf
        work
        hf
        hd
		hdSt
        hdx
        usdLux
		pxOsd
		${RPR_LIBRARY}
        ${RPR_SUPPORT_LIBRARY}
        ${Boost_LIBRARIES}
        ${TBB_LIBRARIES}
        ${GLEW_LIBRARY}
		${OPENGL_LIBRARIES}
		${PYTHON_LIBRARIES}

	INCLUDE_DIRS
        ${RPR_LOCATION_INCLUDE}
        ${Boost_INCLUDE_DIRS}
        ${TBB_INCLUDE_DIRS}
        ${GLEW_INCLUDE_DIR}
		
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
		tokens

    PUBLIC_HEADERS
		api.h
        renderParam.h
		materialFactory.h

    RESOURCE_FILES
        plugInfo.json
)

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

