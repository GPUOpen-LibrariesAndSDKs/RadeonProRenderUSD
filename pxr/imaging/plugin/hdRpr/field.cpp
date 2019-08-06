
#include "field.h"

PXR_NAMESPACE_OPEN_SCOPE


HdRprField::HdRprField(SdfPath const& id, HdRprApiSharedPtr rprApi) : HdField(id)
{
	m_rprApiWeakPtr = rprApi;
}

HdRprField::~HdRprField()
{
}

void HdRprField::Sync(HdSceneDelegate *sceneDelegate,
	HdRenderParam   *renderParam,
	HdDirtyBits     *dirtyBits)
{


}

HdDirtyBits
HdRprField::GetInitialDirtyBitsMask() const
{

	return (HdDirtyBits)0;
}

PXR_NAMESPACE_CLOSE_SCOPE
