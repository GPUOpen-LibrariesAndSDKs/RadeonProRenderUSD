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
    , m_api(api)
{

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
    TF_VERIFY(!IsMapped());
    TF_UNUSED(multiSampled);

    if (dimensions[2] != 1) {
        TF_WARN("HdRprRenderBuffer supports 2D buffers only");
        return false;
    }

    _Deallocate();

    m_width = dimensions[0];
    m_height = dimensions[1];
    m_format = format;
    size_t dataByteSize = m_width * m_height * HdDataSizeOfFormat(m_format);
    m_mappedBuffer.resize(dataByteSize, 0);

    return false;
}

void HdRprRenderBuffer::_Deallocate() {
    TF_VERIFY(!IsMapped());

    m_width = 0u;
    m_height = 0u;
    m_format = HdFormatInvalid;
    m_isConverged.store(false);
    m_numMappers.store(0);
    m_mappedBuffer.resize(0);
}

void* HdRprRenderBuffer::Map() {
    ++m_numMappers;
    return m_mappedBuffer.data();
}

void HdRprRenderBuffer::Unmap() {
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

VtValue HdRprRenderBuffer::GetResource(bool multiSampled) const {
    if ("aov_color" == GetId().GetElementString()) {
        rpr::FrameBuffer* color = m_api->GetColorFramebuffer();

        // RPR framebuffer not created yet
        if (color == nullptr)
        {
            return VtValue();
        }

        rpr::PluginType type = m_api->GetActivePluginType();
        bool isHybrid = false;
        if (type == rpr::PluginType::kPluginHybrid) {
            isHybrid = true;
        }

        VtDictionary dictionary;
        dictionary["is_hybrid"] = isHybrid;
        dictionary["framebuffer"] = color;

        return VtValue(dictionary);
    }
    return VtValue();
}

PXR_NAMESPACE_CLOSE_SCOPE
