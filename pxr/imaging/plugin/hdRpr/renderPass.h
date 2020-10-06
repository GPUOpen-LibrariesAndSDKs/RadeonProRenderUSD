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

    ~HdRprRenderPass() override;

    bool IsConverged() const override;

    void _Execute(HdRenderPassStateSharedPtr const& renderPassState,
                  TfTokenVector const& renderTags) override;

private:
    HdRprRenderParam* m_renderParam;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_PASS_H
