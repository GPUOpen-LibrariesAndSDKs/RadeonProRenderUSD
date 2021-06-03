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

#include "rprApiAov.h"
#include "rprApi.h"
#include "rprApiFramebuffer.h"
#include "rifcpp/rifError.h"

#include "pxr/imaging/rprUsd/contextMetadata.h"
#include "pxr/imaging/rprUsd/error.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

bool ReadRifImage(rif_image image, void* dstBuffer, size_t dstBufferSize) {
    if (!image || !dstBuffer) {
        return false;
    }

    size_t size;
    size_t dummy;
    auto rifStatus = rifImageGetInfo(image, RIF_IMAGE_DATA_SIZEBYTE, sizeof(size), &size, &dummy);
    if (rifStatus != RIF_SUCCESS || dstBufferSize < size) {
        return false;
    }

    void* data = nullptr;
    rifStatus = rifImageMap(image, RIF_IMAGE_MAP_READ, &data);
    if (rifStatus != RIF_SUCCESS) {
        return false;
    }

    std::memcpy(dstBuffer, data, size);

    rifStatus = rifImageUnmap(image, data);
    if (rifStatus != RIF_SUCCESS) {
        TF_WARN("Failed to unmap rif image");
    }

    return true;
}

} // namespace anonymous

HdRprApiAov::HdRprApiAov(rpr_aov rprAovType, int width, int height, HdFormat format,
	rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext,
	float renderResolution)
    : m_aovDescriptor(HdRprAovRegistry::GetInstance().GetAovDesc(rprAovType, false))
    , m_format(format)
	, m_width(width)
	, m_height(height)
	, m_renderResolution(renderResolution)
	, m_rifContext(rifContext) {
    if (rif::Image::GetDesc(0, 0, format).type == 0) {
        RIF_THROW_ERROR_MSG("Unsupported format: " + TfEnum::GetName(format));
    }

    m_aov = pxr::make_unique<HdRprApiFramebuffer>(rprContext, width * renderResolution, height * renderResolution);
    m_aov->AttachAs(rprAovType);

    // XXX (Hybrid): Hybrid plugin does not support framebuffer resolving (rprContextResolveFrameBuffer)
    if (rprContextMetadata.pluginType != kPluginHybrid) {
        m_resolved = pxr::make_unique<HdRprApiFramebuffer>(rprContext, width * renderResolution, height * renderResolution);
    }

	// RPR framebuffers by default with such format
	if (format != HdFormatFloat32Vec4)
	{
		SetFilter(FilterType::kFilterResample, true);
	}

	if (renderResolution != 1.0f)
	{
		SetFilter(FilterType::kFilterResample, true);
	}
}

void HdRprApiAov::Resolve() {
    if (m_aov) {
        m_aov->Resolve(m_resolved.get());
    }

	for (auto& filter : m_filters)
	{
		filter.second->Resolve();
	}
}

void HdRprApiAov::Clear() {
    if (m_aov) {
        auto& v = m_aovDescriptor.clearValue;
        m_aov->Clear(v[0], v[1], v[2], v[3]);
    }
}

bool HdRprApiAov::GetDataImpl(void* dstBuffer, size_t dstBufferSize) {
    if (!m_filters.empty()) {
        return ReadRifImage(m_filters.back().second->GetOutput(), dstBuffer, dstBufferSize);
    }

    auto resolvedFb = GetResolvedFb();
    if (!resolvedFb) {
        return false;
    }

    return resolvedFb->GetData(dstBuffer, dstBufferSize);
}

bool HdRprApiAov::GetData(void* dstBuffer, size_t dstBufferSize) {
    if (GetDataImpl(dstBuffer, dstBufferSize)) {
        if (m_format == HdFormatInt32) {
            // RPR store integer ID values to RGB images using such formula:
            // c[i].x = i;
            // c[i].y = i/256;
            // c[i].z = i/(256*256);
            // i.e. saving little endian int24 to uchar3
            // That's why we interpret the value as int and filling the alpha channel with zeros
            auto primIdData = reinterpret_cast<int*>(dstBuffer);
            for (size_t i = 0; i < dstBufferSize / sizeof(int); ++i) {
                primIdData[i] = (primIdData[i] & 0xFFFFFF) - 1;
            }
        }

        return true;
    }

    return false;
}

