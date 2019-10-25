#include "renderPass.h"
#include "renderDelegate.h"
#include "config.h"
#include "rprApi.h"
#include "renderBuffer.h"
#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hd/renderIndex.h"

#include <GL/glew.h>

PXR_NAMESPACE_OPEN_SCOPE

HdRprRenderPass::HdRprRenderPass(HdRenderIndex* index
    , HdRprimCollection const& collection
    , HdRprApiSharedPtr rprApi)
    : HdRenderPass(index, collection)
    , m_lastSettingsVersion(0) {
    m_rprApiWeakPtr = rprApi;
}

void HdRprRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState, TfTokenVector const& renderTags) {
    HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
    if (!rprApi) {
        TF_CODING_ERROR("RprApi is expired");
        return;
    }

    HdRprConfig::GetInstance().Sync(GetRenderIndex()->GetRenderDelegate());

    auto& cameraViewMatrix = rprApi->GetCameraViewMatrix();
    auto& wvm = renderPassState->GetWorldToViewMatrix();
    if (cameraViewMatrix != wvm) {
        rprApi->SetCameraViewMatrix(wvm);
    }

    auto& cameraProjMatrix = rprApi->GetCameraProjectionMatrix();
    auto proj = renderPassState->GetProjectionMatrix();
    if (cameraProjMatrix != proj) {
        rprApi->SetCameraProjectionMatrix(proj);
    }

    rprApi->Render();
    m_isConverged = rprApi->IsConverged();

    auto& aovBindings = renderPassState->GetAovBindings();
    if (aovBindings.empty()) {
        auto& activeAov = rprApi->GetActiveAov();
        if (auto colorBuffer = rprApi->GetAovData(activeAov)) {
            auto aovSize = rprApi->GetAovSize(activeAov);
            auto& vp = renderPassState->GetViewport();

            float currentZoomX;
            float currentZoomY;
            glGetFloatv(GL_ZOOM_X, &currentZoomX);
            glGetFloatv(GL_ZOOM_Y, &currentZoomY);

            // Viewport size is not required to be of the same size as AOV framebuffer
            glPixelZoom(float(vp[2]) / aovSize[0], float(vp[3]) / aovSize[1]);
            glDrawPixels(aovSize[0], aovSize[1], GL_RGBA, GL_FLOAT, colorBuffer.get());
            glPixelZoom(currentZoomX, currentZoomY);
        }
    } else {
        for (auto& aovBinding : aovBindings) {
            auto buff = static_cast<HdRprRenderBuffer*>(aovBinding.renderBuffer);
            if (!buff) {
                buff = static_cast<HdRprRenderBuffer*>(GetRenderIndex()->GetBprim(HdPrimTypeTokens->renderBuffer, aovBinding.renderBufferId));
            }
            if (buff) {
                buff->SetConverged(m_isConverged);
            }
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
