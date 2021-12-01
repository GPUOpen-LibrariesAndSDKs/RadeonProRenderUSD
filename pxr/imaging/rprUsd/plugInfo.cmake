set(PLUG_INFO_ROOT "..")
set(PLUG_INFO_LIBRARY_PATH "../../lib/rprUsd${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(PLUG_INFO_RESOURCE_PATH "resources")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json ${CMAKE_CURRENT_BINARY_DIR}/configured/plugInfo.json @ONLY)
