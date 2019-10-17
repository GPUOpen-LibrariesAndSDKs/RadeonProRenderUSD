#include "renderPass.h"
#include "renderDelegate.h"
#include "config.h"
#include "rprApi.h"
#include "pxr/imaging/hd/renderPassState.h"

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

    // Synchronize HdRprConfig with Hydra render settings
    auto renderDelegate = GetRenderIndex()->GetRenderDelegate();
    int currentSettingsVersion = renderDelegate->GetRenderSettingsVersion();
    if (m_lastSettingsVersion != currentSettingsVersion) {
        m_lastSettingsVersion = currentSettingsVersion;

        auto enableDenoisingValue = renderDelegate->GetRenderSetting(HdRprRenderSettingsTokens->enableDenoising);
        if (enableDenoisingValue.IsHolding<int64_t>()) {
            HdRprConfig::GetInstance().SetDenoising(enableDenoisingValue.UncheckedGet<int64_t>());
        } else if (enableDenoisingValue.IsHolding<bool>()) {
            HdRprConfig::GetInstance().SetDenoising(enableDenoisingValue.UncheckedGet<bool>());
        }
    }

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
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
