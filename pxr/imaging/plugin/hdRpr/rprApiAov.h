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
#include "pxr/base/gf/matrix4f.h"
#include "pxr/imaging/hd/types.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;
struct RprUsdContextMetadata;

void CpuResampleNearest(GfVec4f* src, size_t srcWidth, size_t srcHeight, GfVec4f* dest, size_t destWidth, size_t destHeight);

class HdRprApiAov {
public:
    HdRprApiAov(rpr_aov rprAovType, int width, int height, HdFormat format,
                rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata);
    virtual ~HdRprApiAov() = default;

    virtual void Resize(int width, int height, HdFormat format);
    void Update(HdRprApi const* rprApi);
    void Resolve();

    virtual bool GetData(void* dstBuffer, size_t dstBufferSize);
    void Clear();

    HdFormat GetFormat() const { return m_format; }
    HdRprAovDescriptor const& GetDesc() const { return m_aovDescriptor; }

    HdRprApiFramebuffer* GetAovFb() { return m_aov.get(); }
    HdRprApiFramebuffer* GetResolvedFb();
protected:
    HdRprApiAov(HdRprAovDescriptor const& aovDescriptor, HdFormat format)
        : m_aovDescriptor(aovDescriptor), m_format(format) {};
    virtual void UpdateImpl(HdRprApi const* rprApi);
    virtual void ResolveImpl();
protected:
    HdFormat m_format;

    std::unique_ptr<HdRprApiFramebuffer> m_aov;
    std::vector<float> m_outputBuffer;

    enum ChangeTracker {
        Clean = 0,
        AllDirty = ~0u,
        DirtySize = 1 << 0,
        DirtyFormat = 1 << 1,
    };
    uint32_t m_dirtyBits = AllDirty;
private:
    bool GetDataImpl(void* dstBuffer, size_t dstBufferSize);
    void UpdateTempBufferSize(int width, int height, HdFormat format);
    void UpdateTempBuffer();

    HdRprAovDescriptor const& m_aovDescriptor;
    bool m_filterEnabled = false;
    size_t m_requiredTempBufferSize = 0;
    std::vector<char> m_tmpBuffer;
    std::unique_ptr<HdRprApiFramebuffer> m_resolved;
};

class HdRprApiColorAov : public HdRprApiAov {
public:
    HdRprApiColorAov(HdFormat format, std::shared_ptr<HdRprApiAov> rawColorAov, rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata);
    ~HdRprApiColorAov() override = default;

    void Resize(int width, int height, HdFormat format) override;

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
protected:
    void UpdateImpl(HdRprApi const* rprApi);
    void ResolveImpl() override;
private:
    void OnFormatChange();

    enum Filter {
        kFilterNone = 0,
        kFilterResample = 1 << 0,
        kFilterComposeOpacity = 1 << 3,
        kFilterTonemap = 1 << 4,
        kFilterGamma = 1 << 5
    };
    void SetFilter(Filter filter, bool enable);
    bool CanComposeAlpha();
private:
    std::shared_ptr<HdRprApiAov> m_retainedRawColor;
    std::shared_ptr<HdRprApiAov> m_retainedOpacity;

    uint32_t m_enabledFilters = kFilterNone;
    bool m_isEnabledFiltersDirty = true;

    TonemapParams m_tonemap;
    GammaParams m_gamma;

    int m_width = 0;
    int m_height = 0;

    std::vector<float> m_opacityBuffer;
};

class HdRprApiNormalAov : public HdRprApiAov {
public:
    HdRprApiNormalAov(int width, int height, HdFormat format,
        rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata);
    ~HdRprApiNormalAov() override = default;
protected:
    void UpdateImpl(HdRprApi const* rprApi) override;
    void ResolveImpl() override;
private:
    //std::vector<float> m_cpuFilterBuffer;
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
protected:
    void UpdateImpl(HdRprApi const* rprApi) override;
    void ResolveImpl() override;
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
        rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata);
    ~HdRprApiIdMaskAov() override = default;
protected:
    void UpdateImpl(HdRprApi const* rprApi) override;
    void ResolveImpl() override;
private:
    std::shared_ptr<HdRprApiAov> m_baseIdAov;
    std::vector<float> m_cpuFilterBuffer;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_AOV_H
