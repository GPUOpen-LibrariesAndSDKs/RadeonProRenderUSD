#ifndef HDRPR_RENDER_PARAM_H
#define HDRPR_RENDER_PARAM_H

#include "pxr/imaging/hd/renderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;

class HdRprRenderParam final : public HdRenderParam {
public:
    HdRprRenderParam(HdRprApi* rprApi)
        : m_rprApi(rprApi) {

    }
    ~HdRprRenderParam() override = default;

    HdRprApi const* GetRprApi() const { return m_rprApi; }
    HdRprApi* AcquireRprApiForEdit() {
        m_sceneEdited.store(true);
        return m_rprApi;
    }

    bool IsSceneEdited() const { return m_sceneEdited.load(); }
    void ResetSceneEdited() { m_sceneEdited.store(false); }

private:
    HdRprApi* m_rprApi;
    std::atomic<bool> m_sceneEdited;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_PARAM_H
