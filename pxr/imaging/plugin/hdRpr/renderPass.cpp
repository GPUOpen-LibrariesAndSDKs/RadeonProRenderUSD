
#include "renderPass.h"

#include <GL/glew.h>

#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/renderPassState.h"

#include "pxr/imaging/hd/primGather.h"

#include "pxr/imaging/hdx/simpleLightTask.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprRenderPass::HdRprRenderPass(HdRenderIndex* index
    , HdRprimCollection const& collection
    , HdRprApiSharedPtr rprApi)
    : HdRenderPass(index, collection) {
    m_rprApiWeakPtr = rprApi;
}

void HdRprRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState, TfTokenVector const& renderTags) {
    HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
    if (!rprApi) {
        TF_CODING_ERROR("RprApi is expired");
        return;
    }

    auto& vp = renderPassState->GetViewport();
    GfVec2i fbSize(vp[2], vp[3]);
    rprApi->ResizeAovFramebuffers(fbSize[0], fbSize[1]);

    auto& cameraViewMatrix = rprApi->GetCameraViewMatrix();
    auto& wvm = renderPassState->GetWorldToViewMatrix();
    if (cameraViewMatrix != wvm) {
        rprApi->SetCameraViewMatrix(wvm);
    }

    auto& cameraProjMatrix = rprApi->GetCameraProjectionMatrix();
    auto& proj = renderPassState->GetProjectionMatrix();
    if (cameraProjMatrix != proj) {
        rprApi->SetCameraProjectionMatrix(proj);
    }

    rprApi->Render();

    auto& aovBindings = renderPassState->GetAovBindings();
    if (aovBindings.empty()) {
        if (auto colorBuffer = rprApi->GetFramebufferData(rprApi->GetActiveAov())) {
            glDrawPixels(fbSize[0], fbSize[1], GL_RGBA, GL_FLOAT, colorBuffer.get());
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
