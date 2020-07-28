# USD 20.05 does not generate cmake config for the monolithic library as it does for the default mode
# So we have to handle monolithic USD in a special way

find_path(USD_INCLUDE_DIR pxr/pxr.h
    PATHS ${pxr_DIR}/include
          $ENV{pxr_DIR}/include
    DOC "USD Include directory"
    NO_DEFAULT_PATH
    NO_SYSTEM_ENVIRONMENT_PATH)

find_path(USD_LIBRARY_DIR
    NAMES "${PXR_LIB_PREFIX}usd_ms${CMAKE_SHARED_LIBRARY_SUFFIX}"
    PATHS ${pxr_DIR}/lib
          $ENV{pxr_DIR}/lib
    DOC "USD Libraries directory"
    NO_DEFAULT_PATH
    NO_SYSTEM_ENVIRONMENT_PATH)

find_library(USD_MONOLITHIC_LIBRARY
    NAMES ${PXR_LIB_PREFIX}usd_ms
    PATHS ${USD_LIBRARY_DIR}
    NO_DEFAULT_PATH
    NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(USDMonolithic
    USD_INCLUDE_DIR
    USD_LIBRARY_DIR
    USD_MONOLITHIC_LIBRARY)

if(USDMonolithic_FOUND)
    list(APPEND CMAKE_PREFIX_PATH ${pxr_DIR})

    # --Python.
    if(PXR_USE_PYTHON_3)
        find_package(PythonInterp 3.0 REQUIRED)
        find_package(PythonLibs 3.0 REQUIRED)
    else()
        find_package(PythonInterp 2.7 REQUIRED)
        find_package(PythonLibs 2.7 REQUIRED)
    endif()

    # Set up a version string for comparisons. This is available
    # as Boost_VERSION_STRING in CMake 3.14+
    set(boost_version_string "${Boost_MAJOR_VERSION}.${Boost_MINOR_VERSION}.${Boost_SUBMINOR_VERSION}")

    if (((${boost_version_string} VERSION_GREATER_EQUAL "1.67") AND
         (${boost_version_string} VERSION_LESS "1.70")) OR
        ((${boost_version_string} VERSION_GREATER_EQUAL "1.70") AND
          Boost_NO_BOOST_CMAKE))
        # As of boost 1.67 the boost_python component name includes the
        # associated Python version (e.g. python27, python36). After boost 1.70
        # the built-in cmake files will deal with this. If we are using boost
        # that does not have working cmake files, or we are using a new boost
        # and not using cmake's boost files, we need to do the below.
        #
        # Find the component under the versioned name and then set the generic
        # Boost_PYTHON_LIBRARY variable so that we don't have to duplicate this
        # logic in each library's CMakeLists.txt.
        set(python_version_nodot "${PYTHON_VERSION_MAJOR}${PYTHON_VERSION_MINOR}")
        find_package(Boost
            COMPONENTS
                python${python_version_nodot}
            REQUIRED
        )
        set(Boost_PYTHON_LIBRARY "${Boost_PYTHON${python_version_nodot}_LIBRARY}")
    else()
        find_package(Boost
            COMPONENTS
                python
            REQUIRED
        )
    endif()

    add_library(usd_ms SHARED IMPORTED)
    set_property(TARGET usd_ms APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)

    target_compile_definitions(usd_ms INTERFACE
        -DPXR_PYTHON_ENABLED=1)
    target_link_libraries(usd_ms INTERFACE
        ${USD_MONOLITHIC_LIBRARY}
        ${Boost_LIBRARIES}
        ${PYTHON_LIBRARIES})
    target_link_directories(usd_ms INTERFACE
        ${USD_LIBRARY_DIR})
    target_include_directories(usd_ms INTERFACE
        ${USD_INCLUDE_DIR}
        ${Boost_INCLUDE_DIRS})
    set_target_properties(usd_ms PROPERTIES
      IMPORTED_IMPLIB_RELEASE "${USD_MONOLITHIC_LIBRARY}"
      IMPORTED_LOCATION_RELEASE "${USD_LIBRARY_DIR}/${PXR_LIB_PREFIX}usd_ms${CMAKE_SHARED_LIBRARY_SUFFIX}"
    )

    foreach(targetName
        arch tf gf js trace work plug vt ar kind sdf ndr sdr pcp usd usdGeom
        usdVol usdLux usdMedia usdShade usdRender usdHydra usdRi usdSkel usdUI
        usdUtils garch hf hio cameraUtil pxOsd glf hgi hgiGL hd hdSt hdx
        usdImaging usdImagingGL usdRiImaging usdSkelImaging usdVolImaging
        usdAppUtils usdviewq)
        add_library(${targetName} INTERFACE)
        target_link_libraries(${targetName} INTERFACE usd_ms)
    endforeach()
endif()
