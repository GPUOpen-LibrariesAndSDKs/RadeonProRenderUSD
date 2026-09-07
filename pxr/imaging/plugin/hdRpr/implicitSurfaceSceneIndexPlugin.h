// SPDX-License-Identifier: Apache-2.0
#ifndef HDRPR_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H
#define HDRPR_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H

#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

// Hydra's scene-index path preserves analytic primitives. hdRpr consumes meshes.
class HdRprImplicitSurfaceSceneIndexPlugin : public HdSceneIndexPlugin {
public:
    HdRprImplicitSurfaceSceneIndexPlugin() = default;
protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr& inputScene,
        const HdContainerDataSourceHandle& inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE
#endif