void HdRprApiAov::Resize(int width, int height, HdFormat format, float renderResolution) {
	if (m_width != width || m_height != height || m_renderResolution != renderResolution) {
		m_width = width;
		m_height = height;
		m_renderResolution = renderResolution;
		m_dirtyBits |= ChangeTracker::DirtySize;
	}

    if (m_format != format) {
        m_format = format;
        m_dirtyBits |= ChangeTracker::DirtyFormat;
    }

    if (m_aov && m_aov->Resize(width * m_renderResolution, height * m_renderResolution)) {
        m_dirtyBits |= ChangeTracker::DirtySize;
    }

    if (m_resolved && m_resolved->Resize(width * m_renderResolution, height * m_renderResolution)) {
        m_dirtyBits |= ChangeTracker::DirtyFormat;
    }
}

void HdRprApiAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_dirtyBits & ChangeTracker::DirtyFormat) {
        OnFormatChange(rifContext);
    }

    if (m_dirtyBits & ChangeTracker::DirtySize) {
        OnSizeChange(rifContext);
    }

    m_dirtyBits = ChangeTracker::Clean;

	for (auto& filter : m_filters)
	{
		filter.second->Update();
	}
}

HdRprApiFramebuffer* HdRprApiAov::GetResolvedFb() {
    return (m_resolved ? m_resolved : m_aov).get();
}

HdRprApiFramebuffer* HdRprApiAov::GetRifInputFramebuffer()
{
	return GetResolvedFb();
}

rif::Filter* HdRprApiAov::FindFilter(FilterType type)
{
	auto it = std::find_if(m_filters.begin(), m_filters.end(), [type](auto& entry) {
		return entry.first == type;
	});

	if (it != m_filters.end())
	{
		return it->second.get();
	}
	else
	{
		return nullptr;
	}
}

void HdRprApiAov::OnFormatChange(rif::Context* rifContext) {
    if (rifContext && m_format != HdFormatFloat32Vec4) {
		SetFilter(FilterType::kFilterResample, true);

        // Reset inputs
        m_dirtyBits |= ChangeTracker::DirtySize;
    }
}

void HdRprApiAov::OnSizeChange(rif::Context* rifContext) {
	if (m_filters.empty())
	{
		return;
	}

	auto resizeFilter = [this](int width, int height, rif::Filter* filter, auto input) {
		filter->Resize(width, height);
		filter->SetInput(rif::Color, input);
		filter->SetOutput(rif::Image::GetDesc(width, height, m_format));
	};

	if (m_filters.size() == 1)
	{
		resizeFilter(m_width, m_height, m_filters.back().second.get(), GetRifInputFramebuffer());
	}
	else
	{
		void* filterInput = GetRifInputFramebuffer();

		for (auto it = m_filters.begin(); it != std::prev(m_filters.end()); ++it)
		{
			if (it == m_filters.begin())
			{
				resizeFilter(m_width * m_renderResolution, m_height * m_renderResolution, it->second.get(), (HdRprApiFramebuffer*)filterInput);
			}
			else
			{
				resizeFilter(m_width * m_renderResolution, m_height * m_renderResolution, it->second.get(), (rif_image)filterInput);
			}
			filterInput = it->second->GetOutput();
		}

		resizeFilter(m_width, m_height, m_filters.back().second.get(), (rif_image)filterInput);
	}
}

HdRprApiColorAov::HdRprApiColorAov(HdFormat format, std::shared_ptr<HdRprApiAov> rawColorAov, rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext)
    : HdRprApiAov(HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kColorAlpha), true), format, rifContext)
    , m_retainedRawColor(std::move(rawColorAov)) {

}

