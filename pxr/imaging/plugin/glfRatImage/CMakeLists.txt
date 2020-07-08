if(NOT TARGET Houdini)
    return()
endif()

set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE ratImage)

pxr_plugin(ratImage
    DISABLE_PRECOMPILED_HEADERS
    LIBRARIES
        tf
        glf
        arch
        Houdini

    CPPFILES
        ratImage.cpp

    RESOURCE_FILES
        plugInfo.json
)

GroupSources(ratImage)
