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

#ifndef HDRPR_RENDER_BUFFER_H
#define HDRPR_RENDER_BUFFER_H

#include "pxr/imaging/hd/renderBuffer.h"

#include <condition_variable>

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;

class HdRprRenderBuffer final : public HdRenderBuffer {
public:
    HdRprRenderBuffer(SdfPath const& id, HdRprApi* api = nullptr);
    ~HdRprRenderBuffer() override = default;

    void Sync(HdSceneDelegate* sceneDelegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

    void Finalize(HdRenderParam* renderParam) override;

    bool Allocate(GfVec3i const& dimensions,
                  HdFormat format,
                  bool multiSampled) override;

    unsigned int GetWidth() const override { return m_width; }

    unsigned int GetHeight() const override { return m_height; }

    unsigned int GetDepth() const override { return 1u; }

    HdFormat GetFormat() const override { return m_format; }

    bool IsMultiSampled() const override { return m_multiSampled; }

    void* Map() override;

    void Unmap() override;

    bool IsMapped() const override;

    void Resolve() override;

    bool IsConverged() const override;

    void SetConverged(bool converged);

    void* GetPointerForWriting() { return m_mappedBuffer.data(); }

    // HdRprRenderBuffer should hold actual framebuffer
    // But for now just take it from HdRprApi in order to provide valid API
    VtValue GetResource(bool multiSampled) const;

protected:
    void _Deallocate() override;

private:
    std::vector<uint8_t> m_mappedBuffer;
    uint32_t m_width = 0u;
    uint32_t m_height = 0u;
    HdFormat m_format = HdFormat::HdFormatInvalid;
    bool m_multiSampled = false;

    std::atomic<bool> m_isConverged;

    HdRprApi* m_rprApi = nullptr;
    
#ifdef ENABLE_MULTITHREADED_RENDER_BUFFER
    std::mutex m_mapMutex;
    std::condition_variable m_mapConditionVar;
#endif // ENABLE_MULTITHREADED_RENDER_BUFFER

    int m_numMappers;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_BUFFER_H
