#ifndef HDRPR_RENDER_PASS_H
#define HDRPR_RENDER_PASS_H

#include "pxr/imaging/hd/renderPass.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprRenderPass final : public HdRenderPass {
public:
    HdRprRenderPass(HdRenderIndex* index,
                    HdRprimCollection const& collection,
                    HdRprApiSharedPtr rprApiShader);

    ~HdRprRenderPass() override = default;

    bool IsConverged() const override { return m_isConverged; }

    void _Execute(HdRenderPassStateSharedPtr const& renderPassState,
                  TfTokenVector const& renderTags) override;

private:
    HdRprApiWeakPtr m_rprApiWeakPtr;

    int m_lastSettingsVersion;
    bool m_isConverged = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_PASS_H
