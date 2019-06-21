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

FIND_PATH( OPENVDB_LOCATION include/openvdb/version.h 
  "$ENV{OPENVDB_ROOT}"
  NO_DEFAULT_PATH
  NO_SYSTEM_ENVIRONMENT_PATH
  )

FIND_PACKAGE_HANDLE_STANDARD_ARGS( OpenVDB
  REQUIRED_VARS OPENVDB_LOCATION 
  )

IF( OpenVDB_FOUND )
  SET( OpenVDB_INCLUDE_DIR ${OPENVDB_LOCATION}/include
    CACHE PATH "OpenVDB include directory")

  SET( OpenVDB_LIBRARY_DIR ${OPENVDB_LOCATION}/lib
    CACHE PATH "OpenVDB library directory" )
  
  FIND_LIBRARY( OpenVDB_OPENVDB_LIBRARY openvdb
    PATHS ${OpenVDB_LIBRARY_DIR}
    NO_DEFAULT_PATH
    NO_SYSTEM_ENVIRONMENT_PATH
    )
  
  SET( OpenVDB_LIBRARIES "")
  LIST( APPEND OpenVDB_LIBRARIES ${OpenVDB_OPENVDB_LIBRARY} )
  
  SET( OPENVDB_VERSION_FILE ${OpenVDB_INCLUDE_DIR}/openvdb/version.h )

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

ENDIF( OpenVDB_FOUND )
