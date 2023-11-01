# This finds Pixar USD which is part of a Houdini installation.

set(HOUDINI_ROOT $ENV{HFS} CACHE PATH "Houdini installation directory")

set(HUSD_REQ_VARS "")

find_path(Houdini_USD_INCLUDE_DIR
    "pxr/pxr.h"
    PATHS ${HOUDINI_ROOT}
    PATH_SUFFIXES "toolkit/include"
    NO_DEFAULT_PATH)
list(APPEND HUSD_REQ_VARS "Houdini_USD_INCLUDE_DIR")

find_path(Houdini_USD_LIB_DIR
    "libpxr_tf${CMAKE_SHARED_LIBRARY_SUFFIX}"
    PATHS ${HOUDINI_ROOT}
    PATH_SUFFIXES
        "dsolib" # Linux and Windows
        "../Libraries" # macOS
        "bin" # Windows (dll)
    NO_DEFAULT_PATH)
list(APPEND HUSD_REQ_VARS "Houdini_USD_LIB_DIR")

if(WIN32)
    find_path(Houdini_USD_IMPLIB_DIR
        "libpxr_tf.lib"
        PATHS ${HOUDINI_ROOT}
        PATH_SUFFIXES "custom/houdini/dsolib" # Windows (import lib)
        NO_DEFAULT_PATH)
    list(APPEND HUSD_REQ_VARS "Houdini_USD_IMPLIB_DIR")
endif()

if(APPLE)
    find_library(Houdini_TBB_LIB
        NAMES tbb
        PATHS ${HOUDINI_ROOT}
        PATH_SUFFIXES "../Libraries"
        NO_DEFAULT_PATH)
endif(APPLE)

set(Houdini_Python_VARS Houdini_Python_INCLUDE_DIR Houdini_Python_LIB Houdini_Boostpython_LIB)
list(APPEND HUSD_REQ_VARS ${Houdini_Python_VARS})

foreach(python_major_minor "3;9" "3;7" "2;7")
    list(GET python_major_minor 0 py_major)
    list(GET python_major_minor 1 py_minor)

    foreach(var ${Houdini_Python_VARS})
        set(${var} ${var}-NOTFOUND)
    endforeach()

    find_path(
        Houdini_Python_INCLUDE_DIR
            "pyconfig.h"
        PATHS ${HOUDINI_ROOT}/toolkit/include
        PATH_SUFFIXES
            "python${py_major}.${py_minor}"
            "python${py_major}.${py_minor}m" # macOS Houdini 18.5
        NO_DEFAULT_PATH)

    find_file(
        Houdini_Python_LIB
        NAMES
            "libpython${py_major}.${py_minor}${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix
            "libpython${py_major}.${py_minor}m${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix Houdini 18.5
            "python${py_major}${py_minor}.lib" # Windows (import lib)
        PATHS ${HOUDINI_ROOT}
        PATH_SUFFIXES
            "python/lib" # Linux
            "../../../../Python.framework/Versions/Current/lib" # macOS
            "python${py_major}${py_minor}/libs" # Windows (import lib)
        NO_DEFAULT_PATH)

    find_file(
        Houdini_Boostpython_LIB
        "libhboost_python-mt${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix
        "libhboost_python${py_major}${py_minor}-mt-x64${CMAKE_SHARED_LIBRARY_SUFFIX}" # Unix
        "libhboost_python${py_major}${py_minor}-mt-a64${CMAKE_SHARED_LIBRARY_SUFFIX}" # MacOS ARM
        "hboost_python-mt.lib" # Windows (import lib)
        "hboost_python${py_major}${py_minor}-mt-x64.lib" # Windows Houdini 18.5
        PATHS ${HOUDINI_ROOT}
        PATH_SUFFIXES
            "dsolib" # Linux
            "../Libraries" # macOS
            "custom/houdini/dsolib" # Windows (import lib)
        NO_DEFAULT_PATH)

    set(Houdini_Python_FOUND TRUE)
    foreach(var ${Houdini_Python_VARS})
        if(NOT ${var})
            set(Houdini_Python_FOUND FALSE)
        endif()
    endforeach()

    if(Houdini_Python_FOUND)
        break()
    endif()
endforeach()

find_program(HYTHON_EXECUTABLE hython
    PATHS ${HOUDINI_ROOT}
    PATH_SUFFIXES bin)
if(NOT HYTHON_EXECUTABLE)
    message(FATAL "Could not find hython executable")
endif()
list(APPEND HUSD_REQ_VARS "HYTHON_EXECUTABLE")
set(PYTHON_EXECUTABLE ${HYTHON_EXECUTABLE})

if(WIN32)
    set(Houdini_LIB_DIR ${HOUDINI_ROOT}/custom/houdini/dsolib)
elseif(APPLE)
    set(Houdini_LIB_DIR ${HOUDINI_ROOT}/../Libraries)
else()
    set(Houdini_LIB_DIR ${HOUDINI_ROOT}/dsolib)
endif()

find_path(Houdini_MTLX_INCLUDE_DIR
    "MaterialXCore/Document.h"
    PATHS ${HOUDINI_ROOT}
    PATH_SUFFIXES "toolkit/include"
    NO_DEFAULT_PATH)
