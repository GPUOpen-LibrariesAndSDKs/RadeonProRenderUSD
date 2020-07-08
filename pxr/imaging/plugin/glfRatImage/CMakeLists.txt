if(NOT TARGET Houdini)
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
