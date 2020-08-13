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

#ifndef HDRPR_RPR_API_FRAMEBUFFER_H
#define HDRPR_RPR_API_FRAMEBUFFER_H

#include "pxr/pxr.h"

#include <RadeonProRender.hpp>
#include <RadeonProRender_CL.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApiFramebuffer {
public:
    static constexpr uint32_t kNumChannels = 4;

    HdRprApiFramebuffer(rpr::Context* context, uint32_t width, uint32_t height);
    HdRprApiFramebuffer(HdRprApiFramebuffer&& fb) noexcept;
    ~HdRprApiFramebuffer();

    HdRprApiFramebuffer& operator=(HdRprApiFramebuffer&& fb) noexcept;

    void AttachAs(rpr::Aov aov);
    void Clear(float r, float g, float b, float a);
    void Resolve(HdRprApiFramebuffer* dstFrameBuffer);
    /// Return true if framebuffer was actually resized
    bool Resize(uint32_t width, uint32_t height);

    bool GetData(void* dstBuffer, size_t dstBufferSize);
    size_t GetSize() const;
    rpr::FramebufferDesc GetDesc() const;
    rpr::Aov GetAovId() const { return m_aov; }

    rpr_cl_mem GetCLMem();
    rpr::FrameBuffer* GetRprObject() { return m_rprFb; }

protected:
    HdRprApiFramebuffer() = default;
    void Delete();

protected:
    rpr::Context* m_context;
    rpr::FrameBuffer* m_rprFb;
    uint32_t m_width;
    uint32_t m_height;
    rpr::Aov m_aov;

private:
    void Create(uint32_t width, uint32_t height);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_FRAMEBUFFER_H