if(Houdini_MTLX_INCLUDE_DIR)
    if(WIN32)
        find_path(Houdini_MTLX_IMPLIB_DIR
            "MaterialXCore.lib"
            PATHS ${HOUDINI_ROOT}
            PATH_SUFFIXES "custom/houdini/dsolib"
            NO_DEFAULT_PATH)
        list(APPEND HUSD_REQ_VARS "Houdini_MTLX_IMPLIB_DIR")
    else()
        set(_MTLX_LIB_PREFIX "lib")
    endif()

    foreach(targetName MaterialXCore MaterialXFormat)
        add_library(${targetName} SHARED IMPORTED)
        set_target_properties(${targetName} PROPERTIES
            IMPORTED_LOCATION "${Houdini_USD_LIB_DIR}/${_MTLX_LIB_PREFIX}${targetName}${CMAKE_SHARED_LIBRARY_SUFFIX}"
            INTERFACE_INCLUDE_DIRECTORIES ${Houdini_MTLX_INCLUDE_DIR})
        target_compile_definitions(${targetName} INTERFACE -DMATERIALX_BUILD_SHARED_LIBS)
        if(WIN32)
            set_target_properties(${targetName} PROPERTIES
                IMPORTED_IMPLIB "${Houdini_MTLX_IMPLIB_DIR}/${targetName}.lib")
        endif()
    endforeach()

    set(MaterialX_FOUND 1)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
    "HoudiniUSD"
    REQUIRED_VARS ${HUSD_REQ_VARS})

if(HoudiniUSD_FOUND)
    message(STATUS "  Houdini USD includes: ${Houdini_USD_INCLUDE_DIR}")
    message(STATUS "  Houdini USD libs: ${Houdini_USD_LIB_DIR}")
    message(STATUS "  Houdini Python includes: ${Houdini_Python_INCLUDE_DIR}")
    message(STATUS "  Houdini Python lib: ${Houdini_Python_LIB}")
    message(STATUS "  Houdini Boost.Python lib: ${Houdini_Boostpython_LIB}")
    if(MaterialX_FOUND)
        message(STATUS "  Houdini MaterialX: ${Houdini_MTLX_INCLUDE_DIR}")
    endif()
endif()

if(HoudiniUSD_FOUND AND NOT TARGET hd)
    set(version_file ${Houdini_USD_INCLUDE_DIR}/pxr/pxr.h)
    file(STRINGS "${version_file}" PXR_VERSION
         REGEX "^#define[\t ]+PXR_VERSION[\t ]+.*")
    string(REGEX REPLACE "^.*PXR_VERSION[\t ]+([0-9]*).*$" "\\1"
           PXR_VERSION "${PXR_VERSION}")

    # Generic creation of the usd targets. This is not meant to be perfect. The
    # criteria is "does it work for our use". Also, these names match the ones
    # of the USD distribution so we can use either without many conditions.
    # We're missing the interlib dependencies here so the plugin has to link a
    # bit more stuff explicitly.
    foreach(targetName
        arch tf gf js trace work plug vt ar kind sdf ndr sdr pcp usd usdGeom
        usdVol usdLux usdMedia usdShade usdRender usdHydra usdRi usdSkel usdUI
        usdUtils garch hf hio cameraUtil pxOsd glf hgi hgiGL hd hdSt hdMtlx hdx
        usdImaging usdImagingGL usdRiImaging usdSkelImaging usdVolI maging
        usdAppUtils usdviewq)
        add_library(${targetName} SHARED IMPORTED)
        set_target_properties(${targetName} PROPERTIES
            IMPORTED_LOCATION "${Houdini_USD_LIB_DIR}/libpxr_${targetName}${CMAKE_SHARED_LIBRARY_SUFFIX}"
            INTERFACE_INCLUDE_DIRECTORIES ${Houdini_USD_INCLUDE_DIR})
        if(MSVC)
            set_target_properties(${targetName} PROPERTIES
                IMPORTED_IMPLIB "${Houdini_USD_IMPLIB_DIR}/libpxr_${targetName}.lib")
        endif()
        if(WIN32)
            # Shut up compiler about warnings from USD.
            target_compile_options(${targetName} INTERFACE
                "/wd4506" "/wd4244" "/wd4305" "/wd4267")
            # For automatically linked libraries (python, tbb)
            target_link_directories(${targetName}
                INTERFACE "${HOUDINI_ROOT}/custom/houdini/dsolib")
        endif()
    endforeach()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Houdini builds with the old ABI. We need to match.
        add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)
    endif()
    # Add python to tf, usdImagingGL targets.
    target_include_directories(tf INTERFACE ${Houdini_Python_INCLUDE_DIR})
    target_link_libraries(tf INTERFACE ${Houdini_Python_LIB})
    target_include_directories(usdImagingGL INTERFACE
        ${Houdini_Python_INCLUDE_DIR})
    target_link_libraries(usdImagingGL INTERFACE ${Houdini_Python_LIB})
    # Add Boost.Python to tf, vt. Should actually be in many more but that's
    # more than enough to get it linked in for us.
    target_link_libraries(tf INTERFACE ${Houdini_Boostpython_LIB})
    target_link_libraries(vt INTERFACE ${Houdini_Boostpython_LIB})
    if(APPLE)
        target_link_libraries(tf INTERFACE ${Houdini_TBB_LIB})
    endif(APPLE)
    # By default Boost links libraries implicitly for the user via pragma's, we do not want this
    target_compile_definitions(tf INTERFACE -DHBOOST_ALL_NO_LIB)

    set(HOUDINI_BIN ${HOUDINI_ROOT}/bin)
    find_program(USD_SCHEMA_GENERATOR
        NAMES
            usdGenSchema.py usdGenSchema
        PATHS
            ${HOUDINI_BIN}
        REQUIRED
        NO_DEFAULT_PATH)
    if(USD_SCHEMA_GENERATOR)
        set(USD_SCHEMA_GENERATOR ${HOUDINI_BIN}/hython ${USD_SCHEMA_GENERATOR})
    endif()
endif()
