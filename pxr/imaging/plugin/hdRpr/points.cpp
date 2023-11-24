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

#include "points.h"
#include "rprApi.h"
#include "primvarUtil.h"
#include "renderParam.h"
#include "material.h"
#include "instancer.h"

#include "pxr/imaging/rprUsd/debugCodes.h"
#include "pxr/imaging/hd/extComputationUtils.h"
#include "pxr/usdImaging/usdImaging/implicitSurfaceMeshUtils.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprPoints::HdRprPoints(SdfPath const& id HDRPR_INSTANCER_ID_ARG_DECL)
    : HdRprBaseRprim(id HDRPR_INSTANCER_ID_ARG)
    , m_visibilityMask(kVisibleAll)
    , m_subdivisionLevel(0)
    , m_subdivisionCreaseWeight(0.0) {

}

void HdRprPoints::Sync(
    HdSceneDelegate* sceneDelegate,
    HdRenderParam* renderParam,
    HdDirtyBits* dirtyBits,
    TfToken const& reprSelector) {

    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    std::map<HdInterpolation, HdPrimvarDescriptorVector> primvarDescsPerInterpolation;
    SdfPath const& id = GetId();

    bool dirtyPoints = false;
    bool isPointsComputed = false;
    auto extComputationDescs = sceneDelegate->GetExtComputationPrimvarDescriptors(id, HdInterpolationVertex);
    for (auto& desc : extComputationDescs) {
        if (desc.name != HdTokens->points) {
            continue;
        }

        if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, desc.name)) {
            auto valueStore = HdExtComputationUtils::GetComputedPrimvarValues({desc}, sceneDelegate);
            auto pointValueIt = valueStore.find(desc.name);
            if (pointValueIt != valueStore.end()) {
                m_points = pointValueIt->second.Get<VtVec3fArray>();
                isPointsComputed = true;
                dirtyPoints = true;
            }
        }

        break;
    }

    if (!isPointsComputed &&
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        VtValue pointsValue = sceneDelegate->Get(id, HdTokens->points);
        m_points = pointsValue.Get<VtVec3fArray>();
        dirtyPoints = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyWidths) {
        HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, id, &primvarDescsPerInterpolation);
        if (HdRprIsPrimvarExists(HdTokens->widths, primvarDescsPerInterpolation, &m_widthsInterpolation)) {
            m_widths = sceneDelegate->Get(id, HdTokens->widths).Get<VtFloatArray>();
        } else {
            m_widths = VtFloatArray(1, 1.0f);
            m_widthsInterpolation = HdInterpolationConstant;
            TF_WARN("[%s] Points does not have widths. Fallback value is 1.0f with a constant interpolation", id.GetText());
        }
    }

    // By some undefined reason *dirtyBits & HdChangeTracker::DirtyMaterialId does not work for points. Then we need to track changes ourself
    auto oldMaterialId = m_materialId;
    UpdateMaterialId(sceneDelegate, rprRenderParam);
    bool dirtyMaterialOverride = oldMaterialId != m_materialId;
    bool materialOverrideExists = !m_materialId.IsEmpty();

    bool dirtyDisplayColors = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->displayColor);
    if (dirtyDisplayColors) {
        HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, id, &primvarDescsPerInterpolation);
        if (HdRprIsPrimvarExists(HdTokens->displayColor, primvarDescsPerInterpolation, &m_colorsInterpolation)) {
            m_colors = sceneDelegate->Get(GetId(), HdTokens->displayColor).Get<VtVec3fArray>();
        } else {
            m_colors = VtVec3fArray(1, GfVec3f(1, 0, 1));
            m_colorsInterpolation = HdInterpolationConstant;
            TF_WARN("[%s] Points does not have display colors. Fallback value is pink color with a constant interpolation", id.GetText());
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        _sharedData.visible = sceneDelegate->GetVisible(id);
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        m_transform = GfMatrix4f(sceneDelegate->GetTransform(id));
    }

    bool dirtySubdivisionLevel = false;
    bool dirtyVisibilityMask = false;
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        HdRprGeometrySettings geomSettings;
        geomSettings.visibilityMask = kVisibleAll;
        HdRprFillPrimvarDescsPerInterpolation(sceneDelegate, id, &primvarDescsPerInterpolation);
        HdRprParseGeometrySettings(sceneDelegate, id, primvarDescsPerInterpolation, &geomSettings);

        if (m_subdivisionLevel != geomSettings.subdivisionLevel) {
            m_subdivisionLevel = geomSettings.subdivisionLevel;
            dirtySubdivisionLevel = true;
        }

        if (m_subdivisionCreaseWeight != geomSettings.subdivisionCreaseWeight) {
            m_subdivisionCreaseWeight = geomSettings.subdivisionCreaseWeight;
            dirtySubdivisionLevel = true;
        }

        if (m_visibilityMask != geomSettings.visibilityMask) {
            m_visibilityMask = geomSettings.visibilityMask;
            dirtyVisibilityMask = true;
        }
    }

    if (dirtyDisplayColors) {
        if (m_material) {
            rprApi->Release(m_material);
            m_material = nullptr;
        }

        if (m_colorsInterpolation == HdInterpolationVertex) {
            m_material = rprApi->CreatePointsMaterial(m_colors);
        } else if (!m_colors.empty()) {
            m_material = rprApi->CreateDiffuseMaterial(m_colors[0]);
        }

        if (m_material && RprUsdIsLeakCheckEnabled()) {
            rprApi->SetName(m_material, id.GetText());
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyInstancer){
        m_instanceTransforms.clear();
#ifdef USE_DECOUPLED_INSTANCER
        _UpdateInstancer(sceneDelegate, dirtyBits);
        HdInstancer::_SyncInstancerAndParents(sceneDelegate->GetRenderIndex(), sceneDelegate->GetInstancerId(id));
#endif
        auto instancer = static_cast<HdRprInstancer*>(sceneDelegate->GetRenderIndex().GetInstancer(sceneDelegate->GetInstancerId(id)));
        if (instancer) {
            auto instanceTransforms = instancer->SampleInstanceTransforms(id);
            auto newNumInstances = (instanceTransforms.count > 0) ? instanceTransforms.values[0].size() : 0;
            if(newNumInstances > 0){
                m_instanceTransforms.reserve(newNumInstances);
                for (auto t : instanceTransforms.values[0]){
                    m_instanceTransforms.emplace_back(GfMatrix4f(t));
                }
            }
        }
        if(m_instanceTransforms.empty()){
            m_instanceTransforms.emplace_back(1.0);
        }
    }

    size_t numInstances = m_points.size() * m_instanceTransforms.size();

    bool dirtyPrototypeMesh = false;
    bool dirtyInstances = false;
    if (m_instances.size() != numInstances) {
        if (m_points.empty()) {
            rprApi->Release(m_prototypeMesh);
            m_prototypeMesh = nullptr;
        } else if (!m_prototypeMesh){
            auto& topology = UsdImagingGetUnitSphereMeshTopology();
            auto& points = UsdImagingGetUnitSphereMeshPoints();

            m_prototypeMesh = rprApi->CreateMesh(points, topology.GetFaceVertexIndices(), points, topology.GetFaceVertexIndices(), VtVec2fArray(), VtIntArray(), topology.GetFaceVertexCounts(), topology.GetOrientation());
            rprApi->SetMeshVisibility(m_prototypeMesh, kInvisible);
            rprApi->SetMeshRefineLevel(m_prototypeMesh, m_subdivisionLevel, m_subdivisionCreaseWeight);

            dirtyPrototypeMesh = true;

            if (RprUsdIsLeakCheckEnabled()) {
                rprApi->SetName(m_prototypeMesh, id.GetText());
            }
        }

        if (m_instances.size() > numInstances) {
            for (size_t i = numInstances; i < m_instances.size(); ++i) {
                rprApi->Release(m_instances[i]);
            }
            m_instances.resize(numInstances);
        } else {
            m_instances.reserve(numInstances);
            for (size_t i = m_instances.size(); i < numInstances; ++i) {
                if (auto instance = rprApi->CreateMeshInstance(m_prototypeMesh)) {
                    rprApi->SetMeshId(instance, i % m_points.size());
                    m_instances.push_back(instance);

                    if (RprUsdIsLeakCheckEnabled()) {
                        rprApi->SetName(instance, id.GetText());
                    }
                }
            }

            dirtyInstances = true;
        }
        m_instances.shrink_to_fit();
    }

    if (!m_instances.empty()) {
        if (dirtySubdivisionLevel && !dirtyPrototypeMesh) {
            rprApi->SetMeshRefineLevel(m_prototypeMesh, m_subdivisionLevel, m_subdivisionCreaseWeight);
        }

        if ((*dirtyBits & HdChangeTracker::DirtyTransform) ||
            (*dirtyBits & HdChangeTracker::DirtyWidths) ||
            (*dirtyBits & HdChangeTracker::DirtyInstancer) ||
            dirtyPoints || dirtyInstances) {

            std::function<float(size_t)> sampleWidth;
            if (m_widthsInterpolation == HdInterpolationVertex) {
                sampleWidth = [this](size_t idx) { return m_widths[idx]; };
            } else if (m_widthsInterpolation == HdInterpolationConstant) {
                sampleWidth = [this](size_t) { return m_widths[0]; };
            } else {
                sampleWidth = [](size_t) { return 1.0f; };
                TF_WARN("[%s] Unsupported widths interpolation. Fallback value is 1.0f with a constant interpolation", id.GetText());
            }

            for (int i = 0; i < m_instanceTransforms.size(); ++i){
                for (size_t j = 0; j < m_points.size(); ++j) {
                    auto& position = m_points[j];
                    auto width = sampleWidth(j);
                    auto transform = GfMatrix4f(1.0f).SetScale(GfVec3f(width)).SetTranslateOnly(position);
                    transform *= m_transform * m_instanceTransforms[i];

                    rprApi->SetTransform(m_instances[j + m_points.size() * i], transform);
                }
            }
        }

        if (!materialOverrideExists && (dirtyDisplayColors || dirtyInstances)) {
            for (size_t i = 0; i < m_instances.size(); ++i) {
                rprApi->SetMeshMaterial(m_instances[i], m_material, false);
            }
        }
        else if (materialOverrideExists && (dirtyMaterialOverride || dirtyInstances)) {
            auto material = static_cast<const HdRprMaterial*>(
                sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, m_materialId));

            if (material && material->GetRprMaterialObject()) {
                for (size_t i = 0; i < m_instances.size(); ++i) {
                    rprApi->SetMeshMaterial(m_instances[i], material->GetRprMaterialObject(), false);
                }
            }
        }

        if (!_sharedData.visible) {
            // if primitive is fully invisible then visibility mask has no effect
            dirtyVisibilityMask = false;
        }
        if ((*dirtyBits & HdChangeTracker::DirtyVisibility) ||
            dirtyVisibilityMask || dirtyInstances) {
            auto visibilityMask = _sharedData.visible ? m_visibilityMask : kInvisible;
            for (size_t i = 0; i < m_instances.size(); ++i) {
                rprApi->SetMeshVisibility(m_instances[i], visibilityMask);
            }
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void HdRprPoints::Finalize(HdRenderParam* renderParam) {
    auto rprApi = static_cast<HdRprRenderParam*>(renderParam)->AcquireRprApiForEdit();

    rprApi->Release(m_prototypeMesh);
    m_prototypeMesh = nullptr;

    for (auto instance : m_instances) {
        rprApi->Release(instance);
    }
    m_instances.clear();

    rprApi->Release(m_material);
    m_material = nullptr;
 
    HdPoints::Finalize(renderParam);
}

HdDirtyBits HdRprPoints::GetInitialDirtyBitsMask() const {
    return HdChangeTracker::Clean |
        HdChangeTracker::DirtyPoints |
        HdChangeTracker::DirtyWidths |
        HdChangeTracker::DirtyTransform |
        HdChangeTracker::DirtyPrimvar |
        HdChangeTracker::DirtyVisibility |
        HdChangeTracker::DirtyInstancer;
}

HdDirtyBits HdRprPoints::_PropagateDirtyBits(HdDirtyBits bits) const {
    return bits;
}

void HdRprPoints::_InitRepr(
    TfToken const& reprName,
    HdDirtyBits* dirtyBits) {
    TF_UNUSED(reprName);
    TF_UNUSED(dirtyBits);

    // No-op
}

PXR_NAMESPACE_CLOSE_SCOPE
