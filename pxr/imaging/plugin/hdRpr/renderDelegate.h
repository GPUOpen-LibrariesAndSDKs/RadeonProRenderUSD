#ifndef HDRPR_RENDER_DELEGATE_H
#define HDRPR_RENDER_DELEGATE_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/tokens.h"

#include "renderParam.h"
#include "resourceRegistry.h"

#include "api.h"
#include "rprApi.h"

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class HdProRenderRenderParam;

///
/// \class HdRprDelegate
///
class HdRprDelegate final : public HdRenderDelegate {
public:

    HdRprDelegate();
    virtual ~HdRprDelegate();

    ///
    /// Returns a list of typeId's of all supported Rprims by this render
    /// delegate.
    ///
    virtual const TfTokenVector &GetSupportedRprimTypes() const override;

    ///
    /// Returns a list of typeId's of all supported Sprims by this render
    /// delegate.
    ///
    virtual const TfTokenVector &GetSupportedSprimTypes() const override;


    ///
    /// Returns a list of typeId's of all supported Bprims by this render
    /// delegate.
    ///
    virtual const TfTokenVector &GetSupportedBprimTypes() const override;

    ///
    /// Returns an opaque handle to a render param, that in turn is
    /// passed to each prim created by the render delegate during sync
    /// processing.  This avoids the need to store a global state pointer
    /// in each prim.
    ///
    /// The typical lifetime of the renderParam would match that of the
    /// RenderDelegate, however the minimal lifetime is that of the Sync
    /// processing.  The param maybe queried multiple times during sync.
    ///
    /// A render delegate may return null for the param.
    ///
    virtual HdRenderParam *GetRenderParam() const override;

    ///
    /// Returns a shared ptr to the resource registry of the current render
    /// delegate.
    ///
    virtual HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// Renderpass Factory
    ///
    ////////////////////////////////////////////////////////////////////////////

    ///
    /// Request to create a new renderpass.
    /// \param index the render index to bind to the new renderpass.
    /// \param collection the rprim collection to bind to the new renderpass.
    /// \return A shared pointer to the new renderpass or empty on error.
    ///
    virtual HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex *index,
                                      HdRprimCollection const& collection) override;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// Instancer Factory
    ///
    ////////////////////////////////////////////////////////////////////////////

    ///
    /// Request to create a new instancer.
    /// \param id The unique identifier of this instancer.
    /// \param instancerId The unique identifier for the parent instancer that
    ///                    uses this instancer as a prototype (may be empty).
    /// \return A pointer to the new instancer or nullptr on error.
    ///
    virtual HdInstancer *CreateInstancer(HdSceneDelegate *delegate,
                                         SdfPath const& id,
                                         SdfPath const& instancerId) ;

    virtual void DestroyInstancer(HdInstancer *instancer) override;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// Prim Factories
    ///
    ////////////////////////////////////////////////////////////////////////////


    ///
    /// Request to Allocate and Construct a new Rprim.
    /// \param typeId the type identifier of the prim to allocate
    /// \param rprimId a unique identifier for the prim
    /// \param instancerId the unique identifier for the instancer that uses
    ///                    the prim (optional: May be empty).
    /// \return A pointer to the new prim or nullptr on error.
    ///                     
    virtual HdRprim *CreateRprim(TfToken const& typeId,
                                 SdfPath const& rprimId,
                                 SdfPath const& instancerId) override;

    ///
    /// Request to Destruct and deallocate the prim.
    /// 
    virtual void DestroyRprim(HdRprim *rPrim) override;

    ///
    /// Request to Allocate and Construct a new Sprim.
    /// \param typeId the type identifier of the prim to allocate
    /// \param sprimId a unique identifier for the prim
    /// \return A pointer to the new prim or nullptr on error.
    ///
    virtual HdSprim *CreateSprim(TfToken const& typeId,
                                 SdfPath const& sprimId) override;

    ///
    /// Request to Allocate and Construct an Sprim to use as a standin, if there
    /// if an error with another another Sprim of the same type.  For example,
    /// if another prim references a non-exisiting Sprim, the fallback could
    /// be used.
    ///
    /// \param typeId the type identifier of the prim to allocate
    /// \return A pointer to the new prim or nullptr on error.
    ///
    virtual HdSprim *CreateFallbackSprim(TfToken const& typeId) override;

    ///
    /// Request to Destruct and deallocate the prim.
    ///
    virtual void DestroySprim(HdSprim *sprim) override;

    ///
    /// Request to Allocate and Construct a new Bprim.
    /// \param typeId the type identifier of the prim to allocate
    /// \param sprimId a unique identifier for the prim
    /// \return A pointer to the new prim or nullptr on error.
    ///
    virtual HdBprim *CreateBprim(TfToken const& typeId,
                                 SdfPath const& bprimId) override;


    ///
    /// Request to Allocate and Construct a Bprim to use as a standin, if there
    /// if an error with another another Bprim of the same type.  For example,
    /// if another prim references a non-exisiting Bprim, the fallback could
    /// be used.
    ///
    /// \param typeId the type identifier of the prim to allocate
    /// \return A pointer to the new prim or nullptr on error.
    ///
    virtual HdBprim *CreateFallbackBprim(TfToken const& typeId) override;

    ///
    /// Request to Destruct and deallocate the prim.
    ///
    virtual void DestroyBprim(HdBprim *bprim) override;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// Sync, Execute & Dispatch Hooks
    ///
    ////////////////////////////////////////////////////////////////////////////

    ///
    /// Notification point from the Engine to the delegate.
    /// This notification occurs after all Sync's have completed and
    /// before task execution.
    ///
    /// This notification gives the Render Delegate a chance to
    /// update and move memory that the render may need.
    ///
    /// For example, the render delegate might fill primvar buffers or texture
    /// memory.
    ///
    virtual void CommitResources(HdChangeTracker *tracker)override;

	virtual TfToken GetMaterialBindingPurpose() const override { return HdTokens->full; }
 
	///
	/// Returns a token that can be used to select among multiple
	/// material network implementations.  The default is empty.
	///
	HDRPR_API
	virtual TfToken GetMaterialNetworkSelector() const;
private:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;

   //HdRprResourceRegistrySharedPtr _resourceRegistry;
  // HdRprParamSharedPtr _renderParam;

    // This class does not support copying.
	HdRprDelegate(const HdRprDelegate &)
        = delete;
	HdRprDelegate &operator =(const HdRprDelegate &)
        = delete;

	HdRprApiSharedPtr m_rprApiSharedPtr;
};


PXR_NAMESPACE_CLOSE_SCOPE

extern "C"
{
	HDRPR_API
	void SetRprGlobalRenderMode(int renderMode);

	HDRPR_API
	void SetRprGlobalRenderDevice(int renderDevice);
}

#endif // HDRPR_RENDER_DELEGATE_H
