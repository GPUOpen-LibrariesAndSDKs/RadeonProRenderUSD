#ifndef HDRPR_RENDER_BUFFER_H
#define HDRPR_RENDER_BUFFER_H

#include "pxr/imaging/hd/renderBuffer.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprRenderBuffer final : public HdRenderBuffer {
public:
    HdRprRenderBuffer(SdfPath const& id, HdRprApiSharedPtr rprApi);

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    bool Allocate(GfVec3i const& dimensions,
                  HdFormat format,
                  bool multiSampled) override;

    unsigned int GetWidth() const override { return m_width; }

    unsigned int GetHeight() const override { return m_height; }

    unsigned int GetDepth() const override { return 1u; }

    HdFormat GetFormat() const override { return m_format; }

    bool IsMultiSampled() const override { return false; }

    void* Map() override;

    void Unmap() override;

    bool IsMapped() const override;

    void Resolve() override;

    bool IsConverged() const override;

    void SetConverged(bool converged);

protected:
    virtual void _Deallocate() override;

private:
    HdRprApiWeakPtr m_rprApiWeakPrt;
    TfToken m_aovName;
    uint32_t m_width = 0u;
    uint32_t m_height = 0u;
    HdFormat m_format = HdFormat::HdFormatInvalid;

    std::vector<uint8_t> m_mappedBuffer;
    std::atomic<int> m_numMappers;
    std::atomic<bool> m_isConverged;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_BUFFER_H
