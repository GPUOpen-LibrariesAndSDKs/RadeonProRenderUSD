if(NOT TARGET Houdini)
    return()
endif()

add_library(RPR_for_Houdini SHARED
    plugin.cpp
    VOP_RPRMaterial.h
    VOP_RPRMaterial.cpp)

target_link_libraries(RPR_for_Houdini
    rprUsd
    Houdini)

install(
    FILES
        ui/RPR.svg
    DESTINATION
        houdini/config/Icons)

GroupSources(RPR_for_Houdini)

houdini_configure_target(RPR_for_Houdini "INSTDIR" "${CMAKE_INSTALL_PREFIX}/houdini/dso")
