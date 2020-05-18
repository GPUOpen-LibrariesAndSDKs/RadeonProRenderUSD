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

    struct TonemapParams {
        bool enable;
        float exposure;
        float sensitivity;
        float fstop;
        float gamma;

        bool operator==(TonemapParams const& lhs) {
            return exposure == lhs.exposure &&
                sensitivity == lhs.sensitivity &&
                fstop == lhs.fstop &&
                gamma == lhs.gamma;
        }
        bool operator!=(TonemapParams const& lhs) {
            return !(*this == lhs);
        }
    };
    void SetTonemap(TonemapParams const& params);

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
        kFilterTonemap = 1 << 4,
    };
    void SetFilter(Filter filter, bool enable);
    
    template <typename T>
    void ResizeFilter(int width, int height, Filter filterType, rif::Filter* filter, T input);

    void SetTonemapFilterParams(rif::Filter* filter);

private:
    std::shared_ptr<HdRprApiAov> m_retainedOpacity;
    std::shared_ptr<HdRprApiAov> m_retainedDenoiseInputs[rif::MaxInput];

    Filter m_mainFilterType = kFilterNone;
    std::vector<std::pair<Filter, std::unique_ptr<rif::Filter>>> m_auxFilters;

    uint32_t m_enabledFilters = kFilterNone;
    bool m_isEnabledFiltersDirty = true;

    TonemapParams m_tonemap;
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
