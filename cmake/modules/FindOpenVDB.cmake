# OpenVDB_FOUND                 Set if OpenVDB is found.
# OpenVDB_INCLUDE_DIR           OpenVDB's include directory
# OpenVDB_LIBRARY_DIR           OpenVDB's library directory
# OpenVDB_<C>_LIBRARY           Specific openvdb library (<C> is upper-case)
# OpenVDB_LIBRARIES             All openvdb libraries
# OpenVDB_MAJOR_VERSION         Major version number
# OpenVDB_MINOR_VERSION         Minor version number
# OpenVDB_PATCH_VERSION         Patch version number
#
# This module read hints about search locations from variables::
#
# OPENVDB_ROOT                  Preferred installtion prefix

FIND_PACKAGE( PackageHandleStandardArgs )

set(OPENVDB_VERSION_FILE_RELPATH include/openvdb/version.h)
set(OPENVDB_FIND_PATH "${OPENVDB_ROOT}" "$ENV{OPENVDB_ROOT}")
if(HoudiniUSD_FOUND)
  set(OPENVDB_VERSION_FILE_RELPATH openvdb/version.h)
  set(OPENVDB_FIND_PATH ${Houdini_USD_INCLUDE_DIR})
endif(HoudiniUSD_FOUND)

FIND_PATH( OPENVDB_LOCATION ${OPENVDB_VERSION_FILE_RELPATH}
  ${OPENVDB_FIND_PATH}
  NO_DEFAULT_PATH
  NO_SYSTEM_ENVIRONMENT_PATH
  )

if(HoudiniUSD_FOUND)
  set(OpenVDB_INCLUDE_DIR ${Houdini_USD_INCLUDE_DIR})
  set(OpenVDB_LIBRARY_DIR ${Houdini_LIB_DIR})
  set(OPENVDB_LIBRARY_NAME openvdb_sesi)
else(HoudiniUSD_FOUND)
  set(OpenVDB_INCLUDE_DIR ${OPENVDB_LOCATION}/include)
  set(OpenVDB_LIBRARY_DIR ${OPENVDB_LOCATION}/lib)
  set(OPENVDB_LIBRARY_NAME openvdb)
endif(HoudiniUSD_FOUND)

FIND_LIBRARY( OpenVDB_OPENVDB_LIBRARY ${OPENVDB_LIBRARY_NAME}
  PATHS "${OpenVDB_LIBRARY_DIR}"
  NO_DEFAULT_PATH
  NO_SYSTEM_ENVIRONMENT_PATH
  )

FIND_PACKAGE_HANDLE_STANDARD_ARGS( OpenVDB
  REQUIRED_VARS OPENVDB_LOCATION OpenVDB_OPENVDB_LIBRARY
  )

SET( OpenVDB_LIBRARIES "")
LIST( APPEND OpenVDB_LIBRARIES "${OpenVDB_OPENVDB_LIBRARY}" )

if(OpenVDB_FOUND)
    SET( OPENVDB_VERSION_FILE "${OpenVDB_INCLUDE_DIR}/openvdb/version.h" )

    FILE( STRINGS "${OPENVDB_VERSION_FILE}" openvdb_major_version_str
      REGEX "^#define[\t ]+OPENVDB_LIBRARY_MAJOR_VERSION_NUMBER[\t ]+.*")
    FILE( STRINGS "${OPENVDB_VERSION_FILE}" openvdb_minor_version_str
      REGEX "^#define[\t ]+OPENVDB_LIBRARY_MINOR_VERSION_NUMBER[\t ]+.*")
    FILE( STRINGS "${OPENVDB_VERSION_FILE}" openvdb_patch_version_str
      REGEX "^#define[\t ]+OPENVDB_LIBRARY_PATCH_VERSION_NUMBER[\t ]+.*")

    STRING( REGEX REPLACE "^.*OPENVDB_LIBRARY_MAJOR_VERSION_NUMBER[\t ]+([0-9]*).*$" "\\1"
      _openvdb_major_version_number "${openvdb_major_version_str}")
    STRING( REGEX REPLACE "^.*OPENVDB_LIBRARY_MINOR_VERSION_NUMBER[\t ]+([0-9]*).*$" "\\1"
      _openvdb_minor_version_number "${openvdb_minor_version_str}")
    STRING( REGEX REPLACE "^.*OPENVDB_LIBRARY_PATCH_VERSION_NUMBER[\t ]+([0-9]*).*$" "\\1"
      _openvdb_patch_version_number "${openvdb_patch_version_str}")

    SET( OpenVDB_MAJOR_VERSION ${_openvdb_major_version_number}
      CACHE STRING "OpenVDB major version number" )
    SET( OpenVDB_MINOR_VERSION ${_openvdb_minor_version_number}
      CACHE STRING "OpenVDB minor version number" )
    SET( OpenVDB_PATCH_VERSION ${_openvdb_patch_version_number}
      CACHE STRING "OpenVDB patch version number" )
endif()
