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

set(HOUDINI_MAJOR_MINOR_VERSION "${Houdini_VERSION_MAJOR}.${Houdini_VERSION_MINOR}")
configure_file(activateHoudiniPlugin.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/activateHoudiniPlugin.cpp)
add_executable(activateHoudiniPlugin ${CMAKE_CURRENT_BINARY_DIR}/activateHoudiniPlugin.cpp)
set_property(TARGET activateHoudiniPlugin PROPERTY CXX_STANDARD 17)
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(activateHoudiniPlugin PRIVATE stdc++fs)
endif()
install(
    TARGETS activateHoudiniPlugin
    RUNTIME DESTINATION .)
