if(NOT TARGET Houdini)
    return()
endif()

if(PXR_VERSION GREATER_EQUAL 2102)
    message(STATUS "Skipping glfRatImage plugin. Hio implementation is required.")
    return()
endif()

set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE glfRatImage)

pxr_plugin(glfRatImage
    DISABLE_PRECOMPILED_HEADERS
    LIBRARIES
        tf
        glf
        arch
        Houdini

    CPPFILES
        glfRatImage.cpp

    RESOURCE_FILES
        plugInfo.json
)

GroupSources(glfRatImage)
