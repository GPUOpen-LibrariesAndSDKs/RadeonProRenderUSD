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

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam *renderParam,
                      HdDirtyBits *dirtyBits) override;
    
    virtual bool Allocate(GfVec3i const& dimensions,
                          HdFormat format,
                          bool multiSampled);
    
    virtual unsigned int GetWidth() const;

    virtual unsigned int GetHeight() const;

    virtual unsigned int GetDepth() const;

    virtual HdFormat GetFormat() const;

    virtual bool IsMultiSampled() const;
    
    virtual uint8_t* Map();

    virtual void Unmap();

    virtual bool IsMapped() const;
    
    virtual void Resolve();
    
    virtual bool IsConverged() const;
    
protected:
    virtual void _Deallocate();
    
private:
    HdRprApiWeakPtr m_rprApiWeakPrt;
    
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_BUFFER_H
