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

#include "instancer.h"

#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

// TODO: Use HdInstancerTokens when Houdini updates USD to 20.02
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

// Helper to accumulate sample times from the largest set of
// samples seen, up to maxNumSamples.
template <typename T1, typename T2, unsigned int C>
static void AccumulateSampleTimes(HdTimeSampleArray<T1, C> const& in, HdTimeSampleArray<T2, C> *out) {
    if (in.count > out->count) {
        out->Resize(in.count);
        out->times = in.times;
    }
}

HdTimeSampleArray<VtMatrix4dArray, 2> HdRprInstancer::SampleInstanceTransforms(SdfPath const& prototypeId) {
    HdSceneDelegate *delegate = GetDelegate();
    const SdfPath &instancerId = GetId();

    VtIntArray instanceIndices = delegate->GetInstanceIndices(instancerId, prototypeId);

    HdTimeSampleArray<GfMatrix4d, 2> instancerXform;
    HdTimeSampleArray<VtValue, 2> boxedInstanceXforms;
    HdTimeSampleArray<VtValue, 2> boxedTranslates;
    HdTimeSampleArray<VtValue, 2> boxedRotates;
    HdTimeSampleArray<VtValue, 2> boxedScales;
    delegate->SampleInstancerTransform(instancerId, &instancerXform);
    delegate->SamplePrimvar(instancerId, _tokens->instanceTransform, &boxedInstanceXforms);
    delegate->SamplePrimvar(instancerId, _tokens->translate, &boxedTranslates);
    delegate->SamplePrimvar(instancerId, _tokens->scale, &boxedScales);
    delegate->SamplePrimvar(instancerId, _tokens->rotate, &boxedRotates);

    HdTimeSampleArray<VtMatrix4dArray, 2> instanceXforms;
    HdTimeSampleArray<VtVec3fArray, 2> translates;
    HdTimeSampleArray<VtQuatdArray, 2> rotates;
    HdTimeSampleArray<VtVec3fArray, 2> scales;
    instanceXforms.UnboxFrom(boxedInstanceXforms);
    translates.UnboxFrom(boxedTranslates);
    rotates.UnboxFrom(boxedRotates);
    scales.UnboxFrom(boxedScales);

    // As a simple resampling strategy, find the input with the max #
    // of samples and use its sample placement.  In practice we expect
    // them to all be the same, i.e. to not require resampling.
    HdTimeSampleArray<VtMatrix4dArray, 2> sa;
    sa.Resize(0);
    AccumulateSampleTimes(instancerXform, &sa);
    AccumulateSampleTimes(instanceXforms, &sa);
    AccumulateSampleTimes(translates, &sa);
    AccumulateSampleTimes(scales, &sa);
    AccumulateSampleTimes(rotates, &sa);

    // Resample inputs and concatenate transformations.
    for (size_t i = 0; i < sa.count; ++i) {
        const float t = sa.times[i];
        GfMatrix4d xf(1);
        if (instancerXform.count > 0) {
            xf = instancerXform.Resample(t);
        }
        VtMatrix4dArray ixf;
        if (instanceXforms.count > 0) {
            ixf = instanceXforms.Resample(t);
        }
        VtVec3fArray trans;
        if (translates.count > 0) {
            trans = translates.Resample(t);
        }
        VtQuatdArray rot;
        if (rotates.count > 0) {
            rot = rotates.Resample(t);
        }
        VtVec3fArray scale;
        if (scales.count > 0) {
            scale = scales.Resample(t);
        }

        // Concatenate transformations and filter to just the instanceIndices.
        VtMatrix4dArray &ma = sa.values[i];
        ma.resize(instanceIndices.size());
        for (size_t j = 0; j < instanceIndices.size(); ++j) {
            ma[j] = xf;
            size_t instanceIndex = instanceIndices[j];
            if (trans.size() > instanceIndex) {
                GfMatrix4d t(1);
                t.SetTranslate(GfVec3d(trans[instanceIndex]));
                ma[j] = t * ma[j];
            }
            if (rot.size() > instanceIndex) {
                GfMatrix4d r(1);
                r.SetRotate(GfRotation(rot[instanceIndex]));
                ma[j] = r * ma[j];
            }
            if (scale.size() > instanceIndex) {
                GfMatrix4d s(1);
                s.SetScale(GfVec3d(scale[instanceIndex]));
                ma[j] = s * ma[j];
            }
            if (ixf.size() > instanceIndex) {
                ma[j] = ixf[instanceIndex] * ma[j];
            }
        }
    }

    // If there is a parent instancer, continue to unroll
    // the child instances across the parent; otherwise we're done.
    if (GetParentId().IsEmpty()) {
        return sa;
    }

    HdInstancer *parentInstancer = GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
    if (!TF_VERIFY(parentInstancer)) {
        return sa;
    }
    auto rprParentInstancer = static_cast<HdRprInstancer*>(parentInstancer);

    // Multiply the instance samples against the parent instancer samples.
    auto parentXf = rprParentInstancer->SampleInstanceTransforms(GetId());
    if (parentXf.count == 0 || parentXf.values[0].empty()) {
        // No samples for parent instancer.
        return sa;
    }
    // Move aside previously computed child xform samples to childXf.
    HdTimeSampleArray<VtMatrix4dArray, 2> childXf(sa);
    // Merge sample times, taking the densest sampling.
    AccumulateSampleTimes(parentXf, &sa);
    // Apply parent xforms to the children.
    for (size_t i = 0; i < sa.count; ++i) {
        const float t = sa.times[i];
        // Resample transforms at the same time.
        VtMatrix4dArray curParentXf = parentXf.Resample(t);
        VtMatrix4dArray curChildXf = childXf.Resample(t);
        // Multiply out each combination.
        VtMatrix4dArray &result = sa.values[i];
        result.resize(curParentXf.size() * curChildXf.size());
        for (size_t j = 0; j < curParentXf.size(); ++j) {
            for (size_t k = 0; k < curChildXf.size(); ++k) {
                result[j * curChildXf.size() + k] =
                    curChildXf[k] * curParentXf[j];
            }
        }
    }

    return sa;
}

PXR_NAMESPACE_CLOSE_SCOPE
