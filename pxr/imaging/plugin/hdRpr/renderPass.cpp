
#include "renderPass.h"

#include <GL/glew.h>

#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/renderPassState.h"

#include "pxr/imaging/hd/primGather.h"

#include "pxr/imaging/hdx/simpleLightTask.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprApi * rprApi = nullptr;

HdRprRenderPass::HdRprRenderPass(HdRenderIndex * index
	, HdRprimCollection const & collection
	, HdRprApiSharedPtr rprApiShader
)
	: HdRenderPass(index, collection)
{
	m_rprApiWeakPtr = rprApiShader;
}

HdRprRenderPass::~HdRprRenderPass()
{
}

void HdRprRenderPass::_Execute(HdRenderPassStateSharedPtr const & renderPassState, TfTokenVector const & renderTags)
{
	// RenderPass is caled with UsdImagingGL for renderint and intersection test.
	// Intersection test has disabled Light state in cause of this
	// renter pass should be ignorred in this case
	if (!renderPassState->GetLightingEnabled())
	{
		return;
	}

	HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
	if (!rprApi)
	{
		TF_CODING_ERROR("RprApi is expired");
		return;
	}

	// Extract viewport
	const GfVec4f & vp = renderPassState->GetViewport();
	GfVec2i fbSize(vp[2], vp[3]);

	// Change viewport if it is modified
    rprApi->ResizeAovFramebuffers(fbSize[0], fbSize[1]);

	// Change view-projection matrix if it is modified
	const GfMatrix4d & cameraViewMatrix = rprApi->GetCameraViewMatrix();
	const GfMatrix4d & wvm = renderPassState->GetWorldToViewMatrix();
	if (cameraViewMatrix != wvm)
	{
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
