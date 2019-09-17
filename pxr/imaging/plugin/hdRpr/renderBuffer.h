#ifndef HDRPR_RENDER_BUFFER_H
#define HDRPR_RENDER_BUFFER_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderBuffer.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

/// The HdRprRenderBuffer does not actually render buffet
/// It is a stub to be able retrieve AOV Name, to define which RPR render mode to use


class HdRprRenderBuffer final : public HdRenderBuffer {
public:
	HdRprRenderBuffer(SdfPath const& id, HdRprApiSharedPtr rprApi);

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    virtual bool Allocate(GfVec3i const& dimensions,
                          HdFormat format,
                          bool multiSampled) override;

    virtual unsigned int GetWidth() const override;

    virtual unsigned int GetHeight() const override;

    virtual unsigned int GetDepth() const override;

    virtual HdFormat GetFormat() const override;

    virtual bool IsMultiSampled() const override;

    virtual void* Map() override;

    virtual void Unmap() override;

    virtual bool IsMapped() const override;

    virtual void Resolve() override;

    virtual bool IsConverged() const override;

protected:
    virtual void _Deallocate() override;

private:
    HdRprApiWeakPtr m_rprApiWeakPrt;
    TfToken m_aovName;
    HdFormat m_format = HdFormat::HdFormatInvalid;

    std::shared_ptr<char> m_dataCache;
    size_t m_dataCacheSize = 0;
    std::atomic<int> m_numMappers;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_BUFFER_H
