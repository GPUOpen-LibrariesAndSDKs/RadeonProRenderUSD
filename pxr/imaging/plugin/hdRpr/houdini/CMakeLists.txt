if(OpenVDB_FOUND)
    set(HOUDINI_OPENVDB_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/openvdb.h
        ${CMAKE_CURRENT_SOURCE_DIR}/openvdb.cpp)
endif()

target_sources(hdRpr PRIVATE
    ${HOUDINI_OPENVDB_SOURCES})
