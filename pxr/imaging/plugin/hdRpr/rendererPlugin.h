#ifndef HDRPR_RENDERER_PLUGIN_H
#define HDRPR_RENDERER_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/rendererPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

///
/// \class HdRprPlugin
///
class HdRprPlugin final : public HdRendererPlugin {
public:
    HdRprPlugin() = default;
    virtual ~HdRprPlugin() = default;

    /// Construct a new render delegate of type HdProRenderRenderDelegate.
    virtual HdRenderDelegate *CreateRenderDelegate() override;

    /// Destroy a render delegate created by this class's CreateRenderDelegate.
    ///   \param renderDelegate The render delegate to delete.
    virtual void DeleteRenderDelegate(
        HdRenderDelegate *renderDelegate) override;

	virtual bool IsSupported() const override
	{
		return true;
	}

private:
    // This class does not support copying.
    HdRprPlugin(const HdRprPlugin&) 
        = delete;
    HdRprPlugin &operator =(const HdRprPlugin&) 
        = delete;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDERER_PLUGIN_H
