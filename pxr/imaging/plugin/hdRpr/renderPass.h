#ifndef HDRPR_RENDER_PASS_H
#define HDRPR_RENDER_PASS_H

#include "pxr/imaging/hd/renderPass.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprRenderParam;

class HdRprRenderPass final : public HdRenderPass {
public:
    HdRprRenderPass(HdRenderIndex* index,
                    HdRprimCollection const& collection,
                    HdRprRenderParam* renderParam);

    ~HdRprRenderPass() override = default;

    bool IsConverged() const override;

    void _Execute(HdRenderPassStateSharedPtr const& renderPassState,
                  TfTokenVector const& renderTags) override;

private:
    HdRprRenderParam* m_renderParam;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_PASS_H