void HdRprApiAov::SetFilter(FilterType filter, bool enable)
{
	// Disable filter
	if (!enable)
	{
		m_filters.remove_if([filter](auto& entry) { return entry.first == filter; });
		return;
	}

	// Enable filter
	std::unique_ptr<rif::Filter> createdFilter;

	switch (filter)
	{
	case FilterType::kFilterTonemap:
		{
			createdFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_PHOTO_LINEAR_TONEMAP, m_rifContext);
		}
		break;

	case FilterType::kFilterAIDenoise:
	case FilterType::kFilterEAWDenoise:
		{
			auto denoiseFilterType = (filter == FilterType::kFilterAIDenoise) ? rif::FilterType::AIDenoise : rif::FilterType::EawDenoise;
			createdFilter = rif::Filter::Create(denoiseFilterType, m_rifContext, m_width, m_height);
		}
		break;

	case FilterType::kFilterComposeOpacity:
		{
			createdFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_USER_DEFINED, m_rifContext);
			auto opacityComposingKernelCode = std::string(R"(
                           int2 coord;
                           GET_COORD_OR_RETURN(coord, GET_BUFFER_SIZE(inputImage));
                           vec4 alpha = ReadPixelTyped(alphaImage, coord.x, coord.y);
                           vec4 color = ReadPixelTyped(inputImage, coord.x, coord.y) * alpha.x;
                           WritePixelTyped(outputImage, coord.x, coord.y, make_vec4(color.x, color.y, color.z, alpha.x));
                       )");
			createdFilter->SetParam("code", opacityComposingKernelCode);
		}
		break;

	case FilterType::kFilterUpscale:
		{
			createdFilter = rif::Filter::Create(rif::FilterType::Upscale, m_rifContext, 1, 1);
		}
		break;

	case FilterType::kFilterResample:
		{
			createdFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_RESAMPLE, m_rifContext);
			createdFilter->SetParam("interpOperator", (int)RIF_IMAGE_INTERPOLATION_NEAREST);
			createdFilter->SetParam("outSize", GfVec2i(m_width, m_height));
		}
		break;

	default:
		throw std::runtime_error("Unsupported filter was set");
	}

	if (filter == FilterType::kFilterResample)
	{
		m_filters.emplace_back(filter, std::move(createdFilter));
	}
	else
	{
		m_filters.emplace_front(filter, std::move(createdFilter));
	}

	// Signal to update inputs
	m_dirtyBits |= ChangeTracker::DirtySize;
}

void HdRprApiColorAov::SetOpacityAov(std::shared_ptr<HdRprApiAov> opacity) {
    if (m_retainedOpacity != opacity) {
        m_retainedOpacity = opacity;
        SetFilter(FilterType::kFilterComposeOpacity, CanComposeAlpha());
    }
}

void HdRprApiColorAov::InitAIDenoise(
    std::shared_ptr<HdRprApiAov> albedo,
    std::shared_ptr<HdRprApiAov> normal,
    std::shared_ptr<HdRprApiAov> linearDepth) {
    if (FindFilter(FilterType::kFilterAIDenoise)) {
        return;
    }
    if (!albedo || !normal || !linearDepth) {
        TF_RUNTIME_ERROR("Failed to enable AI denoise: invalid parameters");
        return;
    }

    for (auto& retainedInput : m_retainedDenoiseInputs) {
        retainedInput = nullptr;
    }
    m_retainedDenoiseInputs[rif::Normal] = normal;
    m_retainedDenoiseInputs[rif::LinearDepth] = linearDepth;
    m_retainedDenoiseInputs[rif::Albedo] = albedo;

    m_denoiseFilterType = FilterType::kFilterAIDenoise;
}

void HdRprApiColorAov::InitEAWDenoise(
    std::shared_ptr<HdRprApiAov> albedo,
    std::shared_ptr<HdRprApiAov> normal,
    std::shared_ptr<HdRprApiAov> linearDepth,
    std::shared_ptr<HdRprApiAov> objectId,
    std::shared_ptr<HdRprApiAov> worldCoordinate) {
    if (FindFilter(FilterType::kFilterEAWDenoise)) {
        return;
    }
    if (!albedo || !normal || !linearDepth || !objectId || !worldCoordinate) {
        TF_RUNTIME_ERROR("Failed to enable EAW denoise: invalid parameters");
        return;
    }

    for (auto& retainedInput : m_retainedDenoiseInputs) {
        retainedInput = nullptr;
    }
    m_retainedDenoiseInputs[rif::Normal] = normal;
    m_retainedDenoiseInputs[rif::LinearDepth] = linearDepth;
    m_retainedDenoiseInputs[rif::ObjectId] = objectId;
    m_retainedDenoiseInputs[rif::Albedo] = albedo;
    m_retainedDenoiseInputs[rif::WorldCoordinate] = worldCoordinate;

    m_denoiseFilterType = FilterType::kFilterEAWDenoise;
}

void HdRprApiColorAov::DeinitDenoise(rif::Context* rifContext) {
    for (auto& retainedInput : m_retainedDenoiseInputs) {
        retainedInput = nullptr;
    }

    m_denoiseFilterType = FilterType::kFilterNone;
}

