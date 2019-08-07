
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
	GfVec2i currentFbSize;
	rprApi->GetFramebufferSize(currentFbSize);
	if (fbSize != currentFbSize)
	{
		rprApi->Resize(fbSize);
	}


	// Change view-projection matrix if it is modified
	const GfMatrix4d & cameraViewMatrix = rprApi->GetCameraViewMatrix();
	const GfMatrix4d & wvm = renderPassState->GetWorldToViewMatrix();
	if (cameraViewMatrix != wvm)
	{
		rprApi->SetCameraViewMatrix(wvm);
	}

	const GfMatrix4d & cameraProjMatrix = rprApi->GetCameraProjectionMatrix();
	const GfMatrix4d & proj = renderPassState->GetProjectionMatrix();
	if (cameraProjMatrix != proj) {
		rprApi->SetCameraProjectionMatrix(proj);
	}

	if (rprApi->IsGlInteropUsed()) {
		GLuint rprFb = rprApi->GetFramebufferGL();
		GLint usdReadFB, usdWriteFB;
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &usdReadFB);
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &usdWriteFB);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, rprFb);
		rprApi->Render();

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, usdWriteFB);
		glBlitFramebuffer(0, 0, fbSize[0], fbSize[1],
						  0, 0, fbSize[0], fbSize[1],
						  GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, usdReadFB);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, usdWriteFB);
	} else {
		rprApi->Render();
		glDrawPixels(fbSize[0], fbSize[1], GL_RGBA, GL_FLOAT, rprApi->GetFramebufferData());
	}

}

PXR_NAMESPACE_CLOSE_SCOPE
