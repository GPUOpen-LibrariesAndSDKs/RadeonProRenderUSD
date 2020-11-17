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

#include "renderBuffer.h"
#include "renderParam.h"
#include "rprApi.h"

#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprRenderBuffer::HdRprRenderBuffer(SdfPath const& id, HdRprApi* api)
    : HdRenderBuffer(id)
    , m_numMappers(0)
    , m_isConverged(false)
    , m_rprApi(api) {

}

void HdRprRenderBuffer::Sync(HdSceneDelegate* sceneDelegate,
                             HdRenderParam* renderParam,
                             HdDirtyBits* dirtyBits) {
    if (*dirtyBits & DirtyDescription) {
        // hdRpr has the background thread write directly into render buffers,
        // so we need to stop the render thread before reallocating them.
        static_cast<HdRprRenderParam*>(renderParam)->AcquireRprApiForEdit();
    }

    HdRenderBuffer::Sync(sceneDelegate, renderParam, dirtyBits);
}

void HdRprRenderBuffer::Finalize(HdRenderParam* renderParam) {
    // hdRpr has the background thread write directly into render buffers,
    // so we need to stop the render thread before reallocating them.
    static_cast<HdRprRenderParam*>(renderParam)->AcquireRprApiForEdit();

    HdRenderBuffer::Finalize(renderParam);
}

bool HdRprRenderBuffer::Allocate(GfVec3i const& dimensions,
                                 HdFormat format,
                                 bool multiSampled) {
    if (dimensions[2] != 1) {
        TF_WARN("HdRprRenderBuffer supports 2D buffers only");
        return false;
    }

#ifdef ENABLE_MULTITHREADED_RENDER_BUFFER
    std::unique_lock<std::mutex> lock(m_mapMutex);
    m_mapConditionVar.wait(lock, [this]() { return m_numMappers == 0; });
#endif // ENABLE_MULTITHREADED_RENDER_BUFFER

    m_width = dimensions[0];
    m_height = dimensions[1];
    m_format = format;
    m_multiSampled = multiSampled;
    m_isConverged.store(false);

    size_t dataByteSize = m_width * m_height * HdDataSizeOfFormat(m_format);
    if (dataByteSize) {
        m_mappedBuffer.reserve(dataByteSize);
        std::memset(m_mappedBuffer.data(), 0, dataByteSize);
    } else {
        m_mappedBuffer = std::vector<uint8_t>();
    }

    return true;
}

void HdRprRenderBuffer::_Deallocate() {

#ifdef ENABLE_MULTITHREADED_RENDER_BUFFER
    std::unique_lock<std::mutex> lock(m_mapMutex);
    m_mapConditionVar.wait(lock, [this]() { return m_numMappers == 0; });
#endif // ENABLE_MULTITHREADED_RENDER_BUFFER

    m_width = 0u;
    m_height = 0u;
    m_format = HdFormatInvalid;
    m_isConverged.store(false);
    m_mappedBuffer = std::vector<uint8_t>();
}

void* HdRprRenderBuffer::Map() {
    m_rprApi->Resolve();

#ifdef ENABLE_MULTITHREADED_RENDER_BUFFER
    std::unique_lock<std::mutex> lock(m_mapMutex);
#endif // ENABLE_MULTITHREADED_RENDER_BUFFER

    ++m_numMappers;
    return m_mappedBuffer.data();
}

void HdRprRenderBuffer::Unmap() {

#ifdef ENABLE_MULTITHREADED_RENDER_BUFFER
    bool isLastMapper;
    {
        std::unique_lock<std::mutex> lock(m_mapMutex);
        if (!TF_VERIFY(m_numMappers)) {
            TF_CODING_ERROR("Invalid HdRenderBuffer usage detected. Over-use of Unmap.");
            return;
        }
#endif // ENABLE_MULTITHREADED_RENDER_BUFFER

        --m_numMappers;
        TF_VERIFY(m_numMappers >= 0);

#ifdef ENABLE_MULTITHREADED_RENDER_BUFFER
        isLastMapper = m_numMappers == 0;
    }

    if (isLastMapper) {
        m_mapConditionVar.notify_one();
    }
#endif // ENABLE_MULTITHREADED_RENDER_BUFFER

    // XXX We could consider clearing _mappedBuffer here to free RAM.
    //     For now we assume that Map() will be called frequently so we prefer
    //     to avoid the cost of clearing the buffer over memory savings.
    //if (m_numMappers == 0) {
    //    m_mappedBuffer = std::vector<uint8_t>();
    //}
}

bool HdRprRenderBuffer::IsMapped() const {
    // There is no point to lock this read because HdRenderBuffer user has no idea about internal synchronization.
    // Calling this function to check if they need to unmap a render buffer is just wrong and should not happen.
    return m_numMappers != 0;
}

void HdRprRenderBuffer::Resolve() {
    // no-op
}

bool HdRprRenderBuffer::IsConverged() const {
    return m_isConverged.load();
}

void HdRprRenderBuffer::SetConverged(bool converged) {
    return m_isConverged.store(converged);
}

VtValue HdRprRenderBuffer::GetResource(bool multiSampled) const {
    if ("aov_color" == GetId().GetElementString()) {
        rpr::FrameBuffer* color = m_rprApi->GetRawColorFramebuffer();
        // RPR framebuffer not created yet
        if (!color) {
            return VtValue();
        }

        VtDictionary dictionary;
        dictionary["isVulkanInteropEnabled"] = m_rprApi->IsVulkanInteropEnabled();
        dictionary["framebuffer"] = color;

        return VtValue(dictionary);
    }
    return VtValue();
}

PXR_NAMESPACE_CLOSE_SCOPE