void HdRprApiColorAov::SetDenoise(bool enable, HdRprApi const* rprApi, rif::Context* rifContext) {
    if (m_denoiseFilterType != FilterType::kFilterNone) {
        SetFilter(m_denoiseFilterType, enable);
        SetFilter(m_denoiseFilterType == FilterType::kFilterAIDenoise ? FilterType::kFilterEAWDenoise : FilterType::kFilterAIDenoise, false);
    } else {
        SetFilter(FilterType::kFilterAIDenoise, false);
        SetFilter(FilterType::kFilterEAWDenoise, false);
    }

    SetFilter(FilterType::kFilterResample, m_format != HdFormatFloat32Vec4);

    Update(rprApi, rifContext);
}

HdRprApiFramebuffer* HdRprApiColorAov::GetRifInputFramebuffer()
{
	return m_retainedRawColor->GetResolvedFb();
}

void HdRprApiColorAov::SetTonemap(TonemapParams const& params) {
    bool isTonemapEnabled = FindFilter(FilterType::kFilterTonemap);
    bool tonemapEnableDirty = params.enable != isTonemapEnabled;

    SetFilter(FilterType::kFilterTonemap, params.enable);

    if (m_tonemap != params) {
        m_tonemap = params;

        if (!tonemapEnableDirty && isTonemapEnabled) {
			SetTonemapFilterParams(FindFilter(FilterType::kFilterTonemap));
        }
    }
}

void HdRprApiColorAov::SetUpscale(UpscaleParams const& params, HdRprApi const* rprApi, rif::Context* rifContext)
{
	SetFilter(FilterType::kFilterUpscale, params.enable);
	Update(rprApi, rifContext);
}

void HdRprApiColorAov::SetTonemapFilterParams(rif::Filter* filter) {
    filter->SetParam("exposureTime", m_tonemap.exposureTime);
    filter->SetParam("sensitivity", m_tonemap.sensitivity);
    filter->SetParam("fstop", m_tonemap.fstop);
    filter->SetParam("gamma", m_tonemap.gamma);
}

bool HdRprApiColorAov::CanComposeAlpha() {
    // Compositing alpha into framebuffer with less than 4 components is a no-op
    return HdGetComponentCount(m_format) == 4 && m_retainedOpacity;
}

bool HdRprApiColorAov::GetData(void* dstBuffer, size_t dstBufferSize) {
    if (m_filters.empty()) {
        if (auto resolvedRawColorFb = m_retainedRawColor->GetResolvedFb()) {
            return resolvedRawColorFb->GetData(dstBuffer, dstBufferSize);
        } else {
            return false;
        }
    } else {
        return HdRprApiAov::GetData(dstBuffer, dstBufferSize);
    }
}

void HdRprApiColorAov::OnFormatChange(rif::Context* rifContext) {
    SetFilter(FilterType::kFilterResample, m_format != HdFormatFloat32Vec4);
    SetFilter(FilterType::kFilterComposeOpacity, CanComposeAlpha());
    m_dirtyBits |= ChangeTracker::DirtySize;
}

void HdRprApiColorAov::OnSizeChange(rif::Context* rifContext) {
	if (auto filter = FindFilter(FilterType::kFilterAIDenoise) ? FindFilter(FilterType::kFilterAIDenoise) : FindFilter(FilterType::kFilterEAWDenoise))
	{
		for (int i = 0; i < rif::MaxInput; ++i)
		{
			if (auto retainedInput = m_retainedDenoiseInputs[i].get())
			{
				filter->SetInput(static_cast<rif::FilterInputType>(i), retainedInput->GetResolvedFb());
			}
		}
	}

	if (auto filter = FindFilter(FilterType::kFilterComposeOpacity))
	{
		filter->SetInput("alphaImage", m_retainedOpacity->GetResolvedFb());
	}

	if (auto filter = FindFilter(FilterType::kFilterTonemap))
	{
		SetTonemapFilterParams(filter);
	}

	HdRprApiAov::OnSizeChange(rifContext);
}

HdRprApiNormalAov::HdRprApiNormalAov(
    int width, int height, HdFormat format,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext, float renderResolution)
    : HdRprApiAov(RPR_AOV_SHADING_NORMAL, width, height, format, rprContext, rprContextMetadata, rifContext, renderResolution) {
    if (!rifContext) {
        RPR_THROW_ERROR_MSG("Can not create normal AOV: RIF context required");
    }

	auto filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_REMAP_RANGE, rifContext);

	filter->SetParam("srcRangeAuto", 0);
	filter->SetParam("dstLo", -1.0f);
	filter->SetParam("dstHi", 1.0f);

	m_filters.emplace_back(FilterType::kFilterRemapRange, std::move(filter));
}

