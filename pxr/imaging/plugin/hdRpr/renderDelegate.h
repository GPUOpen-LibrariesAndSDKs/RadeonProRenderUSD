#ifndef HDRPR_RENDER_DELEGATE_H
#define HDRPR_RENDER_DELEGATE_H

#include "api.h"
#include "renderThread.h"

#include "pxr/imaging/hd/renderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprRenderParam;
class HdRprApi;

class HdRprDelegate final : public HdRenderDelegate {
public:

    HdRprDelegate();
    ~HdRprDelegate() override;

    HdRprDelegate(const HdRprDelegate&) = delete;
    HdRprDelegate& operator =(const HdRprDelegate&) = delete;

    const TfTokenVector& GetSupportedRprimTypes() const override;
    const TfTokenVector& GetSupportedSprimTypes() const override;
    const TfTokenVector& GetSupportedBprimTypes() const override;

    HdRenderParam* GetRenderParam() const override;
    HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex* index,
                                           HdRprimCollection const& collection) override;

    HdInstancer* CreateInstancer(HdSceneDelegate* delegate,
                                 SdfPath const& id,
                                 SdfPath const& instancerId) override;
    void DestroyInstancer(HdInstancer* instancer) override;

    HdRprim* CreateRprim(TfToken const& typeId,
                         SdfPath const& rprimId,
                         SdfPath const& instancerId) override;
    void DestroyRprim(HdRprim* rPrim) override;

    HdSprim* CreateSprim(TfToken const& typeId,
                         SdfPath const& sprimId) override;
    HdSprim* CreateFallbackSprim(TfToken const& typeId) override;
    void DestroySprim(HdSprim* sprim) override;

    HdBprim* CreateBprim(TfToken const& typeId,
                         SdfPath const& bprimId) override;
    HdBprim* CreateFallbackBprim(TfToken const& typeId) override;
    void DestroyBprim(HdBprim* bprim) override;

    void CommitResources(HdChangeTracker* tracker) override;

    TfToken GetMaterialBindingPurpose() const override { return HdTokens->full; }
    TfToken GetMaterialNetworkSelector() const override;

    HdAovDescriptor GetDefaultAovDescriptor(TfToken const& name) const override;

    HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;

    VtDictionary GetRenderStats() const override;

    bool IsPauseSupported() const override;
    bool Pause() override;
    bool Resume() override;
private:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;

    std::unique_ptr<HdRprApi> m_rprApi;
    std::unique_ptr<HdRprRenderParam> m_renderParam;
    HdRenderSettingDescriptorList m_settingDescriptors;
    HdRprRenderThread m_renderThread;
};

PXR_NAMESPACE_CLOSE_SCOPE

extern "C" {

HDRPR_API void SetHdRprRenderDevice(int renderDevice);

HDRPR_API void SetHdRprRenderQuality(int quality);

HDRPR_API int GetHdRprRenderQuality();

} // extern "C"

#endif // HDRPR_RENDER_DELEGATE_H
