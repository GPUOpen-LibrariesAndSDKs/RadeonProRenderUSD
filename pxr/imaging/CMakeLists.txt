add_custom_target(shared_libs)
add_custom_target(python ALL)

add_subdirectory(rprUsd)
add_subdirectory(plugin)

pxr_setup_python()
