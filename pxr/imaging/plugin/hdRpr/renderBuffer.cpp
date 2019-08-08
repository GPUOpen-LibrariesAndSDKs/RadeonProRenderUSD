#include "renderBuffer.h"

#include "pxr/imaging/hd/sceneDelegate.h"
#include "tokens.h"

PXR_NAMESPACE_OPEN_SCOPE


namespace {
	//XXX: we need count renderbuffers to determine	when all renderbuffers are deleted to be able set NONE aov
	static size_t g_RenderbuferCounter = 0;
}

HdRprRenderBuffer::HdRprRenderBuffer(SdfPath const & id, HdRprApiSharedPtr rprApi) : HdRenderBuffer(id)
{
    if (!rprApi)
    {
        TF_CODING_ERROR("RprApi is expired");
        return;
    }
    
    m_rprApiWeakPrt = rprApi;
    
    const std::string aovName = id.GetName();
    
    if(aovName == "aov_normal")
    {
        rprApi->SetAov(HdRprAov::NORMAL);
    }
    else if(aovName == "aov_depth")
    {
        rprApi->SetAov(HdRprAov::DEPTH);
    }
    else if(aovName == "aov_primId")
    {
        rprApi->SetAov(HdRprAov::PRIM_ID);
    }
    else if(aovName == "aov_primvars_st")
    {
        rprApi->SetAov(HdRprAov::UV);
    }

	g_RenderbuferCounter++;
}

void HdRprRenderBuffer::Sync(HdSceneDelegate *sceneDelegate,
	HdRenderParam   *renderParam,
	HdDirtyBits     *dirtyBits)
{
	*dirtyBits = Clean;
}

HdDirtyBits HdRprRenderBuffer::GetInitialDirtyBitsMask() const
{
	return AllDirty;
}

bool HdRprRenderBuffer::Allocate(GfVec3i const& dimensions,
                      HdFormat format,
                      bool multiSampled) {
    return true;
}

unsigned int HdRprRenderBuffer::GetWidth() const
{
    return 0u;
}
unsigned int HdRprRenderBuffer::GetHeight() const
{
    return 0u;
}

unsigned int HdRprRenderBuffer::GetDepth() const
{
    return 0u;
}

HdFormat HdRprRenderBuffer::GetFormat() const{
    return HdFormat::HdFormatFloat32;
}

bool HdRprRenderBuffer::IsMultiSampled() const {return false;}

uint8_t* HdRprRenderBuffer::Map(){return nullptr;}

void HdRprRenderBuffer::Unmap(){}

bool HdRprRenderBuffer::IsMapped() const {return false;}

void HdRprRenderBuffer::Resolve() {}

bool HdRprRenderBuffer::IsConverged() const {return false;}

void HdRprRenderBuffer::_Deallocate() {
	if (0 == --g_RenderbuferCounter)
	{
		HdRprApiSharedPtr rprApi = m_rprApiWeakPrt.lock();
		rprApi->SetAov(HdRprAov::NONE);
	}
}

PXR_NAMESPACE_CLOSE_SCOPE
