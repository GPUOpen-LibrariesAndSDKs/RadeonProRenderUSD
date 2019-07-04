#pragma once

#include "RadeonProRender.h"
#include "RadeonImageFilters.h"

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>

#include "pxr/pxr.h"
#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE


enum RifFilterInput
{
	RifColor,
	RifNormal,
	RifDepth,
	RifWorldCoordinate,
	RifObjectId,
	RifTrans,
	RifAlbedo,
	RifMaxInput
};

enum class RifParamType
{
	RifInt,
	RifFloat
};

union RifData
{
	rif_int   i;
	rif_float f;
};

struct RifParam
{
	RifParamType mType;
	RifData      mData;
};

class RifContextWrapper;
class RifFilterWrapper;

class ImageFilter final
{
	std::unique_ptr<RifContextWrapper> mRifContext;

	std::unique_ptr<RifFilterWrapper> mRifFilter;

	std::uint32_t mWidth;
	std::uint32_t mHeight;
	
	bool mIsCPUMode;

public:
	explicit ImageFilter(const rpr_context rprContext, std::uint32_t width, std::uint32_t height);
	~ImageFilter();

	void CreateFilter();
	void DeleteFilter();

	void Resize(std::uint32_t w, std::uint32_t h);

	void SetInput(RifFilterInput inputId, const rpr_framebuffer rprFrameBuffer, const float sigma) const;

	void SetOutput(const rpr_framebuffer rprFrameBuffer) const;

	void SetOutputGlTexture(const unsigned int glTexture) const;

	void AddParam(std::string name, RifParam param) const;

	void AttachFilter() const;

	void Run() const;

	std::vector<float> GetData() const;
};



class RifContextWrapper
{
protected:
	rif_context mRifContextHandle = nullptr;
	rif_command_queue mRifCommandQueueHandle = nullptr;
	rif_image mOutputRifImage = nullptr;

public:
	virtual ~RifContextWrapper();

	const rif_context Context() const;
	const rif_command_queue Queue() const;
	const rif_image Output() const;

	void SetOutput(const rif_image& desc);

	virtual rif_image CreateRifImage(const rpr_framebuffer rprFrameBuffer, const rif_image_desc& desc) const = 0;

	virtual rif_image CreateRifImageGl(const unsigned int glTexture) const { return NULL; };

	virtual void UpdateInputs(const RifFilterWrapper* rifFilter) const = 0;

protected:
	virtual std::vector<rpr_char> GetRprCachePath(rpr_context rprContext) const final;
};

class RifContextGPU final : public RifContextWrapper
{
	const rif_backend_api_type rifBackendApiType = RIF_BACKEND_API_OPENCL;

public:
	explicit RifContextGPU(const rpr_context rprContext);
	virtual ~RifContextGPU();

	virtual rif_image CreateRifImage(const rpr_framebuffer rprFrameBuffer, const rif_image_desc& desc) const override;
	virtual rif_image CreateRifImageGl(const unsigned int glTexture) const override;
	virtual void UpdateInputs(const RifFilterWrapper* rifFilter) const override;
};

class RifContextGPUMetal final : public RifContextWrapper
{
	const rif_backend_api_type rifBackendApiType = RIF_BACKEND_API_METAL;
    
public:
	explicit RifContextGPUMetal(const rpr_context rprContext);
	virtual ~RifContextGPUMetal();
    
	virtual rif_image CreateRifImage(const rpr_framebuffer rprFrameBuffer, const rif_image_desc& desc) const override;
	virtual void UpdateInputs(const RifFilterWrapper* rifFilter) const override;
};

class RifContextCPU final : public RifContextWrapper
{
	const rif_backend_api_type rifBackendApiType = RIF_BACKEND_API_OPENCL;

public:
	explicit RifContextCPU(const rpr_context rprContext);
	virtual ~RifContextCPU();

	virtual rif_image CreateRifImage(const rpr_framebuffer rprFrameBuffer, const rif_image_desc& desc) const override;
	virtual void UpdateInputs(const RifFilterWrapper* rifFilter) const override;
};



class RifFilterWrapper
{
	friend class RifContextWrapper;
	friend class RifContextCPU;

protected:
	rif_image_filter mRifImageFilterHandle = nullptr;

	std::vector<rif_image_filter> mAuxFilters;
	std::vector<rif_image> mAuxImages;

	struct InputTraits
	{
		rif_image       mRifImage;
		rpr_framebuffer mRprFrameBuffer;
		float           mSigma;
	};

	std::unordered_map<RifFilterInput, InputTraits, std::hash<std::underlying_type<RifFilterInput>::type>> mInputs;
	std::unordered_map<std::string, RifParam> mParams;

public:
	virtual ~RifFilterWrapper();

	void AddInput(RifFilterInput inputId, const rif_image rifImage, const rpr_framebuffer rprFrameBuffer, float sigma);
	void AddParam(std::string name, RifParam param);

	virtual void AttachFilter(const RifContextWrapper* rifContext) = 0;
	virtual void DetachFilter(const RifContextWrapper* rifContext) noexcept final;

	void ApplyParameters() const;

protected:
	void SetupVarianceImageFilter(const rif_image_filter inputFilter, const rif_image outVarianceImage) const;
};

class RifFilterAIDenoise final : public RifFilterWrapper
{
	enum
	{
		RemapNormalFilter,
		RemapDepthFilter,
		AuxFilterMax
	};
public:
	explicit RifFilterAIDenoise(const RifContextWrapper* rifContext, bool isCPUMode);
	~RifFilterAIDenoise() override = default;
	
	void AttachFilter(const RifContextWrapper* rifContext) override;
};

PXR_NAMESPACE_CLOSE_SCOPE
