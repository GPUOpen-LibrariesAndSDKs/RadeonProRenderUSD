#ifndef HDRPR_RENDER_PARAM_H
#define HDRPR_RENDER_PARAM_H

#include "renderThread.h"

#include "pxr/imaging/hd/renderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

#define HDRPR_MATERIAL_NETWORK_SELECTOR_TOKENS \
    (rpr) \
    (karma)

TF_DECLARE_PUBLIC_TOKENS(HdRprMaterialNetworkSelectorTokens, HDRPR_MATERIAL_NETWORK_SELECTOR_TOKENS);

class HdRprApi;

class HdRprRenderParam final : public HdRenderParam {
public:
    HdRprRenderParam(HdRprApi* rprApi, HdRprRenderThread* renderThread)
        : m_rprApi(rprApi)
        , m_renderThread(renderThread) {
        m_numLights.store(0);
        InitializeEnvParameters();
    }
    ~HdRprRenderParam() override = default;

    HdRprApi const* GetRprApi() const { return m_rprApi; }
    HdRprApi* AcquireRprApiForEdit() {
        m_renderThread->StopRender();
        return m_rprApi;
    }

    HdRprRenderThread* GetRenderThread() { return m_renderThread; }

    void AddLight() { ++m_numLights; }
    void RemoveLight() { --m_numLights; }
    bool HasLights() const { return m_numLights != 0; }

    TfToken const& GetMaterialNetworkSelector() const { return m_materialNetworkSelector; }

private:
    void InitializeEnvParameters();

    HdRprApi* m_rprApi;
    HdRprRenderThread* m_renderThread;

    std::atomic<uint32_t> m_numLights;

    TfToken m_materialNetworkSelector;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_PARAM_H
