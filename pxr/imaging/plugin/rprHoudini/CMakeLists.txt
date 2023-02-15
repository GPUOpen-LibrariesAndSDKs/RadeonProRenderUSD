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
    LOP_RPRExportHelper.cpp
    LOP_RPRRendererSettings.h
    LOP_RPRRendererSettings.cpp)

target_link_libraries(RPR_for_Houdini
    arch
    usd
    usdGeom
    usdShade
    usdRender
    rprUsd
    Houdini)

_pxr_init_rpath(rpath "houdini/dso")
_pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/lib")
_pxr_install_rpath(rpath RPR_for_Houdini)

GroupSources(RPR_for_Houdini)

set_target_properties(RPR_for_Houdini PROPERTIES PREFIX "")
install(TARGETS RPR_for_Houdini
        RUNTIME DESTINATION "houdini/dso"
        LIBRARY DESTINATION "houdini/dso")

set(HOUDINI_MAJOR_MINOR_VERSION "${Houdini_VERSION_MAJOR}.${Houdini_VERSION_MINOR}")
configure_file(houdiniPluginActivator.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/houdiniPluginActivator.cpp)

function(add_plugin_activator name)
    add_executable(${name} ${name}.cpp)
    set_property(TARGET ${name} PROPERTY CXX_STANDARD 11)
    target_link_libraries(${name} PRIVATE ghc_filesystem)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(${name} PRIVATE GHC_WIN_WSTRING_STRING_TYPE)
endfunction()

add_plugin_activator(activateHoudiniPlugin)
install(TARGETS activateHoudiniPlugin RUNTIME DESTINATION .)

if(RPR_INSTALL_MATLIB_PACKAGE)
    add_plugin_activator(activateMatlibPlugin)
    install(TARGETS activateMatlibPlugin RUNTIME DESTINATION MaterialLibrary)

    install(
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/MatLib_INSTALL.md
        DESTINATION MaterialLibrary
        RENAME INSTALL.md)
endif()

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/hda/rpr_exportRpr1.hda
        ${CMAKE_CURRENT_SOURCE_DIR}/hda/rpr_standard_rendervars.hda
    DESTINATION "houdini/otls")
install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/python/houRpr/__init__.py
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/python/houRpr/cache.py
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/python/houRpr/devices.py
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/python/houRpr/materialLibrary.py
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/python/houRpr/materialLibraryClient.py
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/python/houRpr/mtlxInclude/standard_surface.mtlx
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/python/houRpr/materialLibrary.ui
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/python/houRpr/na.png
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
