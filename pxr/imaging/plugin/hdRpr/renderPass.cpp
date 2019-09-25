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

    auto& vp = renderPassState->GetViewport();
    GfVec2i fbSize(vp[2], vp[3]);
    rprApi->ResizeAovFramebuffers(fbSize[0], fbSize[1]);

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
        if (auto colorBuffer = rprApi->GetFramebufferData(rprApi->GetActiveAov())) {
            glDrawPixels(fbSize[0], fbSize[1], GL_RGBA, GL_FLOAT, colorBuffer.get());
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
