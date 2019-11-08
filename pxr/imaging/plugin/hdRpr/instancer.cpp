#include "instancer.h"

#include "pxr/base/gf/quatd.h"
#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (instanceTransform)
    (rotate)
    (scale)
    (translate)
);

void HdRprInstancer::Sync() {
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    auto& instancerId = GetId();
    auto& changeTracker = GetDelegate()->GetRenderIndex().GetChangeTracker();

    // Use the double-checked locking pattern to check if this instancer's
    // primvars are dirty.
    int dirtyBits = changeTracker.GetInstancerDirtyBits(instancerId);
    if (!HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, instancerId)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_syncMutex);
    dirtyBits = changeTracker.GetInstancerDirtyBits(instancerId);
    if (!HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, instancerId)) {
        return;
    }

    auto primvarDescs = GetDelegate()->GetPrimvarDescriptors(instancerId, HdInterpolationInstance);
    for (auto& desc : primvarDescs) {
        if (!HdChangeTracker::IsPrimvarDirty(dirtyBits, instancerId, desc.name)) {
            continue;
        }

        VtValue value = GetDelegate()->Get(instancerId, desc.name);
        if (value.IsEmpty()) {
            continue;
        }

        if (desc.name == _tokens->translate) {
            if (value.IsHolding<VtVec3fArray>()) {
                m_translate = value.UncheckedGet<VtVec3fArray>();
            }
        } else if (desc.name == _tokens->rotate) {
            if (value.IsHolding<VtVec4fArray>()) {
                m_rotate = value.UncheckedGet<VtVec4fArray>();
            }
        } else if (desc.name == _tokens->scale) {
            if (value.IsHolding<VtVec3fArray>()) {
                m_scale = value.UncheckedGet<VtVec3fArray>();
            }
        } else if (desc.name == _tokens->instanceTransform) {
            if (value.IsHolding<VtMatrix4dArray>()) {
                m_transform = value.UncheckedGet<VtMatrix4dArray>();
            }
        }
    }

    // Mark the instancer as clean
    changeTracker.MarkInstancerClean(instancerId);
}

VtMatrix4dArray HdRprInstancer::ComputeTransforms(SdfPath const& prototypeId) {
    Sync();

    GfMatrix4d instancerTransform = GetDelegate()->GetInstancerTransform(GetId());
    VtIntArray instanceIndices = GetDelegate()->GetInstanceIndices(GetId(), prototypeId);

    VtMatrix4dArray transforms;
    transforms.reserve(instanceIndices.size());
    for (int idx : instanceIndices) {
        GfMatrix4d translateMat(1);
        GfMatrix4d rotateMat(1);
        GfMatrix4d scaleMat(1);
        GfMatrix4d transform(1);

        if (!m_translate.empty()) {
            translateMat.SetTranslate(GfVec3d(m_translate.cdata()[idx]));
        }

        if (!m_rotate.empty()) {
            auto& v = m_rotate.cdata()[idx];
            rotateMat.SetRotate(GfQuatd(v[0], GfVec3d(v[1], v[2], v[3])));
        }

        if (!m_scale.empty()) {
            scaleMat.SetScale(GfVec3d(m_scale.cdata()[idx]));
        }

        if (!m_transform.empty()) {
            transform = m_transform.cdata()[idx];
        }

        transforms.push_back(transform * scaleMat * rotateMat * translateMat * instancerTransform);
    }

    auto parentInstancer = static_cast<HdRprInstancer*>(GetDelegate()->GetRenderIndex().GetInstancer(GetParentId()));
    if (!parentInstancer) {
        return transforms;
    }

    VtMatrix4dArray wordTransform;
    for (const GfMatrix4d& parentTransform : parentInstancer->ComputeTransforms(GetId())) {
        for (const GfMatrix4d& localTransform : transforms) {
            wordTransform.push_back(parentTransform * localTransform);
        }
    }

    return wordTransform;
}

PXR_NAMESPACE_CLOSE_SCOPE
