#include "field.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprField::HdRprField(SdfPath const& id, HdRprApiSharedPtr rprApi) : HdField(id) {
    m_rprApiWeakPtr = rprApi;
}

void HdRprField::Sync(HdSceneDelegate* sceneDelegate,
                      HdRenderParam* renderParam,
                      HdDirtyBits* dirtyBits) {

}

HdDirtyBits HdRprField::GetInitialDirtyBitsMask() const {
    return DirtyBits::Clean;
}

PXR_NAMESPACE_CLOSE_SCOPE
