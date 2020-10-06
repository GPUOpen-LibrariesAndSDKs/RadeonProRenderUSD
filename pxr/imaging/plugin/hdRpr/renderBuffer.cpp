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

HdRprRenderBuffer::HdRprRenderBuffer(SdfPath const& id)
    : HdRenderBuffer(id)
    , m_numMappers(0)
    , m_isConverged(false) {

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

    // The following _Deallocate should have been called here but it might lead to a crash in Houdini.
    // _Deallocate();
    //
    // Houdini may reallocate (resync) HdRenderBuffer while it's mapped in another
    // thread (most likely for blitting to the viewport on the main thread).
    // When HdRenderBuffer is reallocated, we resize the underlying std::vector
    // and returned previously pointer from HdRenderBuffer::Map is invalidated.
    //
    // github.com/sideeffects/HoudiniUsdBridge tells us that Karma's HdRenderBuffer
    // returns a pointer to the memory that is reallocated only on the next
    // HdRenderPass::_Execute in validateAOVs function.
    // So when Houdini makes Hydra reallocate all HdRenderBuffers it has no
    // actual effect. These changes in fact postponed until HdRenderPass::_Execute.
    //
    // So we do the same - render buffer is reallocated in HdRprRenderPass::_Execute
    // via HdRprRenderBuffer::Commit.

    m_commitWidth = dimensions[0];
    m_commitHeight = dimensions[1];
    m_commitFormat = format;
    m_multiSampled = multiSampled;

    return true;
}

unsigned int HdRprRenderBuffer::GetWidth() const {
    if (!IsMappable()) { return 0u; }

    return m_width;
}

void HdRprRenderBuffer::_Deallocate() {
    m_commitWidth = 0u;
    m_commitHeight = 0u;
    m_commitFormat = HdFormatInvalid;
    m_width = 0u;
    m_height = 0u;
    m_format = HdFormatInvalid;
    m_isConverged.store(false);
    m_numMappers.store(0);
    m_mappedBuffer = std::vector<uint8_t>();
}

void* HdRprRenderBuffer::Map() {
    if (!IsMappable()) { return nullptr; }

    ++m_numMappers;
    return m_mappedBuffer.data();
}

void HdRprRenderBuffer::Unmap() {
    if (!IsMappable()) { return; }

    // XXX We could consider clearing _mappedBuffer here to free RAM.
    //     For now we assume that Map() will be called frequently so we prefer
    //     to avoid the cost of clearing the buffer over memory savings.
    // m_mappedBuffer.clear();
    // m_mappedBuffer.shrink_to_fit();
    --m_numMappers;
}

bool HdRprRenderBuffer::IsMapped() const {
    return m_numMappers.load() != 0;
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

bool HdRprRenderBuffer::IsMappable() const {
    return m_isValid &&
        m_commitWidth == m_width &&
        m_commitHeight == m_height &&
        m_commitFormat == m_format &&
        m_isDataAvailable.load();
}

void HdRprRenderBuffer::MarkAsReadyForMapping() {
    m_isDataAvailable.store(true);
}

void* HdRprRenderBuffer::Commit(bool isValid) {
    m_isValid = isValid;

    if (m_isValid) {
        if (m_width != m_commitWidth ||
            m_height != m_commitHeight ||
            m_format != m_commitFormat) {
            size_t dataByteSize = m_width * m_height * HdDataSizeOfFormat(m_format);
            size_t commitDataByteSize = m_commitWidth * m_commitHeight * HdDataSizeOfFormat(m_commitFormat);

            m_width = m_commitWidth;
            m_height = m_commitHeight;
            m_format = m_commitFormat;

            if (commitDataByteSize != dataByteSize) {
                if (commitDataByteSize) {
                    m_mappedBuffer.reserve(commitDataByteSize);
                } else {
                    m_mappedBuffer = std::vector<uint8_t>();
                }
            }

            m_isDataAvailable.store(false);
        }
    } else {
        m_mappedBuffer = std::vector<uint8_t>();
    }

    return m_mappedBuffer.data();
}

PXR_NAMESPACE_CLOSE_SCOPE
