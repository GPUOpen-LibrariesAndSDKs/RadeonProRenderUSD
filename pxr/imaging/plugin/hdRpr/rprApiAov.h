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
#include <optional>

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;
class HdRprApiAov;
class HdRprApiColorAov;
struct RprUsdContextMetadata;

///
///	Builder for HdRprApiAov designed to simplify AOV creation with lot of optional parameters
///

class HdRprApiAovBuilder {
public:
	HdRprApiAovBuilder& WithType(rpr_aov type);
	HdRprApiAovBuilder& WithSize(int width, int height);
	HdRprApiAovBuilder& WithFormat(HdFormat format);
	HdRprApiAovBuilder& WithRprContext(rpr::Context* context);
	HdRprApiAovBuilder& WithRifContext(rif::Context* context);
	HdRprApiAovBuilder& WithRprContextMetadata(RprUsdContextMetadata* metadata);
	HdRprApiAovBuilder& WithRenderResolution(float renderResolution);
	HdRprApiAovBuilder& WithRawColorAov(std::shared_ptr<HdRprApiColorAov> rawColorAov);
	HdRprApiAovBuilder& WithWorldCoordinateAov(std::shared_ptr<HdRprApiAov> worldCoordinateAov);
	HdRprApiAovBuilder& WithBaseIdAov(std::shared_ptr<HdRprApiAov> baseIdAov);
	HdRprApiAovBuilder& WithAovDesc(HdRprAovDescriptor aovDesc);

	HdRprApiAov* Build();

private:
	friend class HdRprApiAov;
	HdRprApiAovBuilder() = default;

	std::optional<rpr_aov> type;
	std::optional<int> width;
	std::optional<int> height;
	std::optional<HdFormat> format;
	std::optional<rpr::Context*> rprContext;
	std::optional<RprUsdContextMetadata*> rprContextMetadata;
	std::optional<rif::Context*> rifContext;
	std::optional<float> renderResolution;
	std::optional<std::shared_ptr<HdRprApiColorAov>> rawColorAov;
	std::optional<std::shared_ptr<HdRprApiAov>> worldCoordinateAov;
	std::optional<std::shared_ptr<HdRprApiAov>> baseIdAov;
	std::optional<HdRprAovDescriptor> aovDesc;
};

///
///	Base class for all AOV's
///	HdRprApiAovBuilder is used to create instance
///

class HdRprApiAov {
public:
	friend class HdRprApiAovBuilder;
	static HdRprApiAovBuilder Builder() { return HdRprApiAovBuilder(); }
    virtual ~HdRprApiAov() = default;

    virtual void Resize(int width, int height, HdFormat format, float renderResolution);
    virtual void Update(HdRprApi const* rprApi, rif::Context* rifContext);
    virtual void Resolve();
    virtual bool GetData(void* dstBuffer, size_t dstBufferSize);

	virtual HdRprApiFramebuffer* GetRifInputFramebuffer() {
		return GetResolvedFb();
	}

    void Clear();

	HdFormat GetFormat() const {
		return m_format;
	}

	HdRprAovDescriptor const& GetDesc() const {
		return m_aovDescriptor;
	}

	HdRprApiFramebuffer* GetAovFb() const {
		return m_aov.get();
	}

	HdRprApiFramebuffer* GetResolvedFb() const {
		return (m_resolved ? m_resolved : m_aov).get();
	}

protected:
	/// Create and own RPR framebuffer
	HdRprApiAov(int width,
				int height,
				float renderResolution,
				HdFormat format,
				rif::Context* rifContext,
				rpr_aov rprAovType,
				rpr::Context* rprContext,
				RprUsdContextMetadata const& rprContextMetadata);

	/// Doesn't own framebuffer directly
	HdRprApiAov(int width,
				int height,
				float renderResolution,
				HdFormat format,
				rif::Context* rifContext,
				HdRprAovDescriptor const& aovDescriptor);

	/// Control filter priority here
	enum class FilterType {
		kFilterNone,
		kFilterAIDenoise,
		kFilterEAWDenoise,
		kFilterComposeOpacity,
		kFilterNdcDepth,
		kFilterRemapRange,
		kFilterTonemap,
		kFilterUserDefined,
		kFilterUpscale,
		kFilterResample,
	};

	enum ChangeTracker {
		Clean = 0,
		AllDirty = ~0u,
		DirtySize = 1 << 0,
		DirtyFormat = 1 << 1,
	};

	virtual void OnFormatChange(rif::Context* rifContext);
	virtual void OnSizeChange(rif::Context* rifContext);

	rif::Filter* FindFilter(FilterType type);
	void SetFilter(FilterType type, bool enable);
	void AddFilter(FilterType type, std::unique_ptr<rif::Filter> filter);

