if(NOT TARGET Houdini)
    return()
endif()

add_library(RPR_for_Houdini SHARED
    plugin.cpp
    VOP_RPRMaterial.h
    VOP_RPRMaterial.cpp
    LOP_RPRMaterialProperties.h
    LOP_RPRMaterialProperties.cpp
    LOP_RPRExportHelper.h
    LOP_RPRExportHelper.cpp)

target_link_libraries(RPR_for_Houdini
    arch
    usd
    usdGeom
    usdShade
    usdRender
    rprUsd
    Houdini)

GroupSources(RPR_for_Houdini)

houdini_configure_target(RPR_for_Houdini "INSTDIR" "${CMAKE_INSTALL_PREFIX}/houdini/dso")

set(HOUDINI_MAJOR_MINOR_VERSION "${Houdini_VERSION_MAJOR}.${Houdini_VERSION_MINOR}")
configure_file(activateHoudiniPlugin.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/activateHoudiniPlugin.cpp)
add_executable(activateHoudiniPlugin ${CMAKE_CURRENT_BINARY_DIR}/activateHoudiniPlugin.cpp)
set_property(TARGET activateHoudiniPlugin PROPERTY CXX_STANDARD 11)
target_link_libraries(activateHoudiniPlugin PRIVATE ghc_filesystem)
target_compile_definitions(activateHoudiniPlugin PRIVATE GHC_WIN_WSTRING_STRING_TYPE)
install(
    TARGETS activateHoudiniPlugin
    RUNTIME DESTINATION .)
install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/hda/rpr_exportRpr1.hda
        ${CMAKE_CURRENT_SOURCE_DIR}/hda/rpr_standard_rendervars.hda
    DESTINATION "houdini/otls")
install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/python/houRpr/__init__.py
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/python/houRpr/cache.py
    DESTINATION
        houdini/scripts/python/houRpr)
install(
    FILES
        ui/RPR.svg
    DESTINATION
        houdini/config/Icons)
install(
    FILES
        ui/MainMenuCommon.xml
        UsdRenderers.json
    DESTINATION
        houdini)
