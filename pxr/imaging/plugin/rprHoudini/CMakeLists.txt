if(NOT TARGET Houdini)
    return()
endif()

add_library(RPR_VOPs SHARED
    plugin.cpp
    VOP_RPRMaterial.h
    VOP_RPRMaterial.cpp)

target_link_libraries(RPR_VOPs
    rprUsd
    Houdini)

install(
    FILES
        ui/RPR.svg
    DESTINATION
        houdini/config/Icons)

GroupSources(RPR_VOPs)

houdini_configure_target(RPR_VOPs "INSTDIR" "${CMAKE_INSTALL_PREFIX}/houdini/dso")
