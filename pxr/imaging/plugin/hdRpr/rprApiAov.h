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
#include <list>

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;
struct RprUsdContextMetadata;

class HdRprApiAov {
public:
    HdRprApiAov(
		rpr_aov rprAovType, 
		int width, 
		int height, 
		HdFormat format,
		rpr::Context* rprContext, 
		RprUsdContextMetadata const& rprContextMetadata, 
		rif::Context* rifContext, 
		float renderResolution);
    virtual ~HdRprApiAov() = default;

    virtual void Resize(int width, int height, HdFormat format, float renderResolution);
    virtual void Update(HdRprApi const* rprApi, rif::Context* rifContext);
    virtual void Resolve();

    virtual bool GetData(void* dstBuffer, size_t dstBufferSize);
    void Clear();

    HdFormat GetFormat() const { return m_format; }
    HdRprAovDescriptor const& GetDesc() const { return m_aovDescriptor; }

    HdRprApiFramebuffer* GetAovFb() { return m_aov.get(); };
    HdRprApiFramebuffer* GetResolvedFb();

	virtual HdRprApiFramebuffer* GetRifInputFramebuffer();

protected:
    HdRprApiAov(HdRprAovDescriptor const& aovDescriptor, HdFormat format, rif::Context* rifContext)
        : m_aovDescriptor(aovDescriptor), m_format(format), m_rifContext(rifContext) {};

	HdRprApiAov(HdRprAovDescriptor const& aovDescriptor, HdFormat format, int width, int height, float renderResolution, rif::Context* rifContext)
		: m_aovDescriptor(aovDescriptor), m_format(format), m_width(width), m_height(height), m_renderResolution(renderResolution), m_rifContext(rifContext)
	{};

    virtual void OnFormatChange(rif::Context* rifContext);
    virtual void OnSizeChange(rif::Context* rifContext);

	enum class FilterType
	{
		kFilterNone,
		kFilterAIDenoise,
		kFilterEAWDenoise,
		kFilterComposeOpacity,
		kFilterTonemap,
		kFilterRemapRange,
		kFilterUserDefined,
		kFilterNdcDepth,
		kFilterUpscale,
		kFilterResample,
	};

	rif::Filter* FindFilter(FilterType type);
	void SetFilter(FilterType filter, bool enable);

protected:
    HdRprAovDescriptor const& m_aovDescriptor;
    HdFormat m_format;

    std::unique_ptr<HdRprApiFramebuffer> m_aov;
    std::unique_ptr<HdRprApiFramebuffer> m_resolved;

	std::list<std::pair<FilterType, std::unique_ptr<rif::Filter>>> m_filters;

    enum ChangeTracker {
        Clean = 0,
        AllDirty = ~0u,
        DirtySize = 1 << 0,
        DirtyFormat = 1 << 1,
    };
    uint32_t m_dirtyBits = AllDirty;

	float m_renderResolution = 1.0f;
	int m_width = 0;
	int m_height = 0;

	rif::Context* m_rifContext = nullptr;

private:
    bool GetDataImpl(void* dstBuffer, size_t dstBufferSize);
};

class HdRprApiColorAov : public HdRprApiAov {
public:
    HdRprApiColorAov(HdFormat format, std::shared_ptr<HdRprApiAov> rawColorAov, rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext);
    ~HdRprApiColorAov() override = default;

    bool GetData(void* dstBuffer, size_t dstBufferSize) override;

    void SetOpacityAov(std::shared_ptr<HdRprApiAov> opacity);

    void InitAIDenoise(std::shared_ptr<HdRprApiAov> albedo,
                         std::shared_ptr<HdRprApiAov> normal,
                         std::shared_ptr<HdRprApiAov> linearDepth);
    void InitEAWDenoise(std::shared_ptr<HdRprApiAov> albedo,
                          std::shared_ptr<HdRprApiAov> normal,
                          std::shared_ptr<HdRprApiAov> linearDepth,
                          std::shared_ptr<HdRprApiAov> objectId,
                          std::shared_ptr<HdRprApiAov> worldCoordinate);
    void DeinitDenoise(rif::Context* rifContext);
    void SetDenoise(bool enable, HdRprApi const* rprApi, rif::Context* rifContext);

	HdRprApiFramebuffer* GetRifInputFramebuffer() override;

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

	struct UpscaleParams
	{
		bool enable;

		enum class Mode
		{
			Good,
			Best,
			Fast
		};

		Mode mode;
	};

    void SetTonemap(TonemapParams const& params);
    void SetUpscale(UpscaleParams const& params, HdRprApi const* rprApi, rif::Context* rifContext);

protected:
    void OnFormatChange(rif::Context* rifContext) override;
	void OnSizeChange(rif::Context* rifContext) override;

private:
    void SetTonemapFilterParams(rif::Filter* filter);

    bool CanComposeAlpha();

private:
    std::shared_ptr<HdRprApiAov> m_retainedRawColor;
    std::shared_ptr<HdRprApiAov> m_retainedOpacity;
    std::shared_ptr<HdRprApiAov> m_retainedDenoiseInputs[rif::MaxInput];
    FilterType m_denoiseFilterType = FilterType::kFilterNone;

    TonemapParams m_tonemap;
};

class HdRprApiNormalAov : public HdRprApiAov {
public:
    HdRprApiNormalAov(int width, int height, HdFormat format,
                      rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext, float renderResolution);
    ~HdRprApiNormalAov() override = default;
protected:
    void OnFormatChange(rif::Context* rifContext) override;
};

class HdRprApiComputedAov : public HdRprApiAov {
public:
    HdRprApiComputedAov(HdRprAovDescriptor const& aovDescriptor, int width, int height, HdFormat format, float renderResolution, rif::Context* rifContext)
        : HdRprApiAov(aovDescriptor, format, width, height, renderResolution, rifContext) {}
    ~HdRprApiComputedAov() override = default;
};

class HdRprApiDepthAov : public HdRprApiComputedAov {
public:
    HdRprApiDepthAov(int width, int height, HdFormat format,
                     std::shared_ptr<HdRprApiAov> worldCoordinateAov,
                     rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext, float renderResolution);
    ~HdRprApiDepthAov() override = default;

    void Update(HdRprApi const* rprApi, rif::Context* rifContext) override;

	HdRprApiFramebuffer* GetRifInputFramebuffer() override;

private:
    std::shared_ptr<HdRprApiAov> m_retainedWorldCoordinateAov;
};

class HdRprApiIdMaskAov : public HdRprApiComputedAov {
public:
    HdRprApiIdMaskAov(HdRprAovDescriptor const& aovDescriptor, std::shared_ptr<HdRprApiAov> const& baseIdAov,
                      int width, int height, HdFormat format,
                      rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext, float renderResolution);
    ~HdRprApiIdMaskAov() override = default;

	HdRprApiFramebuffer* GetRifInputFramebuffer() override;

private:
    std::shared_ptr<HdRprApiAov> m_baseIdAov;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_AOV_H
