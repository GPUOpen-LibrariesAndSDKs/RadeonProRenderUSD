/************************************************************************
Copyright 2020 Advanced Micro Devices, Inc
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
************************************************************************/

#ifndef HDRPR_RENDER_DELEGATE_H
#define HDRPR_RENDER_DELEGATE_H

#include "api.h"
#include "renderThread.h"

#include "pxr/imaging/hd/renderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprDiagnosticMgrDelegate;
class HdRprRenderParam;
class HdRprApi;

class HdRprDelegate final : public HdRenderDelegate {
public:

    HdRprDelegate(HdRenderSettingsMap const& renderSettings);
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

#if PXR_VERSION >= 2005
    bool IsStopSupported() const override;
    bool Stop() override;
    bool Restart() override;
    void SetDrivers(HdDriverVector const& drivers) override;
#endif // PXR_VERSION >= 2005

    bool IsBatch() const { return m_isBatch; }
    bool IsProgressive() const { return m_isProgressive; }

private:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;

    bool m_isBatch;
    bool m_isProgressive;

    std::unique_ptr<HdRprApi> m_rprApi;
    std::unique_ptr<HdRprRenderParam> m_renderParam;
    HdRenderSettingDescriptorList m_settingDescriptors;
    HdRprRenderThread m_renderThread;

    using DiagnostMgrDelegatePtr = std::unique_ptr<HdRprDiagnosticMgrDelegate, std::function<void (HdRprDiagnosticMgrDelegate*)>>;
    DiagnostMgrDelegatePtr m_diagnosticMgrDelegate;
};


PXR_NAMESPACE_CLOSE_SCOPE

extern "C" {

HDRPR_API void HdRprSetRenderDevice(const char* renderDevice);

HDRPR_API void HdRprSetRenderQuality(const char* quality);

// Returned pointer should be released by the caller with HdRprFree
HDRPR_API char* HdRprGetRenderQuality();

HDRPR_API void HdRprFree(void* ptr);

} // extern "C"

#endif // HDRPR_RENDER_DELEGATE_H
