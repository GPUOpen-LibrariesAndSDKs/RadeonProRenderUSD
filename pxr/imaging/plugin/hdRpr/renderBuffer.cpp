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

PXR_NAMESPACE_CLOSE_SCOPE
