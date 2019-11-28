#ifndef HDRPR_RENDER_PARAM_H
#define HDRPR_RENDER_PARAM_H

#include "renderThread.h"

#include "pxr/imaging/hd/renderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;

class HdRprRenderParam final : public HdRenderParam {
public:
    HdRprRenderParam(HdRprApi* rprApi, HdRprRenderThread* renderThread)
        : m_rprApi(rprApi)
        , m_renderThread(renderThread) {
        m_numLights.store(0);
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

private:
    HdRprApi* m_rprApi;
    HdRprRenderThread* m_renderThread;

    std::atomic<uint32_t> m_numLights;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_PARAM_H
