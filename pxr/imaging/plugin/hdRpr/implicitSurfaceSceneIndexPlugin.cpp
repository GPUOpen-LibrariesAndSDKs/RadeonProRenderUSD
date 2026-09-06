// SPDX-License-Identifier: Apache-2.0
#include "implicitSurfaceSceneIndexPlugin.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hdsi/implicitSurfaceSceneIndex.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType) {
    HdSceneIndexPluginRegistry::Define<HdRprImplicitSurfaceSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin) {
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "RPR", TfToken("HdRprImplicitSurfaceSceneIndexPlugin"), nullptr, 0,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdSceneIndexBaseRefPtr
HdRprImplicitSurfaceSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputScene,
    const HdContainerDataSourceHandle& inputArgs) {
    static const auto args = []() {
        // String tokens keep this compatible with SDKs predating the plane token.
        const TfToken names[] = {TfToken("sphere"), TfToken("cube"),
            TfToken("cone"), TfToken("cylinder"), TfToken("capsule"), TfToken("plane")};
        const auto toMesh = HdRetainedTypedSampledDataSource<TfToken>::New(
            HdsiImplicitSurfaceSceneIndexTokens->toMesh);
        HdDataSourceBaseHandle values[6] = {toMesh,toMesh,toMesh,toMesh,toMesh,toMesh};
        return HdRetainedContainerDataSource::New(6, names, values);
    }();
    return HdsiImplicitSurfaceSceneIndex::New(inputScene, args);
}

PXR_NAMESPACE_CLOSE_SCOPE
