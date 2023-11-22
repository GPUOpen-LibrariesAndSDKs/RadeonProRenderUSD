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

#include "aovDescriptor.h"
#include "rprApiFramebuffer.h"
#include "rifcpp/rifFilter.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/imaging/hd/types.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;
struct RprUsdContextMetadata;

class HdRprApiAov {
public:
    HdRprApiAov(rpr_aov rprAovType, int width, int height, HdFormat format,
                rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, std::unique_ptr<rif::Filter> filter);
    HdRprApiAov(rpr_aov rprAovType, int width, int height, HdFormat format,
                rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext);
    virtual ~HdRprApiAov() = default;

    virtual void Resize(int width, int height, HdFormat format);
    virtual void Update(HdRprApi const* rprApi, rif::Context* rifContext);
    virtual void Resolve();

    virtual bool GetData(void* dstBuffer, size_t dstBufferSize);
    void Clear();

    HdFormat GetFormat() const { return m_format; }
    HdRprAovDescriptor const& GetDesc() const { return m_aovDescriptor; }

    HdRprApiFramebuffer* GetAovFb() { return m_aov.get(); }
    HdRprApiFramebuffer* GetResolvedFb();

    virtual bool GetDataImpl(void* dstBuffer, size_t dstBufferSize);
protected:
    HdRprApiAov(HdRprAovDescriptor const& aovDescriptor, HdFormat format)
        : m_aovDescriptor(aovDescriptor), m_format(format) {};
protected:
    HdRprAovDescriptor const& m_aovDescriptor;
    HdFormat m_format;

    std::unique_ptr<HdRprApiFramebuffer> m_aov;
    std::unique_ptr<HdRprApiFramebuffer> m_resolved;
    bool m_filterEnabled = false;
    std::vector<char> m_tmpBuffer;
    std::vector<char> m_outputBuffer;

    enum ChangeTracker {
        Clean = 0,
        AllDirty = ~0u,
        DirtySize = 1 << 0,
        DirtyFormat = 1 << 1,
    };
    uint32_t m_dirtyBits = AllDirty;
};

class HdRprApiColorAov : public HdRprApiAov {
public:
    HdRprApiColorAov(HdFormat format, std::shared_ptr<HdRprApiAov> rawColorAov, rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata);
    ~HdRprApiColorAov() override = default;

    void Resize(int width, int height, HdFormat format) override;
    void Update(HdRprApi const* rprApi, rif::Context* rifContext) override;
    bool GetData(void* dstBuffer, size_t dstBufferSize) override;
    void Resolve() override;

    void SetOpacityAov(std::shared_ptr<HdRprApiAov> opacity);

    struct TonemapParams {
        bool enable;
        float exposureTime;
        float sensitivity;
        float fstop;
        float gamma;

        bool operator==(TonemapParams const& lhs) {
            return exposureTime == lhs.exposureTime &&
                sensitivity == lhs.sensitivity &&
                fstop == lhs.fstop &&
                gamma == lhs.gamma;
        }
        bool operator!=(TonemapParams const& lhs) {
            return !(*this == lhs);
        }
    };
    void SetTonemap(TonemapParams const& params);

    struct GammaParams {
        bool enable;
        float value;

        bool operator==(GammaParams const& lhs) {
            return value == lhs.value && enable == lhs.enable;
        }
        bool operator!=(GammaParams const& lhs) {
            return !(*this == lhs);
        }
    };
    void SetGamma(GammaParams const& params);

    bool GetDataImpl(void* dstBuffer, size_t dstBufferSize) override;
protected:
    void OnFormatChange(rif::Context* rifContext);// override;
    void OnSizeChange();// override;
private:
    enum Filter {
        kFilterNone = 0,
        kFilterResample = 1 << 0,
        //kFilterAIDenoise = 1 << 1,
        //kFilterEAWDenoise = 1 << 2,
        kFilterComposeOpacity = 1 << 3,
        kFilterTonemap = 1 << 4,
        kFilterGamma = 1 << 5
    };
    void SetFilter(Filter filter, bool enable);
    
    template <typename T>
    void ResizeFilter(int width, int height, Filter filterType, rif::Filter* filter, T input);