	bool IsFiltersEmpty() const {
		return m_filters.empty();
	}

    HdFormat m_format;
	int m_width = 0;
	int m_height = 0;
	float m_renderResolution = 1.0f;

	std::unique_ptr<HdRprApiFramebuffer> m_aov;
	std::unique_ptr<HdRprApiFramebuffer> m_resolved;
	std::uint32_t m_dirtyBits = ChangeTracker::AllDirty;

private:
    bool GetDataImpl(void* dstBuffer, size_t dstBufferSize);

	std::list<std::pair<FilterType, std::unique_ptr<rif::Filter>>> m_filters;
	rif::Context* m_rifContext = nullptr;
    HdRprAovDescriptor const& m_aovDescriptor;
};

class HdRprApiColorAov : public HdRprApiAov {
public:
	friend class HdRprApiAovBuilder;
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

	HdRprApiFramebuffer* GetRifInputFramebuffer() override {
		return m_retainedRawColor->GetResolvedFb();
	}

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

	struct UpscaleParams {
		bool enable;

		enum class Mode {
			Good,
			Best,
			Fast
		};

		Mode mode;
	};

    void SetTonemap(TonemapParams const& params);
    void SetUpscale(UpscaleParams const& params);

protected:
	void OnFormatChange(rif::Context* rifContext) override;
	void OnSizeChange(rif::Context* rifContext) override;

private:
    HdRprApiColorAov(int width, 
					 int height, 
					 float renderResolution, 
					 HdFormat format,
					 rif::Context* rifContext,
					 rpr::Context* rprContext, 
					 RprUsdContextMetadata const& rprContextMetadata,
					 std::shared_ptr<HdRprApiAov> rawColorAov);
    
	void SetTonemapFilterParams(rif::Filter* filter);
    bool CanComposeAlpha();

    std::shared_ptr<HdRprApiAov> m_retainedRawColor;
    std::shared_ptr<HdRprApiAov> m_retainedOpacity;
    std::shared_ptr<HdRprApiAov> m_retainedDenoiseInputs[rif::MaxInput];
    FilterType m_denoiseFilterType = FilterType::kFilterNone;
    TonemapParams m_tonemap;
};

class HdRprApiNormalAov : public HdRprApiAov {
public:
	friend class HdRprApiAovBuilder;
    ~HdRprApiNormalAov() override = default;

protected:
    void OnFormatChange(rif::Context* rifContext) override;

private:
	HdRprApiNormalAov(int width,
					  int height,
					  float renderResolution,
					  HdFormat format,
					  rif::Context* rifContext,
					  rpr::Context* rprContext, 
					  RprUsdContextMetadata const& rprContextMetadata);
};

class HdRprApiComputedAov : public HdRprApiAov {
public:
    ~HdRprApiComputedAov() override = default;

protected:
	HdRprApiComputedAov(int width,
						int height,
						float renderResolution,
						HdFormat format,
						rif::Context* rifContext,
						HdRprAovDescriptor const& aovDescriptor);
};

class HdRprApiDepthAov : public HdRprApiComputedAov {
public:
	friend class HdRprApiAovBuilder;
    ~HdRprApiDepthAov() override = default;

    void Update(HdRprApi const* rprApi, rif::Context* rifContext) override;

	HdRprApiFramebuffer* GetRifInputFramebuffer() override {
		return m_retainedWorldCoordinateAov->GetResolvedFb();
	}

private:
	HdRprApiDepthAov(
		int width,
		int height,
		float renderResolution,
		HdFormat format,
		rif::Context* rifContext,
		rpr::Context* rprContext,
		RprUsdContextMetadata const& rprContextMetadata,
		std::shared_ptr<HdRprApiAov> worldCoordinateAov);

    std::shared_ptr<HdRprApiAov> m_retainedWorldCoordinateAov;
};

class HdRprApiIdMaskAov : public HdRprApiComputedAov {
public:
	friend class HdRprApiAovBuilder;
    ~HdRprApiIdMaskAov() override = default;

	HdRprApiFramebuffer* GetRifInputFramebuffer() override {
		return m_baseIdAov->GetResolvedFb();
	}

private:
	HdRprApiIdMaskAov(
		int width,
		int height,
		float renderResolution,
		HdFormat format,
		rif::Context* rifContext,
		HdRprAovDescriptor const& aovDescriptor,
		rpr::Context* rprContext,
		RprUsdContextMetadata const& rprContextMetadata,
		std::shared_ptr<HdRprApiAov> const& baseIdAov);

    std::shared_ptr<HdRprApiAov> m_baseIdAov;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_AOV_H
