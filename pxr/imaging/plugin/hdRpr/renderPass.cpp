#include "renderPass.h"
#include "renderDelegate.h"
#include "config.h"
#include "rprApi.h"
#include "renderBuffer.h"
#include "renderParam.h"

#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hd/renderIndex.h"

#include <GL/glew.h>

PXR_NAMESPACE_OPEN_SCOPE

HdRprRenderPass::HdRprRenderPass(HdRenderIndex* index,
                                 HdRprimCollection const& collection,
                                 HdRprRenderParam* renderParam)
    : HdRenderPass(index, collection)
    , m_lastSettingsVersion(0)
    , m_renderParam(renderParam) {

}

void HdRprRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState, TfTokenVector const& renderTags) {
    HdRprConfig::GetInstance().Sync(GetRenderIndex()->GetRenderDelegate());

    auto rprApi = m_renderParam->AcquireRprApiForEdit();

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

    auto& aovBindings = renderPassState->GetAovBindings();
    if (aovBindings.empty() &&
        (rprApi->GetActiveAov().IsEmpty() || !rprApi->GetAovInfo(rprApi->GetActiveAov(), nullptr, nullptr, nullptr))) {

        auto& vp = renderPassState->GetViewport();
        auto& aovToEnable = rprApi->GetActiveAov().IsEmpty() ? HdRprAovTokens->color : rprApi->GetActiveAov();
        rprApi->EnableAov(aovToEnable, vp[2], vp[3], HdFormatFloat32Vec4);
    }

    rprApi->Render();
    m_isConverged = rprApi->IsConverged();

    if (aovBindings.empty()) {
        auto& activeAov = rprApi->GetActiveAov();
        int aovWidth;
        int aovHeight;
        HdFormat aovFormat;
        if (rprApi->GetAovInfo(activeAov, &aovWidth, &aovHeight, &aovFormat)) {
            std::vector<uint8_t> buffer;
            buffer.reserve(aovWidth * aovHeight * HdDataSizeOfFormat(aovFormat));
            if (rprApi->GetAovData(activeAov, buffer.data(), buffer.capacity())) {
                float currentZoomX;
                float currentZoomY;
                glGetFloatv(GL_ZOOM_X, &currentZoomX);
                glGetFloatv(GL_ZOOM_Y, &currentZoomY);

                if (aovFormat == HdFormatInt32) {
                    aovFormat = HdFormatUNorm8Vec4;
                }
                auto componentFormat = HdGetComponentFormat(aovFormat);
                auto componentCount = HdGetComponentCount(aovFormat);

                GLenum format;
                if (componentCount == 4) {
                    format = GL_RGBA;
                } else if (componentCount == 3) {
                    format = GL_RGB;
                } else if (componentCount == 2) {
                    format = GL_RG;
                } else {
                    format = GL_R;
                }

                GLenum type;
                if (componentFormat == HdFormatUNorm8) {
                    type = GL_UNSIGNED_BYTE;
                } else if (componentFormat == HdFormatFloat16) {
                    type = GL_HALF_FLOAT;
                } else if (componentFormat == HdFormatFloat32) {
                    type = GL_FLOAT;
                }

                // Viewport size is not required to be of the same size as AOV framebuffer
                auto& vp = renderPassState->GetViewport();
                glPixelZoom(float(vp[2]) / aovWidth, float(vp[3]) / aovHeight);
                glDrawPixels(aovWidth, aovHeight, format, type, buffer.data());
                glPixelZoom(currentZoomX, currentZoomY);
            }
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