    void SetTonemapFilterParams(rif::Filter* filter);

    bool CanComposeAlpha();

private:
    std::shared_ptr<HdRprApiAov> m_retainedRawColor;
    std::shared_ptr<HdRprApiAov> m_retainedOpacity;

    Filter m_mainFilterType = kFilterNone;
    std::unique_ptr<rif::Filter> m_filter;
    std::vector<std::pair<Filter, std::unique_ptr<rif::Filter>>> m_auxFilters;

    uint32_t m_enabledFilters = kFilterNone;
    bool m_isEnabledFiltersDirty = true;

    TonemapParams m_tonemap;
    GammaParams m_gamma;

    int m_width = 0;
    int m_height = 0;
};

class HdRprApiNormalAov : public HdRprApiAov {
public:
    HdRprApiNormalAov(int width, int height, HdFormat format,
        rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext);
    ~HdRprApiNormalAov() override = default;

    void Update(HdRprApi const* rprApi, rif::Context* rifContext) override;
    void Resolve() override;
protected:
    void OnFormatChange(rif::Context* rifContext);// override;
private:
    std::vector<float> m_cpuFilterBuffer;
};

class HdRprApiComputedAov : public HdRprApiAov {
public:
    HdRprApiComputedAov(HdRprAovDescriptor const& aovDescriptor, int width, int height, HdFormat format)
        : HdRprApiAov(aovDescriptor, format), m_width(width), m_height(height) {}
    ~HdRprApiComputedAov() override = default;

    void Resize(int width, int height, HdFormat format) override final;

protected:
    int m_width = -1;
    int m_height = -1;
};

class HdRprApiDepthAov : public HdRprApiComputedAov {
public:
    HdRprApiDepthAov(int width, int height, HdFormat format,
        std::shared_ptr<HdRprApiAov> worldCoordinateAov,
        std::shared_ptr<HdRprApiAov> opacityAov,
        rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata);
    ~HdRprApiDepthAov() override = default;

    void Update(HdRprApi const* rprApi, rif::Context* rifContext) override;
    void Resolve() override;
//protected:
    //bool GetDataImpl(void* dstBuffer, size_t dstBufferSize) override;
private:
    inline size_t cpuFilterBufferSize() const { return m_width * m_height * 4; }    // Vec4f for each pixel

    std::shared_ptr<HdRprApiAov> m_retainedWorldCoordinateAov;
    std::shared_ptr<HdRprApiAov> m_retainedOpacityAov;
    GfMatrix4f m_viewProjectionMatrix;
    std::vector<float> m_cpuFilterBuffer;
};

class HdRprApiIdMaskAov : public HdRprApiComputedAov {
public:
    HdRprApiIdMaskAov(HdRprAovDescriptor const& aovDescriptor, std::shared_ptr<HdRprApiAov> const& baseIdAov,
        int width, int height, HdFormat format,
        rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext);
    ~HdRprApiIdMaskAov() override = default;

    void Update(HdRprApi const* rprApi, rif::Context* rifContext) override;
    void Resolve() override;
private:
    std::shared_ptr<HdRprApiAov> m_baseIdAov;
    std::vector<float> m_cpuFilterBuffer;
};

class HdRprApiScCompositeAOV : public HdRprApiAov {
public:
    HdRprApiScCompositeAOV(int width, int height, HdFormat format,
                         std::shared_ptr<HdRprApiAov> rawColorAov,
                         std::shared_ptr<HdRprApiAov> opacityAov,
                         std::shared_ptr<HdRprApiAov> scAov,
                         rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext);
    bool GetDataImpl(void* dstBuffer, size_t dstBufferSize) override;
private:
    std::shared_ptr<HdRprApiAov> m_retainedRawColorAov;
    std::shared_ptr<HdRprApiAov> m_retainedOpacityAov;
    std::shared_ptr<HdRprApiAov> m_retainedScAov;

    std::vector<GfVec4f> m_tempColorBuffer;
    std::vector<GfVec4f> m_tempOpacityBuffer;
    std::vector<GfVec4f> m_tempScBuffer;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_AOV_H
