# Detect APIs from the selected SDK, including vendor backports. No Houdini
# or renderer process is launched by these compile-only checks.
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(_rpr_saved_try_compile_target_type ${CMAKE_TRY_COMPILE_TARGET_TYPE})
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
if(USDMonolithic_FOUND)
    set(CMAKE_REQUIRED_LIBRARIES usd_monolithic)
else()
    set(CMAKE_REQUIRED_LIBRARIES tf hd)
endif()
check_cxx_source_compiles("
    #include <pxr/base/arch/attributes.h>
    ARCH_CONSTRUCTOR(rpr_probe, 255) {}
" RPR_USD_ARCH_CONSTRUCTOR_TWO_ARGS)
check_cxx_source_compiles("
    #include <pxr/imaging/hd/rendererPlugin.h>
    PXR_NAMESPACE_USING_DIRECTIVE
    bool probe(const HdRendererPlugin& p, const HdRendererCreateArgs& args) {
        return p.IsSupported(args, nullptr);
    }
" RPR_USD_RENDERER_CREATE_ARGS)
check_cxx_source_compiles("
    #include <pxr/usd/sdr/discoveryPlugin.h>
" RPR_USD_SDR_DISCOVERY)
if(TARGET hdsi OR USDMonolithic_FOUND)
    check_cxx_source_compiles("
        #include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
        #include <pxr/imaging/hd/sceneIndexPlugin.h>
        #include <pxr/imaging/hdsi/implicitSurfaceSceneIndex.h>
        PXR_NAMESPACE_USING_DIRECTIVE
        class Probe : public HdSceneIndexPlugin {
            HdSceneIndexBaseRefPtr _AppendSceneIndex(
                const HdSceneIndexBaseRefPtr& scene,
                const HdContainerDataSourceHandle& args) override {
                return HdsiImplicitSurfaceSceneIndex::New(scene, args);
            }
        };
    " RPR_USD_IMPLICIT_SURFACE_SCENE_INDEX)
endif()
if(TARGET Houdini)
    # Houdini is a non-imported interface target and try_compile cannot copy it.
    get_target_property(CMAKE_REQUIRED_INCLUDES Houdini INTERFACE_INCLUDE_DIRECTORIES)
    set(CMAKE_REQUIRED_LIBRARIES tf)
    get_target_property(_rpr_hdk_defs Houdini INTERFACE_COMPILE_DEFINITIONS)
    foreach(_def IN LISTS _rpr_hdk_defs)
        if(NOT _def MATCHES "[$]<")
            list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D${_def}")
        endif()
    endforeach()
    check_cxx_source_compiles("
        #include <OP/OP_Node.h>
        auto input = static_cast<const char* (OP_Node::*)(OP_InputIdx) const>(&OP_Node::inputLabel);
        auto output = static_cast<const char* (OP_Node::*)(OP_OutputIdx) const>(&OP_Node::outputLabel);
    " RPR_HOUDINI_TYPED_PORT_INDICES)
endif()
set(CMAKE_TRY_COMPILE_TARGET_TYPE ${_rpr_saved_try_compile_target_type})
cmake_pop_check_state()
configure_file(${PROJECT_SOURCE_DIR}/cmake/defaults/apiCapabilities.h.in
    ${PROJECT_BINARY_DIR}/apiCapabilities.h)
