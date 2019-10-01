#ifndef HDRPR_RENDER_PASS_H
#define HDRPR_RENDER_PASS_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderPass.h"

#include "renderParam.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

//class HdProRenderScene;

/// \class HdProRenderRenderPass
///
/// HdRenderPass represents a single render iteration, rendering a view of the
/// scene (the HdRprimCollection) for a specific viewer (the camera/viewport
/// parameters in HdRenderPassState) to the current draw target.
///
class HdRprRenderPass final : public HdRenderPass {
public:
	/// Renderpass constructor.
	///   \param index The render index containing scene data to render.
	///   \param collection The initial rprim collection for this renderpass.
	///   \param scene The Baikal scene to raycast into.
	HdRprRenderPass(HdRenderIndex *index
		, HdRprimCollection const &collection
		, HdRprApiSharedPtr rprApiShader
		);

	/// Renderpass destructor.
	~HdRprRenderPass() override = default;

	/// Return false if scene is require to be rendered or true otherwise
	/// In order to specific RPR api scene always should be rendered
	virtual bool IsConverged() const override { return false; }

	// -----------------------------------------------------------------------
	// HdRenderPass API

	/// Draw the scene with the bound renderpass state.
	///   \param renderPassState Input parameters (including viewer parameters)
	///                          for this renderpass.
	///   \param renderTags Which rendertags should be drawn this pass.
	virtual void _Execute(HdRenderPassStateSharedPtr const& renderPassState,
		TfTokenVector const &renderTags) override;

private:

	// -----------------------------------------------------------------------
	// Internal API

	HdRprApiWeakPtr m_rprApiWeakPtr;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_PASS_H
