#ifndef HDRPR_RPR_API_AOV_H
#define HDRPR_RPR_API_AOV_H

#include "rprApiFramebuffer.h"
#include "rifcpp/rifFilter.h"
#include "rpr/contextMetadata.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/imaging/hd/types.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;

class HdRprApiAov {
public:
    HdRprApiAov(rpr_aov rprAovType, int width, int height, HdFormat format,
                rpr::Context* rprContext, rpr::ContextMetadata const& rprContextMetadata, std::unique_ptr<rif::Filter> filter);
    HdRprApiAov(rpr_aov rprAovType, int width, int height, HdFormat format,
                rpr::Context* rprContext, rpr::ContextMetadata const& rprContextMetadata, rif::Context* rifContext);
    virtual ~HdRprApiAov() = default;

    virtual void Resize(int width, int height, HdFormat format);
    virtual void Update(HdRprApi const* rprApi, rif::Context* rifContext);
    virtual void Resolve();

    bool GetData(void* dstBuffer, size_t dstBufferSize);
    void Clear();

    HdFormat GetFormat() const { return m_format; }
    HdRprApiFramebuffer* GetAovFb() { return m_aov.get(); };
    HdRprApiFramebuffer* GetResolvedFb();

protected:
    HdRprApiAov() = default;

    virtual void OnFormatChange(rif::Context* rifContext);
    virtual void OnSizeChange(rif::Context* rifContext);

protected:
    std::unique_ptr<HdRprApiFramebuffer> m_aov;
    std::unique_ptr<HdRprApiFramebuffer> m_resolved;
    std::unique_ptr<rif::Filter> m_filter;
    HdFormat m_format = HdFormatInvalid;

    enum ChangeTracker {
        Clean = 0,
        AllDirty = ~0u,
        DirtySize = 1 << 0,
        DirtyFormat = 1 << 1,
    };
    uint32_t m_dirtyBits = AllDirty;

private:
    bool GetDataImpl(void* dstBuffer, size_t dstBufferSize);
};

class HdRprApiColorAov : public HdRprApiAov {
public:
    HdRprApiColorAov(int width, int height, HdFormat format, rpr::Context* rprContext, rpr::ContextMetadata const& rprContextMetadata);
    ~HdRprApiColorAov() override = default;

    void Update(HdRprApi const* rprApi, rif::Context* rifContext) override;
    void Resolve() override;

    void SetOpacityAov(std::shared_ptr<HdRprApiAov> opacity);

    void EnableAIDenoise(std::shared_ptr<HdRprApiAov> albedo,
                         std::shared_ptr<HdRprApiAov> normal,
                         std::shared_ptr<HdRprApiAov> linearDepth);
    void EnableEAWDenoise(std::shared_ptr<HdRprApiAov> albedo,
                          std::shared_ptr<HdRprApiAov> normal,
                          std::shared_ptr<HdRprApiAov> linearDepth,
                          std::shared_ptr<HdRprApiAov> objectId,
                          std::shared_ptr<HdRprApiAov> worldCoordinate);
    void DisableDenoise(rif::Context* rifContext);

protected:
    void OnFormatChange(rif::Context* rifContext) override;
    void OnSizeChange(rif::Context* rifContext) override;

private:
    enum Filter {
        kFilterNone = 0,
        kFilterResample = 1 << 0,
        kFilterAIDenoise = 1 << 1,
        kFilterEAWDenoise = 1 << 2,
        kFilterComposeOpacity = 1 << 3,
    };
    void SetFilter(Filter filter, bool enable);
    
    template <typename T>
    void ResizeFilter(int width, int height, Filter filterType, rif::Filter* filter, T input);

private:
    std::shared_ptr<HdRprApiAov> m_retainedOpacity;
    std::shared_ptr<HdRprApiAov> m_retainedDenoiseInputs[rif::MaxInput];

    Filter m_mainFilterType = kFilterNone;
    std::vector<std::pair<Filter, std::unique_ptr<rif::Filter>>> m_auxFilters;

    uint32_t m_enabledFilters = kFilterNone;
    bool m_isEnabledFiltersDirty = true;
};

class HdRprApiNormalAov : public HdRprApiAov {
public:
    HdRprApiNormalAov(int width, int height, HdFormat format,
                      rpr::Context* rprContext, rpr::ContextMetadata const& rprContextMetadata, rif::Context* rifContext);
    ~HdRprApiNormalAov() override = default;
protected:
    void OnFormatChange(rif::Context* rifContext) override;
    void OnSizeChange(rif::Context* rifContext) override;
};

class HdRprApiDepthAov : public HdRprApiAov {
public:
    HdRprApiDepthAov(HdFormat format,
                     std::shared_ptr<HdRprApiAov> worldCoordinateAov,
                     rpr::Context* rprContext, rpr::ContextMetadata const& rprContextMetadata, rif::Context* rifContext);
    ~HdRprApiDepthAov() override = default;

    void Update(HdRprApi const* rprApi, rif::Context* rifContext) override;
    void Resize(int width, int height, HdFormat format) override;

private:
    std::unique_ptr<rif::Filter> m_retainedFilter;

    rif::Filter* m_ndcFilter;
    rif::Filter* m_remapFilter;

    std::shared_ptr<HdRprApiAov> m_retainedWorldCoordinateAov;
    int m_width;
    int m_height;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_AOV_H