void HdRprApiNormalAov::OnFormatChange(rif::Context* rifContext) {
    m_dirtyBits |= ChangeTracker::DirtySize;
}

HdRprApiDepthAov::HdRprApiDepthAov(
    int width, int height, HdFormat format,
    std::shared_ptr<HdRprApiAov> worldCoordinateAov,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext, float renderResolution)
    : HdRprApiComputedAov(HdRprAovRegistry::GetInstance().GetAovDesc(rpr::Aov(kNdcDepth), true), width, height, format, renderResolution, rifContext)
    , m_retainedWorldCoordinateAov(worldCoordinateAov) {
    if (!rifContext) {
        RPR_THROW_ERROR_MSG("Can not create depth AOV: RIF context required");
    }

    auto ndcFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_NDC_DEPTH, rifContext);
	m_filters.emplace_back(FilterType::kFilterNdcDepth, std::move(ndcFilter));

#if PXR_VERSION >= 2002

    auto remapFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_REMAP_RANGE, rifContext);
	remapFilter->SetParam("srcRangeAuto", 0);
	remapFilter->SetParam("srcLo", -1.0f);
	remapFilter->SetParam("srcHi", 1.0f);
	remapFilter->SetParam("dstLo", 0.0f);
	remapFilter->SetParam("dstHi", 1.0f);
	m_filters.emplace_back(FilterType::kFilterRemapRange, std::move(remapFilter));
#endif
}

void HdRprApiDepthAov::Update(HdRprApi const* rprApi, rif::Context* rifContext) {
	auto viewProjectionMatrix = rprApi->GetCameraViewMatrix() * rprApi->GetCameraProjectionMatrix();
	FindFilter(FilterType::kFilterNdcDepth)->SetParam("viewProjMatrix", GfMatrix4f(viewProjectionMatrix.GetTranspose()));

	HdRprApiAov::Update(rprApi, rifContext);
}

HdRprApiFramebuffer* HdRprApiDepthAov::GetRifInputFramebuffer()
{
	return m_retainedWorldCoordinateAov->GetResolvedFb();
}

HdRprApiIdMaskAov::HdRprApiIdMaskAov(
    HdRprAovDescriptor const& aovDescriptor, std::shared_ptr<HdRprApiAov> const& baseIdAov,
    int width, int height, HdFormat format,
    rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, rif::Context* rifContext, float renderResolution)
    : HdRprApiComputedAov(aovDescriptor, width, height, format, renderResolution, rifContext)
    , m_baseIdAov(baseIdAov) {
    if (!rifContext) {
        RPR_THROW_ERROR_MSG("Can not create id mask AOV: RIF context required");
    }

    auto filter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_USER_DEFINED, rifContext);
    auto colorizeIdKernelCode = std::string(R"(
        int2 coord;
        GET_COORD_OR_RETURN(coord, GET_BUFFER_SIZE(inputImage));
        vec3 idEncoded = ReadPixelTyped(inputImage, coord.x, coord.y).xyz;
        unsigned int idDecoded = (unsigned int)(idEncoded.x*256) + (unsigned int)(idEncoded.y*256*256) + (unsigned int)(idEncoded.z*256*256*256);

        vec4 color;
        if (idDecoded) {
            unsigned int v0 = 0x123;
            unsigned int v1 = idDecoded;
            unsigned int s0 = 0;
            const unsigned int N = 4;
            for( unsigned int n = 0; n < N; n++ ) {
                s0 += 0x9e3779b9;
                v0 += ((v1<<4)+0xa341316c)^(v1+s0)^((v1>>5)+0xc8013ea4);
                v1 += ((v0<<4)+0xad90777d)^(v0+s0)^((v0>>5)+0x7e95761e);
            }
            color = make_vec4(v0&0xFFFF, v0>>16, v1&0xFFFF, 0xFFFF)/(float)(0xFFFF);
        } else {
            color = make_vec4(0.0f, 0.0f, 0.0f, 0.0f);
        }

        WritePixelTyped(outputImage, coord.x, coord.y, color);
    )");
    filter->SetParam("code", colorizeIdKernelCode);

	m_filters.emplace_back(FilterType::kFilterUserDefined, std::move(filter));
}

HdRprApiFramebuffer* HdRprApiIdMaskAov::GetRifInputFramebuffer()
{
	return m_baseIdAov->GetResolvedFb();
}

PXR_NAMESPACE_CLOSE_SCOPE
