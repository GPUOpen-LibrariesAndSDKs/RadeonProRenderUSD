include(ExternalProject)

# Configure and install boost 1.76.0
set(BOOST_CONFIG "using python : ${Houdini_Python_VERSION}
     : \"${HYTHON_EXECUTABLE}\"
     : \"${Houdini_Python_INCLUDE_DIR}\"
     : \"${Houdini_Python_LIB}\"
     \; ")
file(WRITE ${CMAKE_BINARY_DIR}/boost-python-config.jam ${BOOST_CONFIG})

if(RESOLVER_BOOST_TOOLSET_OVERRIDE)
    # Boost 1.76.0 always use default toolset. We should modify bootstrap.bat file to override toolset
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/modified_boost_bootstrap.py ${CMAKE_CURRENT_SOURCE_DIR}/build_boost.bat DESTINATION ${CMAKE_BINARY_DIR})
    set(BOOST_CONFIGURE_COMMAND ../build_boost.bat ${RESOLVER_BOOST_TOOLSET_OVERRIDE})
else()
    set(BOOST_CONFIGURE_COMMAND ./bootstrap.bat)
endif()

ExternalProject_Add(boost_1_76_0
        URL https://boostorg.jfrog.io/artifactory/main/release/1.76.0/source/boost_1_76_0.zip
        URL_HASH SHA256=0fd43bb53580ca54afc7221683dfe8c6e3855b351cd6dce53b1a24a7d7fbeedd
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        CONFIGURE_COMMAND ${BOOST_CONFIGURE_COMMAND}
        BUILD_COMMAND ./b2.exe --with-log --with-python --user-config=${CMAKE_BINARY_DIR}/boost-python-config.jam
        BUILD_IN_SOURCE ON
        SOURCE_DIR ${CMAKE_BINARY_DIR}/boost_1_76_0
        INSTALL_DIR ${CMAKE_BINARY_DIR}/boost_1_76_0
        INSTALL_COMMAND ""
)

ExternalProject_Add(RenderStudioKit
        GIT_REPOSITORY https://github.com/Radeon-Pro/RenderStudioKit.git
        GIT_TAG 45a63efddfb709337ba44057c2eed7ac89d988d3
        CMAKE_CACHE_ARGS -DHOUDINI_SUPPORT:STRING=ON
        CMAKE_CACHE_ARGS -DBoost_ROOT:STRING=${CMAKE_BINARY_DIR}/boost_1_76_0/stage/lib/cmake
        CMAKE_CACHE_ARGS -DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}
        CMAKE_CACHE_ARGS -DPXR_ENABLE_PYTHON_SUPPORT:STRING=ON
        DEPENDS boost_1_76_0
)