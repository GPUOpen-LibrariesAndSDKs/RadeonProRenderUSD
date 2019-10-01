#ifndef HDRPR_RENDER_DELEGATE_H
#define HDRPR_RENDER_DELEGATE_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/tokens.h"

#include "api.h"
#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

#define HDRPR_RENDER_SETTINGS_TOKENS \
    (enableDenoising)                \
    (renderQuality)

TF_DECLARE_PUBLIC_TOKENS(HdRprRenderSettingsTokens, HDRPR_RENDER_SETTINGS_TOKENS);

#define HDRPR_RENDER_QUALITY_TOKENS \
    (low)                           \
    (medium)                        \
    (high)                          \
    (full)                          \

TF_DECLARE_PUBLIC_TOKENS(HdRprRenderQualityTokens, HDRPR_RENDER_QUALITY_TOKENS);

///
/// \class HdRprDelegate
///
class HdRprDelegate final : public HdRenderDelegate {
public:

    HdRprDelegate();
    ~HdRprDelegate() override;

    HdRprDelegate(const HdRprDelegate &)= delete;
    HdRprDelegate &operator =(const HdRprDelegate &)= delete;

    ///
    /// Returns a list of typeId's of all supported Rprims by this render
    /// delegate.
    ///
    const TfTokenVector &GetSupportedRprimTypes() const override;

    ///
    /// Returns a list of typeId's of all supported Sprims by this render
    /// delegate.
    ///
    const TfTokenVector &GetSupportedSprimTypes() const override;


    ///
    /// Returns a list of typeId's of all supported Bprims by this render
    /// delegate.
    ///
    const TfTokenVector &GetSupportedBprimTypes() const override;

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
    HdRenderParam *GetRenderParam() const override;

    ///
    /// Returns a shared ptr to the resource registry of the current render
    /// delegate.
    ///
    HdResourceRegistrySharedPtr GetResourceRegistry() const override;

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
    HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex *index,
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
    HdInstancer *CreateInstancer(HdSceneDelegate *delegate,
                                         SdfPath const& id,
                                         SdfPath const& instancerId) override;

    void DestroyInstancer(HdInstancer *instancer) override;

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
    HdRprim *CreateRprim(TfToken const& typeId,
                                 SdfPath const& rprimId,
                                 SdfPath const& instancerId) override;

    ///
    /// Request to Destruct and deallocate the prim.
    /// 
    void DestroyRprim(HdRprim *rPrim) override;

    ///
    /// Request to Allocate and Construct a new Sprim.
    /// \param typeId the type identifier of the prim to allocate
    /// \param sprimId a unique identifier for the prim
    /// \return A pointer to the new prim or nullptr on error.
    ///
    HdSprim *CreateSprim(TfToken const& typeId,
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
    HdSprim *CreateFallbackSprim(TfToken const& typeId) override;

    ///
    /// Request to Destruct and deallocate the prim.
    ///
    void DestroySprim(HdSprim *sprim) override;

    ///
    /// Request to Allocate and Construct a new Bprim.
    /// \param typeId the type identifier of the prim to allocate
    /// \param sprimId a unique identifier for the prim
    /// \return A pointer to the new prim or nullptr on error.
    ///
    HdBprim *CreateBprim(TfToken const& typeId,
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
    HdBprim *CreateFallbackBprim(TfToken const& typeId) override;

    ///
    /// Request to Destruct and deallocate the prim.
    ///
    void DestroyBprim(HdBprim *bprim) override;

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
    void CommitResources(HdChangeTracker *tracker) override;

	TfToken GetMaterialBindingPurpose() const override { return HdTokens->full; }
 
	///
	/// Returns a token that can be used to select among multiple
	/// material network implementations.  The default is empty.
	///
	TfToken GetMaterialNetworkSelector() const override;

    ///
    /// Returns a default AOV descriptor for the given named AOV, specifying
    /// things like preferred format.
    ///
    HdAovDescriptor GetDefaultAovDescriptor(TfToken const& name) const override;

    /// Returns a list of user-configurable render settings.
    /// This is a reflection API for the render settings dictionary; it need
    /// not be exhaustive, but can be used for populating application settings
    /// UI.
    HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;
private:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;

    HdRprApiSharedPtr m_rprApiSharedPtr;
    HdRenderSettingDescriptorList m_settingDescriptors;
};

PXR_NAMESPACE_CLOSE_SCOPE

extern "C"
{
    HDRPR_API
    void SetHdRprRenderDevice(int renderDevice);

    HDRPR_API
    void SetHdRprRenderQuality(int quality);

    HDRPR_API
    const char* GetHdRprTmpDir();
}

#endif // HDRPR_RENDER_DELEGATE_H
