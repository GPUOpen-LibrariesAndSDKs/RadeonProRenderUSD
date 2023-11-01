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

#include <json.hpp>
using json = nlohmann::json;

#include "rprApi.h"
#include "rprApiAov.h"
#include "aovDescriptor.h"

#include "rifcpp/rifFilter.h"
#include "rifcpp/rifImage.h"
#include "rifcpp/rifError.h"

#include "config.h"
#include "camera.h"
#include "debugCodes.h"
#include "renderDelegate.h"
#include "renderBuffer.h"
#include "renderParam.h"

#include "pxr/imaging/rprUsd/util.h"
#include "pxr/imaging/rprUsd/config.h"
#include "pxr/imaging/rprUsd/error.h"
#include "pxr/imaging/rprUsd/helpers.h"
#include "pxr/imaging/rprUsd/coreImage.h"
#include "pxr/imaging/rprUsd/imageCache.h"
#include "pxr/imaging/rprUsd/material.h"
#include "pxr/imaging/rprUsd/materialRegistry.h"
#include "pxr/imaging/rprUsd/contextMetadata.h"
#include "pxr/imaging/rprUsd/contextHelpers.h"

#include "pxr/base/gf/math.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/thisPlugin.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/usd/usdRender/tokens.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/getenv.h"
#include "pxr/base/work/loops.h"
#include "pxr/base/arch/env.h"
#include "pxr/base/tf/envSetting.h"

#include "notify/message.h"

#include <RadeonProRender_MaterialX.h>
#include <RadeonProRender_Baikal.h>
#ifdef RPR_LOADSTORE_AVAILABLE
#include <RprLoadStore.h>
#endif // RPR_LOADSTORE_AVAILABLE

#ifdef RPR_EXR_EXPORT_ENABLED
#include <MurmurHash3.h>
#include <ImfOutputFile.h>
#include <ImfChannelList.h>
#include <ImfStringAttribute.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#endif // RPR_EXR_EXPORT_ENABLED

#ifdef BUILD_AS_HOUDINI_PLUGIN
#include <HOM/HOM_Module.h>
#endif // BUILD_AS_HOUDINI_PLUGIN

#include <fstream>
#include <chrono>
#include <vector>
#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(HDRPR_RENDER_QUALITY_OVERRIDE, "",
    "Set this to override render quality coming from the render settings");

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (usdFilename)
);

namespace {

std::string const& GetPath(SdfAssetPath const& path) {
    if (!path.GetResolvedPath().empty()) {
        return path.GetResolvedPath();
    } else {
        return path.GetAssetPath();
    }
}

TfToken GetRenderQuality(HdRprConfig const& config) {
    std::string renderQualityOverride = TfGetEnvSetting(HDRPR_RENDER_QUALITY_OVERRIDE);
    std::string renderQualityUsdviewEnvSetting = ArchGetEnv("HDRPR_USDVIEW_RENDER_QUALITY");

    auto& tokens = HdRprCoreRenderQualityTokens->allTokens;
    if (std::find(tokens.begin(), tokens.end(), renderQualityOverride) != tokens.end()) {
        return TfToken(renderQualityOverride);
    }
    
    if (std::find(tokens.begin(), tokens.end(), renderQualityUsdviewEnvSetting) != tokens.end()) {
        return TfToken(renderQualityUsdviewEnvSetting);
    }

    return config.GetCoreRenderQuality();
}

using LockGuard = std::lock_guard<std::mutex>;

template <typename T>
struct RenderSetting {
    T value;
    bool isDirty;
};

GfVec4f ToVec4(GfVec3f const& vec, float w) {
    return GfVec4f(vec[0], vec[1], vec[2], w);
}

bool CreateIntermediateDirectories(std::string const& filePath) {
    auto dir = TfGetPathName(filePath);
    if (!dir.empty()) {
        return TfMakeDirs(dir, -1, true);
    }
    return true;
}

template <typename T>
void UnalignedRead(void const* src, size_t* offset, T* dst) {
    std::memcpy(dst, (uint8_t const*)src + *offset, sizeof(T));
    *offset += sizeof(T);
}

GfVec4f ColorizeId(uint32_t id) {
    return {
        (float)(id & 0xFF) / 0xFF,
        (float)((id & 0xFF00) >> 8) / 0xFF,
        (float)((id & 0xFF0000) >> 16) / 0xFF,
        1.0f
    };
}

template <typename T>
std::unique_ptr<T[]> MergeSamples(VtArray<VtArray<T>>* samples, size_t requiredNumSamples, rpr_float const** rprData, size_t* rprDataSize) {
    if (samples->empty()) {
        *rprData = nullptr;
        *rprDataSize = 0;
        return nullptr;
    }

    if (samples->size() != requiredNumSamples) {
        samples->resize(requiredNumSamples, [backElem=samples->back()](auto begin, auto end) {
            for (auto it = begin; it != end; ++it) {
                *it = backElem;
            }
        });
    }

    const size_t numSampleValues = samples->cdata()[0].size();
    auto mergedSamples = std::make_unique<T[]>(requiredNumSamples * numSampleValues);

    for (size_t i = 0; i < requiredNumSamples; ++i) {
        size_t offset = i * numSampleValues;
        VtArray<T> const& values = samples->cdata()[i];
        std::copy(values.cbegin(), values.cend(), mergedSamples.get() + offset);

        if (values.size() != numSampleValues) {
            TF_RUNTIME_ERROR("Non-uniform size between samples: %zu vs %zu", samples->size(), numSampleValues);
            *rprData = nullptr;
            *rprDataSize = 0;
            return nullptr;
        }
    }

    *rprData = (rpr_float const*)mergedSamples.get();
    *rprDataSize = requiredNumSamples * numSampleValues;

    return mergedSamples;
}

} // namespace anonymous

TfToken GetRprLpeAovName(rpr::Aov aov) {
    switch (aov) {
        case RPR_AOV_LPE_0:
            return HdRprAovTokens->lpe0;
        case RPR_AOV_LPE_1:
            return HdRprAovTokens->lpe1;
        case RPR_AOV_LPE_2:
            return HdRprAovTokens->lpe2;
        case RPR_AOV_LPE_3:
            return HdRprAovTokens->lpe3;
        case RPR_AOV_LPE_4:
            return HdRprAovTokens->lpe4;
        case RPR_AOV_LPE_5:
            return HdRprAovTokens->lpe5;
        case RPR_AOV_LPE_6:
            return HdRprAovTokens->lpe6;
        case RPR_AOV_LPE_7:
            return HdRprAovTokens->lpe7;
        case RPR_AOV_LPE_8:
            return HdRprAovTokens->lpe8;
        default:
            return TfToken();
    }
}

HdFormat ConvertUsdRenderVarDataType(TfToken const& format) {
    static std::map<TfToken, HdFormat> s_mapping = []() {
        std::map<TfToken, HdFormat> ret;

        auto addMappingEntry = [&ret](std::string name, HdFormat format) {
            ret[TfToken(name, TfToken::Immortal)] = format;
        };

        addMappingEntry("int", HdFormatInt32);
        addMappingEntry("half", HdFormatFloat16);
        addMappingEntry("half3", HdFormatFloat16Vec3);
        addMappingEntry("half4", HdFormatFloat16Vec4);
        addMappingEntry("float", HdFormatFloat32);
        addMappingEntry("float3", HdFormatFloat32Vec3);
        addMappingEntry("float4", HdFormatFloat32Vec4);
        addMappingEntry("point3f", HdFormatFloat32Vec3);
        addMappingEntry("vector3f", HdFormatFloat32Vec3);
        addMappingEntry("normal3f", HdFormatFloat32Vec3);
        addMappingEntry("color2f", HdFormatFloat32Vec2);
        addMappingEntry("color3f", HdFormatFloat32Vec3);
        addMappingEntry("color4f", HdFormatFloat32Vec4);
        addMappingEntry("float2", HdFormatFloat32Vec2);
        addMappingEntry("float16", HdFormatFloat16);
        addMappingEntry("color2h", HdFormatFloat16Vec2);
        addMappingEntry("color3h", HdFormatFloat16Vec3);
        addMappingEntry("color4h", HdFormatFloat16Vec4);
        addMappingEntry("half2", HdFormatFloat16Vec2);
        addMappingEntry("u8", HdFormatUNorm8);
        addMappingEntry("uint8", HdFormatUNorm8);
        addMappingEntry("color2u8", HdFormatUNorm8Vec2);
        addMappingEntry("color3u8", HdFormatUNorm8Vec3);
        addMappingEntry("color4u8", HdFormatUNorm8Vec4);

        return ret;
    }();
    static std::string s_supportedFormats = []() {
        std::string ret;
        auto it = s_mapping.begin();
        for (size_t i = 0; i < s_mapping.size(); ++i, ++it) {
            ret += it->first.GetString();
            if (i + 1 != s_mapping.size()) {
                ret += ", ";
            }
        }
        return ret;
    }();

    auto it = s_mapping.find(format);
    if (it == s_mapping.end()) {
        TF_RUNTIME_ERROR("Unsupported UsdRenderVar format. Supported formats: %s", s_supportedFormats.c_str());
        return HdFormatInvalid;
    }
    return it->second;
}

class HdRprApiRawMaterial : public RprUsdMaterial {
public:
    static HdRprApiRawMaterial* Create(
        rpr::Context* rprContext,
        rpr::MaterialNodeType nodeType,
        std::vector<std::pair<rpr::MaterialNodeInput, GfVec4f>> const& inputs) {

        rpr::Status status;
        auto rprNode = rprContext->CreateMaterialNode(nodeType, &status);
        if (!rprNode) {
            RPR_ERROR_CHECK(status, "Failed to create material node", rprContext);
            return nullptr;
        }

        for (auto& input : inputs) {
            auto& c = input.second;
            if (RPR_ERROR_CHECK(rprNode->SetInput(input.first, c[0], c[1], c[2], c[3]), "Failed to set material input")) {
                delete rprNode;
                return nullptr;
            }
        };

        return new HdRprApiRawMaterial(rprNode);
    }

    ~HdRprApiRawMaterial() final = default;

private:
    HdRprApiRawMaterial(rpr::MaterialNode* surfaceNode)
        : m_retainedSurfaceNode(surfaceNode) {
        m_surfaceNode = surfaceNode;
    }

private:
    std::unique_ptr<rpr::MaterialNode> m_retainedSurfaceNode;
};

struct HdRprApiVolume {
    std::unique_ptr<rpr::HeteroVolume> heteroVolume;
    std::unique_ptr<rpr::Grid> albedoGrid;
    std::unique_ptr<rpr::Grid> densityGrid;
    std::unique_ptr<rpr::Grid> emissionGrid;
    std::unique_ptr<rpr::Shape> cubeMesh;
    std::unique_ptr<HdRprApiRawMaterial> cubeMeshMaterial;
    GfMatrix4f voxelsTransform;
};

struct HdRprApiEnvironmentLight {
    std::unique_ptr<rpr::EnvironmentLight> light;
    std::unique_ptr<RprUsdCoreImage> image;

    std::unique_ptr<rpr::EnvironmentLight> backgroundOverrideLight;
    std::unique_ptr<RprUsdCoreImage> backgroundOverrideImage;

    enum {
        kDetached,
        kAttachedAsLight,
        kAttachedAsEnvLight
    } state = kDetached;
};

class HdRprApiImpl {
public:
    HdRprApiImpl(HdRprDelegate* delegate)
        : m_delegate(delegate) {
        // Postpone initialization as further as possible to allow Hydra user to set custom render settings before creating a context
        //InitIfNeeded();
    }

    ~HdRprApiImpl() {
        RemoveDefaultLight();
    }

    void InitIfNeeded() {
        if (m_state != kStateUninitialized) {
            return;
        }

        static std::mutex s_rprInitMutex;
        LockGuard lock(s_rprInitMutex);

        if (m_state != kStateUninitialized) {
            return;
        }

        try {
            InitRpr();
            InitRif();
            InitAovs();

            {
                HdRprConfig* config;
                auto configInstanceLock = m_delegate->LockConfigInstance(&config);
                UpdateSettings(*config, true);
            }

            InitScene();
            InitCamera();

            m_state = kStateRender;
        } catch (RprUsdError& e) {
            TF_RUNTIME_ERROR("%s", e.what());
            m_state = kStateInvalid;
        }

        // Try to get scene unit size from delegate. Default value is 1 meter per unit
        static const TfToken metersPerUnitToken("stageMetersPerUnit", TfToken::Immortal);
        double unitSize = m_delegate->GetRenderSetting<double>(metersPerUnitToken, 1.0);
        m_unitSizeTransform[0][0] = m_unitSizeTransform[1][1] = m_unitSizeTransform[2][2] = unitSize;
    }

    rpr::Shape* CreateMesh(VtVec3fArray const& points, VtIntArray const& pointIndices,
                           VtVec3fArray const& normals, VtIntArray const& normalIndices,
                           VtVec2fArray const& uvs, VtIntArray const& uvIndices,
                           VtIntArray const& vpf, TfToken const& polygonWinding = HdTokens->rightHanded) {
        VtArray<VtVec3fArray> pointSamples;
        VtArray<VtVec3fArray> normalSamples;
        VtArray<VtVec2fArray> uvSamples;

        if (!points.empty()) pointSamples.push_back(points);
        if (!normals.empty()) normalSamples.push_back(normals);
        if (!uvs.empty()) uvSamples.push_back(uvs);

        return CreateMesh(pointSamples, pointIndices, normalSamples, normalIndices, uvSamples, uvIndices, vpf, polygonWinding);
    }

    rpr::Shape* CreateMesh(VtArray<VtVec3fArray> pointSamples, VtIntArray const& pointIndices,
                           VtArray<VtVec3fArray> normalSamples, VtIntArray const& normalIndices,
                           VtArray<VtVec2fArray> uvSamples, VtIntArray const& uvIndices,
                           VtIntArray const& vpf, TfToken const& polygonWinding) {
        if (!m_rprContext) {
            return nullptr;
        }

        VtIntArray newIndices, newVpf;
        SplitPolygons(pointIndices, vpf, newIndices, newVpf);
        ConvertIndices(&newIndices, newVpf, polygonWinding);

        VtIntArray newNormalIndices;
        if (normalSamples.empty()) {
            if (RprUsdIsHybrid(m_rprContextMetadata.pluginType)) {
                // XXX (Hybrid): we need to generate geometry normals by ourself
                VtVec3fArray normals;
                normals.reserve(newVpf.size());
                newNormalIndices.clear();
                newNormalIndices.reserve(newIndices.size());

                auto& points = pointSamples[0];

                size_t indicesOffset = 0u;
                for (auto numVerticesPerFace : newVpf) {
                    for (int i = 0; i < numVerticesPerFace; ++i) {
                        newNormalIndices.push_back(normals.size());
                    }

                    auto indices = &newIndices[indicesOffset];
                    indicesOffset += numVerticesPerFace;

                    auto p0 = points[indices[0]];
                    auto p1 = points[indices[1]];
                    auto p2 = points[indices[2]];

                    auto e0 = p0 - p1;
                    auto e1 = p2 - p1;

                    auto normal = GfCross(e1, e0);
                    GfNormalize(&normal);
                    normals.push_back(normal);
                }

                normalSamples.push_back(normals);
            }
        } else {
            if (!normalIndices.empty()) {
                SplitPolygons(normalIndices, vpf, newNormalIndices);
                ConvertIndices(&newNormalIndices, newVpf, polygonWinding);
            } else {
                newNormalIndices = newIndices;
            }
        }

        VtIntArray newUvIndices;
        if (uvSamples.empty()) {
            if (RprUsdIsHybrid(m_rprContextMetadata.pluginType)) {
                newUvIndices = newIndices;
                VtVec2fArray uvs(pointSamples[0].size(), GfVec2f(0.0f));
                uvSamples.push_back(uvs);
            }
        } else {
            if (!uvIndices.empty()) {
                SplitPolygons(uvIndices, vpf, newUvIndices);
                ConvertIndices(&newUvIndices, newVpf, polygonWinding);
            } else {
                newUvIndices = newIndices;
            }
        }

        auto normalIndicesData = !newNormalIndices.empty() ? newNormalIndices.data() : newIndices.data();
        if (normalSamples.empty()) {
            normalIndicesData = nullptr;
        }

        auto uvIndicesData = !newUvIndices.empty() ? newUvIndices.data() : uvIndices.data();
        if (uvSamples.empty()) {
            uvIndicesData = nullptr;
        }

        rpr_float const* pointsData = nullptr;
        rpr_float const* normalsData = nullptr;
        rpr_float const* uvsData = nullptr;
        size_t numPoints = 0;
        size_t numNormals = 0;
        size_t numUvs = 0;

        std::vector<rpr_mesh_info> meshProperties(1, rpr_mesh_info(0));

        std::unique_ptr<GfVec3f[]> mergedPoints;
        std::unique_ptr<GfVec3f[]> mergedNormals;
        std::unique_ptr<GfVec2f[]> mergedUvs;

        size_t numMeshSamples = std::max(std::max(pointSamples.size(), normalSamples.size()), uvSamples.size());

        // XXX (RPR): Only Northstar supports deformation motion blur. Use only the first sample for all other plugins.
        if (m_rprContextMetadata.pluginType != kPluginNorthstar) {
            numMeshSamples = 1;
        }

        if (numMeshSamples > 1) {
            meshProperties[0] = (rpr_mesh_info)RPR_MESH_MOTION_DIMENSION;
            meshProperties[1] = (rpr_mesh_info)numMeshSamples;
            meshProperties[2] = (rpr_mesh_info)0;

            mergedPoints = MergeSamples(&pointSamples, numMeshSamples, &pointsData, &numPoints);
            mergedNormals = MergeSamples(&normalSamples, numMeshSamples, &normalsData, &numNormals);
            mergedUvs = MergeSamples(&uvSamples, numMeshSamples, &uvsData, &numUvs);

        } else {
            if (pointSamples.empty()) {
                return nullptr;
            }
            pointsData = (rpr_float const*)pointSamples[0].cdata();
            numPoints = pointSamples[0].size();

            if (!normalSamples.empty()) {
                normalsData = (rpr_float const*)normalSamples[0].data();
                numNormals = normalSamples[0].size();
            }
            
            if (!uvSamples.empty()) {
                uvsData = (rpr_float const*)uvSamples[0].data();
                numUvs = uvSamples[0].size();
            }
        }

        rpr_int texCoordStride = sizeof(GfVec2f);
        rpr_int texCoordIdxStride = sizeof(rpr_int);

        LockGuard rprLock(m_rprContext->GetMutex());

        rpr::Status status;
        auto mesh = m_rprContext->CreateShape(
            pointsData, numPoints, sizeof(GfVec3f),
            normalsData, numNormals, sizeof(GfVec3f),
            nullptr, 0, 0,
            1, &uvsData, &numUvs, &texCoordStride,
            newIndices.data(), sizeof(rpr_int),
            normalIndicesData, sizeof(rpr_int),
            &uvIndicesData, &texCoordIdxStride,
            newVpf.data(), newVpf.size(), meshProperties.data(), &status);
        if (!mesh) {
            RPR_ERROR_CHECK(status, "Failed to create mesh");
            return nullptr;
        }

        if (RPR_ERROR_CHECK(m_scene->Attach(mesh), "Failed to attach mesh to scene")) {
            delete mesh;
            return nullptr;
        }
        m_dirtyFlags |= ChangeTracker::DirtyScene;
        return mesh;
    }

    rpr::Shape* CreateMeshInstance(rpr::Shape* prototype) {
        if (!m_rprContext) {
            return nullptr;
        }

        LockGuard rprLock(m_rprContext->GetMutex());

        rpr::Status status;
        auto mesh = m_rprContext->CreateShapeInstance(prototype, &status);
        if (!mesh) {
            RPR_ERROR_CHECK(status, "Failed to create mesh instance");
            return nullptr;
        }

        if (RPR_ERROR_CHECK(m_scene->Attach(mesh), "Failed to attach mesh to scene")) {
            delete mesh;
            return nullptr;
        }
        m_dirtyFlags |= ChangeTracker::DirtyScene;
        return mesh;
    }

    void SetMeshRefineLevel(rpr::Shape* mesh, const int level) {
        if (!m_rprContext) {
            return;
        }

        if (RprUsdIsHybrid(m_rprContextMetadata.pluginType)) {
            // Not supported
            return;
        }

        LockGuard rprLock(m_rprContext->GetMutex());

        bool dirty = true;

        size_t dummy;
        int oldLevel;
        if (!RPR_ERROR_CHECK(mesh->GetInfo(RPR_SHAPE_SUBDIVISION_FACTOR, sizeof(oldLevel), &oldLevel, &dummy), "Failed to query mesh subdivision factor")) {
            dirty = level != oldLevel;
        }

        if (dirty) {
            uint64_t normalCount;
            if (RPR_ERROR_CHECK(rprMeshGetInfo(GetRprObject(mesh), RPR_MESH_NORMAL_COUNT, sizeof(normalCount), &normalCount, nullptr), "Failed to get normal count")) return;
            if (normalCount != 0) {
                if (RPR_ERROR_CHECK(mesh->SetSubdivisionFactor(level), "Failed to set mesh subdividion level")) return;
            }
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void SetMeshVertexInterpolationRule(rpr::Shape* mesh, TfToken const& boundaryInterpolation) {
        if (!m_rprContext) {
            return;
        }

        if (RprUsdIsHybrid(m_rprContextMetadata.pluginType)) {
            // Not supported
            return;
        }

        rpr_subdiv_boundary_interfop_type newInterfopType = boundaryInterpolation == PxOsdOpenSubdivTokens->edgeAndCorner ?
            RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_AND_CORNER :
            RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_ONLY;

        LockGuard rprLock(m_rprContext->GetMutex());

        bool dirty = true;

        size_t dummy;
        rpr_subdiv_boundary_interfop_type interfopType;
        if (!RPR_ERROR_CHECK(mesh->GetInfo(RPR_SHAPE_SUBDIVISION_BOUNDARYINTEROP, sizeof(interfopType), &interfopType, &dummy), "Failed to query mesh subdivision interfopType")) {
            dirty = newInterfopType != interfopType;
        }

        if (dirty) {
            if (RPR_ERROR_CHECK(mesh->SetSubdivisionBoundaryInterop(newInterfopType), "Fail set mesh subdividion boundary")) return;
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void SetMeshMaterial(rpr::Shape* mesh, RprUsdMaterial const* material, bool displacementEnabled) {
        LockGuard rprLock(m_rprContext->GetMutex());
        if (material) {
            material->AttachTo(mesh, displacementEnabled);
        } else {
            RprUsdMaterial::DetachFrom(mesh);
        }
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    void SetCurveMaterial(rpr::Curve* curve, RprUsdMaterial const* material) {
        LockGuard rprLock(m_rprContext->GetMutex());
        if (material) {
            material->AttachTo(curve);
        } else {
            RprUsdMaterial::DetachFrom(curve);
        }
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    void SetCurveVisibility(rpr::Curve* curve, uint32_t visibilityMask) {
        LockGuard rprLock(m_rprContext->GetMutex());
        if (RprUsdIsHybrid(m_rprContextMetadata.pluginType)) {
            // XXX (Hybrid): rprCurveSetVisibility not supported, emulate visibility using attach/detach
            if (visibilityMask) {
                m_scene->Attach(curve);
            } else {
                m_scene->Detach(curve);
            }
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        } else {
            RPR_ERROR_CHECK(curve->SetVisibilityFlag(RPR_CURVE_VISIBILITY_PRIMARY_ONLY_FLAG, visibilityMask & kVisiblePrimary), "Failed to set curve primary visibility");
            RPR_ERROR_CHECK(curve->SetVisibilityFlag(RPR_CURVE_VISIBILITY_SHADOW, visibilityMask & kVisibleShadow), "Failed to set curve shadow visibility");
            RPR_ERROR_CHECK(curve->SetVisibilityFlag(RPR_CURVE_VISIBILITY_REFLECTION, visibilityMask & kVisibleReflection), "Failed to set curve reflection visibility");
            RPR_ERROR_CHECK(curve->SetVisibilityFlag(RPR_CURVE_VISIBILITY_REFRACTION, visibilityMask & kVisibleRefraction), "Failed to set curve refraction visibility");
            RPR_ERROR_CHECK(curve->SetVisibilityFlag(RPR_CURVE_VISIBILITY_TRANSPARENT, visibilityMask & kVisibleTransparent), "Failed to set curve transparent visibility");
            RPR_ERROR_CHECK(curve->SetVisibilityFlag(RPR_CURVE_VISIBILITY_DIFFUSE, visibilityMask & kVisibleDiffuse), "Failed to set curve diffuse visibility");
            RPR_ERROR_CHECK(curve->SetVisibilityFlag(RPR_CURVE_VISIBILITY_GLOSSY_REFLECTION, visibilityMask & kVisibleGlossyReflection), "Failed to set curve glossyReflection visibility");
            RPR_ERROR_CHECK(curve->SetVisibilityFlag(RPR_CURVE_VISIBILITY_GLOSSY_REFRACTION, visibilityMask & kVisibleGlossyRefraction), "Failed to set curve glossyRefraction visibility");
            
            // kVisibilityLight was intentionally removed because RPR_SHAPE_VISIBILITY_LIGHT is the sum of
            // RPR_SHAPE_VISIBILITY_PRIMARY_ONLY_FLAG and RPR_SHAPE_VISIBILITY_GLOSSY_REFRACTION
            
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void Release(rpr::Curve* curve) {
        if (curve) {
            LockGuard rprLock(m_rprContext->GetMutex());

            if (!RPR_ERROR_CHECK(m_scene->Detach(curve), "Failed to detach curve from scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            };
            delete curve;
        }
    }

    void Release(rpr::Shape* shape) {
        if (shape) {
            LockGuard rprLock(m_rprContext->GetMutex());

            if (!RPR_ERROR_CHECK(m_scene->Detach(shape), "Failed to detach mesh from scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            };
            delete shape;
        }
    }

    void SetMeshVisibility(rpr::Shape* mesh, uint32_t visibilityMask) {
        LockGuard rprLock(m_rprContext->GetMutex());
        if (RprUsdIsHybrid(m_rprContextMetadata.pluginType)) {
            // XXX (Hybrid): rprShapeSetVisibility not supported, emulate visibility using attach/detach
            if (visibilityMask) {
                m_scene->Attach(mesh);
            } else {
                m_scene->Detach(mesh);
            }
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        } else {
            RPR_ERROR_CHECK(mesh->SetVisibilityFlag(RPR_SHAPE_VISIBILITY_PRIMARY_ONLY_FLAG, visibilityMask & kVisiblePrimary), "Failed to set mesh primary visibility");
            RPR_ERROR_CHECK(mesh->SetVisibilityFlag(RPR_SHAPE_VISIBILITY_SHADOW, visibilityMask & kVisibleShadow), "Failed to set mesh shadow visibility");
            RPR_ERROR_CHECK(mesh->SetVisibilityFlag(RPR_SHAPE_VISIBILITY_REFLECTION, visibilityMask & kVisibleReflection), "Failed to set mesh reflection visibility");
            RPR_ERROR_CHECK(mesh->SetVisibilityFlag(RPR_SHAPE_VISIBILITY_REFRACTION, visibilityMask & kVisibleRefraction), "Failed to set mesh refraction visibility");
            RPR_ERROR_CHECK(mesh->SetVisibilityFlag(RPR_SHAPE_VISIBILITY_TRANSPARENT, visibilityMask & kVisibleTransparent), "Failed to set mesh transparent visibility");
            RPR_ERROR_CHECK(mesh->SetVisibilityFlag(RPR_SHAPE_VISIBILITY_DIFFUSE, visibilityMask & kVisibleDiffuse), "Failed to set mesh diffuse visibility");
            RPR_ERROR_CHECK(mesh->SetVisibilityFlag(RPR_SHAPE_VISIBILITY_GLOSSY_REFLECTION, visibilityMask & kVisibleGlossyReflection), "Failed to set mesh glossyReflection visibility");
            RPR_ERROR_CHECK(mesh->SetVisibilityFlag(RPR_SHAPE_VISIBILITY_GLOSSY_REFRACTION, visibilityMask & kVisibleGlossyRefraction), "Failed to set mesh glossyRefraction visibility");
            
            // kVisibilityLight was intentionally removed because RPR_SHAPE_VISIBILITY_LIGHT is the sum of
            // RPR_SHAPE_VISIBILITY_PRIMARY_ONLY_FLAG and RPR_SHAPE_VISIBILITY_GLOSSY_REFRACTION
            
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void SetMeshId(rpr::Shape* mesh, uint32_t id) {
        LockGuard rprLock(m_rprContext->GetMutex());
        RPR_ERROR_CHECK(mesh->SetObjectID(id), "Failed to set mesh id");
    }

    void SetMeshIgnoreContour(rpr::Shape* mesh, bool ignoreContour) {
        if (m_rprContextMetadata.pluginType == kPluginNorthstar) {
            LockGuard rprLock(m_rprContext->GetMutex());
            // TODO: update C++ wrapper
            RPR_ERROR_CHECK(rprShapeSetContourIgnore(rpr::GetRprObject(mesh), ignoreContour), "Failed to set shape contour ignore");

            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    bool SetMeshVertexColor(rpr::Shape* mesh, VtArray<VtVec3fArray> const& primvarSamples, HdInterpolation interpolation) {

        // We use zero primvar channel to store vertex colors
        const int colorPrimvarKey = 0;

        if (primvarSamples.empty()) {
            return false;
        }
        if (m_rprContextMetadata.pluginType == kPluginNorthstar) {
            LockGuard rprLock(m_rprContext->GetMutex());

            rpr::PrimvarInterpolationType rprInterpolation;

            switch (interpolation)
            {
            case HdInterpolationConstant:
                rprInterpolation = RPR_PRIMVAR_INTERPOLATION_CONSTANT;
                break;
            case HdInterpolationUniform:
                rprInterpolation = RPR_PRIMVAR_INTERPOLATION_UNIFORM;
                break;
            case HdInterpolationVertex:
                rprInterpolation = RPR_PRIMVAR_INTERPOLATION_VERTEX;
                break;
            case HdInterpolationVarying:
                rprInterpolation = RPR_PRIMVAR_INTERPOLATION_FACEVARYING_NORMAL;
                break;
            case HdInterpolationFaceVarying:
                rprInterpolation = RPR_PRIMVAR_INTERPOLATION_FACEVARYING_UV;
                break;
            default:
                // Rpr does not support HdInterpolationInstance
                return false;
            }
            try {
                RPR_ERROR_CHECK_THROW(mesh->SetPrimvar(colorPrimvarKey, (rpr_float const*)primvarSamples[0].cdata(), primvarSamples[0].size() * 3, 3, rprInterpolation), "Failed to set color primvars");
            }
            catch (RprUsdError& e) {
                TF_RUNTIME_ERROR("Failed to set vertex color: %s", e.what());
                return false;
            }

            m_dirtyFlags |= ChangeTracker::DirtyScene;
            return true;
        }
        return false;
    }

    rpr::Curve* CreateCurve(VtVec3fArray const& points, VtIntArray const& indices, VtFloatArray const& radiuses, VtVec2fArray const& uvs, VtIntArray const& segmentPerCurve) {
        if (!m_rprContext) {
            return nullptr;
        }

        auto curveCount = segmentPerCurve.size();
        bool isCurveTapered = radiuses.size() != curveCount;

        const size_t kRprCurveSegmentSize = 4;
        if (segmentPerCurve.empty() || points.empty() || indices.empty() ||
            indices.size() % kRprCurveSegmentSize != 0 ||
            (isCurveTapered && radiuses.size() != (indices.size() / kRprCurveSegmentSize) * 2) ||
            (!uvs.empty() && uvs.size() != curveCount)) {
            TF_RUNTIME_ERROR("Failed to create RPR curve: invalid parameters");
            return nullptr;
        }

        uint32_t creationFlags = rpr::kCurveCreationFlagsNone;
        if (isCurveTapered) {
            creationFlags |= rpr::kCurveCreationFlagTapered;
        }

        LockGuard rprLock(m_rprContext->GetMutex());

        rpr::Status status;
        auto curve = m_rprContext->CreateCurve(
            points.size(), (float const*)points.data(), sizeof(GfVec3f),
            indices.size(), curveCount, (rpr_uint const*)indices.data(),
            radiuses.data(), (float const*)uvs.data(), segmentPerCurve.data(), rpr::CurveCreationFlags(creationFlags), &status);
        if (!curve) {
            RPR_ERROR_CHECK(status, "Failed to create curve");
            return nullptr;
        }

        if (RPR_ERROR_CHECK(m_scene->Attach(curve), "Failed to attach curve to scene")) {
            delete curve;
            return nullptr;
        }
        m_dirtyFlags |= ChangeTracker::DirtyScene;
        return curve;
    }

    template <typename T>
    T* CreateLight(std::function<T*(rpr::Status*)> creator) {
        if (!m_rprContext) {
            return nullptr;
        }

        LockGuard rprLock(m_rprContext->GetMutex());

        rpr::Status status;
        auto light = creator(&status);
        if (!light) {
            RPR_ERROR_CHECK(status, "Failed to create light", m_rprContext.get());
            return nullptr;
        }

        if (RPR_ERROR_CHECK(m_scene->Attach(light), "Failed to attach directional light to scene", m_rprContext.get())) {
            delete light;
            return nullptr;
        }

        m_dirtyFlags |= ChangeTracker::DirtyScene;
        m_numLights++;
        return light;
    }

    rpr::DirectionalLight* CreateDirectionalLight() {
        return CreateLight<rpr::DirectionalLight>([this](rpr::Status* status) {
            return m_rprContext->CreateDirectionalLight(status);
        });
    }

    rpr::SpotLight* CreateSpotLight(float angle, float softness) {
        return CreateLight<rpr::SpotLight>([this, angle, softness](rpr::Status* status) {
            auto light = m_rprContext->CreateSpotLight(status);
            if (light) {
                float outerAngle = GfDegreesToRadians(angle);
                float innerAngle = outerAngle * (1.0f - softness);
                RPR_ERROR_CHECK(light->SetConeShape(innerAngle, outerAngle), "Failed to set spot light cone shape");
            }
            return light;
        });
    }

    rpr::PointLight* CreatePointLight() {
        return CreateLight<rpr::PointLight>([this](rpr::Status* status) {
            return m_rprContext->CreatePointLight(status);
        });
    }

    rpr::DiskLight* CreateDiskLight() {
        return CreateLight<rpr::DiskLight>([this](rpr::Status* status) {
            return m_rprContext->CreateDiskLight(status);
        });
    }

    rpr::SphereLight* CreateSphereLight() {
        return CreateLight<rpr::SphereLight>([this](rpr::Status* status) {
            return m_rprContext->CreateSphereLight(status);
        });
    }

    rpr::IESLight* CreateIESLight(std::string const& iesFilepath) {
        return CreateLight<rpr::IESLight>([this, &iesFilepath](rpr::Status* status) {
            auto light = m_rprContext->CreateIESLight(status);
            if (light) {
                // TODO: consider exposing it as light primitive primvar
                constexpr int kIESImageResolution = 256;

                *status = light->SetImageFromFile(iesFilepath.c_str(), kIESImageResolution, kIESImageResolution);
                if (RPR_ERROR_CHECK(*status, "Failed to set IES data")) {
                    delete light;
                    light = nullptr;
                }
            }
            return light;
        });
    }

    void SetDirectionalLightAttributes(rpr::DirectionalLight* light, GfVec3f const& color, float shadowSoftnessAngle) {
        LockGuard rprLock(m_rprContext->GetMutex());

        RPR_ERROR_CHECK(light->SetRadiantPower(color[0], color[1], color[2]), "Failed to set directional light color");
        RPR_ERROR_CHECK(light->SetShadowSoftnessAngle(GfClamp(shadowSoftnessAngle, 0.0f, float(M_PI_4))), "Failed to set directional light color");
    }

    template <typename Light>
    void SetLightRadius(Light* light, float radius) {
        LockGuard rprLock(m_rprContext->GetMutex());

        RPR_ERROR_CHECK(light->SetRadius(radius), "Failed to set light radius");
    }

    void SetLightAngle(rpr::DiskLight* light, float angle) {
        LockGuard rprLock(m_rprContext->GetMutex());

        RPR_ERROR_CHECK(light->SetAngle(angle), "Failed to set light angle");
    }

    void SetLightColor(rpr::RadiantLight* light, GfVec3f const& color) {
        LockGuard rprLock(m_rprContext->GetMutex());

        RPR_ERROR_CHECK(light->SetRadiantPower(color[0], color[1], color[2]), "Failed to set light color");
    }

    void Release(rpr::Light* light) {
        if (light) {
            LockGuard rprLock(m_rprContext->GetMutex());

            if (!RPR_ERROR_CHECK(m_scene->Detach(light), "Failed to detach light from scene")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
            delete light;
            m_numLights--;
        }
    }

    RprUsdMaterial* CreateGeometryLightMaterial(GfVec3f const& emissionColor) {
        if (!m_rprContext) {
            return nullptr;
        }

        LockGuard rprLock(m_rprContext->GetMutex());

        auto material = HdRprApiRawMaterial::Create(m_rprContext.get(), RPR_MATERIAL_NODE_EMISSIVE, {
            {RPR_MATERIAL_INPUT_COLOR, ToVec4(emissionColor, 1.0f)}
        });
        if (material) m_numLights++;
        return material;
    }

    void ReleaseGeometryLightMaterial(RprUsdMaterial* material) {
        if (material) {
            m_numLights--;
            Release(material);
        }
    }

    HdRprApiEnvironmentLight* CreateEnvironmentLight(std::unique_ptr<RprUsdCoreImage>&& image, float intensity, HdRprApi::BackgroundOverride const& backgroundOverride) {
        // XXX (RPR): default environment light should be removed before creating a new one - RPR limitation
        RemoveDefaultLight();

        auto envLight = new HdRprApiEnvironmentLight;

        rpr::Status status;
        envLight->light.reset(m_rprContext->CreateEnvironmentLight(&status));
        envLight->image = std::move(image);

        if (!envLight ||
            RPR_ERROR_CHECK(envLight->light->SetImage(envLight->image->GetRootImage()), "Failed to set env light image", m_rprContext.get()) ||
            RPR_ERROR_CHECK(envLight->light->SetIntensityScale(intensity), "Failed to set env light intensity", m_rprContext.get())) {
            RPR_ERROR_CHECK(status, "Failed to create environment light");
            delete envLight;
            return nullptr;
        }

        if (RprUsdIsHybrid(m_rprContextMetadata.pluginType)) {
            if ((status = m_scene->SetEnvironmentLight(envLight->light.get())) == RPR_SUCCESS) {
                envLight->state = HdRprApiEnvironmentLight::kAttachedAsEnvLight;
            }
        } else {
            if ((status = m_scene->Attach(envLight->light.get())) == RPR_SUCCESS) {
                envLight->state = HdRprApiEnvironmentLight::kAttachedAsLight;
            }
        }
        if (status != RPR_SUCCESS) {
            RPR_ERROR_CHECK(status, "Failed to attach environment light", m_rprContext.get());
            delete envLight;
            return nullptr;
        }

        if (backgroundOverride.enable) {
            envLight->backgroundOverrideLight.reset(m_rprContext->CreateEnvironmentLight(&status));
            envLight->backgroundOverrideImage = CreateConstantColorImage(backgroundOverride.color.data());

            if (!envLight->backgroundOverrideLight ||
                !envLight->backgroundOverrideImage ||
                RPR_ERROR_CHECK(envLight->backgroundOverrideLight->SetImage(envLight->backgroundOverrideImage->GetRootImage()), "Failed to set background override env light image", m_rprContext.get()) ||
                RPR_ERROR_CHECK(envLight->light->SetEnvironmentLightOverride(RPR_ENVIRONMENT_LIGHT_OVERRIDE_BACKGROUND, envLight->backgroundOverrideLight.get()), "Failed to set env light background override")) {
                envLight->backgroundOverrideLight = nullptr;
                envLight->backgroundOverrideImage = nullptr;
            }
        }

        m_dirtyFlags |= ChangeTracker::DirtyScene;
        m_numLights++;
        return envLight;
    }

    void ReleaseImpl(HdRprApiEnvironmentLight* envLight) {
        if (envLight) {
            rpr::Status status;
            if (envLight->state == HdRprApiEnvironmentLight::kAttachedAsEnvLight) {
                status = m_scene->SetEnvironmentLight(nullptr);
            } else {
                status = m_scene->Detach(envLight->light.get());
            }

            if (!RPR_ERROR_CHECK(status, "Failed to detach environment light")) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
            delete envLight;
            m_numLights--;
        }
    }

    void Release(HdRprApiEnvironmentLight* envLight) {
        if (envLight) {
            LockGuard rprLock(m_rprContext->GetMutex());
            ReleaseImpl(envLight);
        }
    }

    HdRprApiEnvironmentLight* CreateEnvironmentLight(const std::string& path, float intensity, HdRprApi::BackgroundOverride const& backgroundOverride) {
        if (!m_rprContext || path.empty()) {
            return nullptr;
        }

        LockGuard rprLock(m_rprContext->GetMutex());

        auto image = std::unique_ptr<RprUsdCoreImage>(RprUsdCoreImage::Create(m_rprContext.get(), path, 0));
        if (!image) {
            return nullptr;
        }

        return CreateEnvironmentLight(std::move(image), intensity, backgroundOverride);
    }

    HdRprApiEnvironmentLight* CreateEnvironmentLight(GfVec3f color, float intensity, HdRprApi::BackgroundOverride const& backgroundOverride) {
        if (!m_rprContext) {
            return nullptr;
        }

        LockGuard rprLock(m_rprContext->GetMutex());

        auto backgroundImage = CreateConstantColorImage(color.data());
        if (!backgroundImage) {
            return nullptr;
        }

        return CreateEnvironmentLight(std::move(backgroundImage), intensity, backgroundOverride);
    }

    void SetTransform(rpr::SceneObject* object, GfMatrix4f const& transform) {
        LockGuard rprLock(m_rprContext->GetMutex());

        // apply scene units size
        auto finalTransform = transform * GfMatrix4f(m_unitSizeTransform);

        if (!RPR_ERROR_CHECK(object->SetTransform(finalTransform.GetArray(), false), "Fail set object transform")) {
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void DecomposeTransform(GfMatrix4d const& transform, GfVec3f& scale, GfQuatf& orient, GfVec3f& translate) {
        translate = GfVec3f(transform.ExtractTranslation());

        GfVec3f col[3], skew;

        // Now get scale and shear.
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                col[i][j] = transform[i][j];
            }
        }

        scale[0] = col[0].GetLength();
        col[0] /= scale[0];

        skew[2] = GfDot(col[0], col[1]);
        // Make Y col orthogonal to X col
        col[1] = col[1] - skew[2] * col[0];

        scale[1] = col[1].GetLength();
        col[1] /= scale[1];
        skew[2] /= scale[1];

        // Compute XZ and YZ shears, orthogonalize Z col
        skew[1] = GfDot(col[0], col[2]);
        col[2] = col[2] - skew[1] * col[0];
        skew[0] = GfDot(col[1], col[2]);
        col[2] = col[2] - skew[0] * col[1];

        scale[2] = col[2].GetLength();
        col[2] /= scale[2];
        skew[1] /= scale[2];
        skew[0] /= scale[2];

        // At this point, the matrix is orthonormal.
        // Check for a coordinate system flip. If the determinant
        // is -1, then negate the matrix and the scaling factors.
        if (GfDot(col[0], GfCross(col[1], col[2])) < 0.0f) {
            for (int i = 0; i < 3; i++) {
                scale[i] *= -1.0f;
                col[i] *= -1.0f;
            }
        }

        float trace = col[0][0] + col[1][1] + col[2][2];
        if (trace > 0.0f) {
            float root = std::sqrt(trace + 1.0f);
            orient.SetReal(0.5f * root);
            root = 0.5f / root;
            orient.SetImaginary(
                root * (col[1][2] - col[2][1]),
                root * (col[2][0] - col[0][2]),
                root * (col[0][1] - col[1][0]));
        } else {
            static int next[3] = {1, 2, 0};
            int i, j, k = 0;
            i = 0;
            if (col[1][1] > col[0][0]) i = 1;
            if (col[2][2] > col[i][i]) i = 2;
            j = next[i];
            k = next[j];

            float root = std::sqrt(col[i][i] - col[j][j] - col[k][k] + 1.0f);

            GfVec3f im;
            im[i] = 0.5f * root;
            root = 0.5f / root;
            im[j] = root * (col[i][j] + col[j][i]);
            im[k] = root * (col[i][k] + col[k][i]);
            orient.SetImaginary(im);
            orient.SetReal(root * (col[j][k] - col[k][j]));
        }
    }

    void GetMotion(GfMatrix4d const& startTransform, GfMatrix4d const& endTransform,
                   GfVec3f* linearMotion, GfVec3f* scaleMotion, GfVec3f* rotateAxis, float* rotateAngle) {
        GfVec3f startScale, startTranslate; GfQuatf startRotation;
        GfVec3f endScale, endTranslate; GfQuatf endRotation;
        DecomposeTransform(startTransform, startScale, startRotation, startTranslate);
        DecomposeTransform(endTransform, endScale, endRotation, endTranslate);

        *linearMotion = endTranslate - startTranslate;
        *scaleMotion = endScale - startScale;
        *rotateAxis = GfVec3f(1, 0, 0);
        *rotateAngle = 0.0f;

        auto rotateMotion = endRotation * startRotation.GetInverse();
        auto imLen = rotateMotion.GetImaginary().GetLength();
        if (imLen > std::numeric_limits<float>::epsilon()) {
            *rotateAxis = rotateMotion.GetImaginary() / imLen;
            *rotateAngle = 2.0f * std::atan2(imLen, rotateMotion.GetReal());
        }

        if (*rotateAngle > M_PI) {
            *rotateAngle -= 2 * M_PI;
        }
    }

    void SetTransform(rpr::Shape* shape, size_t numSamples, float* timeSamples, GfMatrix4d* transformSamples) {
        // TODO: Implement C++ wrapper methods
        auto rprShapeHandle = rpr::GetRprObject(shape);

        if (numSamples == 1) {
            RPR_ERROR_CHECK(rprShapeSetMotionTransformCount(rprShapeHandle, 0), "Failed to set shape motion transform count");

            return SetTransform(shape, GfMatrix4f(transformSamples[0]));
        }

        // XXX (RPR): for the moment, RPR supports only 1 motion matrix
        auto startTransform = transformSamples[0] * m_unitSizeTransform;
        auto endTransform = transformSamples[numSamples - 1] * m_unitSizeTransform;

        auto rprStartTransform = GfMatrix4f(startTransform);

        if (m_rprContextMetadata.pluginType == kPluginNorthstar) {
            LockGuard rprLock(m_rprContext->GetMutex());
            RPR_ERROR_CHECK(shape->SetTransform(rprStartTransform.GetArray(), false), "Fail set shape transform");

            auto rprEndTransform = GfMatrix4f(endTransform);
            RPR_ERROR_CHECK(rprShapeSetMotionTransformCount(rprShapeHandle, 1), "Failed to set shape motion transform count");
            RPR_ERROR_CHECK(rprShapeSetMotionTransform(rprShapeHandle, false, rprEndTransform.GetArray(), 1), "Failed to set shape motion transform count");
        } else {
            GfVec3f linearMotion, scaleMotion, rotateAxis;
            float rotateAngle;
            GetMotion(startTransform, endTransform, &linearMotion, &scaleMotion, &rotateAxis, &rotateAngle);

            LockGuard rprLock(m_rprContext->GetMutex());

            RPR_ERROR_CHECK(shape->SetTransform(rprStartTransform.GetArray(), false), "Fail set shape transform");
            RPR_ERROR_CHECK(shape->SetLinearMotion(linearMotion[0], linearMotion[1], linearMotion[2]), "Fail to set shape linear motion");
            RPR_ERROR_CHECK(shape->SetScaleMotion(scaleMotion[0], scaleMotion[1], scaleMotion[2]), "Fail to set shape scale motion");
            RPR_ERROR_CHECK(shape->SetAngularMotion(rotateAxis[0], rotateAxis[1], rotateAxis[2], rotateAngle), "Fail to set shape angular motion");
        }
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    RprUsdMaterial* CreateMaterial(SdfPath const& materialId, HdSceneDelegate* sceneDelegate, HdMaterialNetworkMap const& materialNetwork) {
        if (!m_rprContext) {
            return nullptr;
        }

        LockGuard rprLock(m_rprContext->GetMutex());
        return RprUsdMaterialRegistry::GetInstance().CreateMaterial(materialId, sceneDelegate, materialNetwork, m_rprContext.get(), m_imageCache.get());
    }

    RprUsdMaterial* CreatePointsMaterial(VtVec3fArray const& colors) {
        if (!m_rprContext) {
            return nullptr;
        }

        LockGuard rprLock(m_rprContext->GetMutex());

        class HdRprApiPointsMaterial : public RprUsdMaterial {
        public:
            HdRprApiPointsMaterial(VtVec3fArray const& colors, rpr::Context* context) {
                rpr::Status status;

                rpr::BufferDesc bufferDesc;
                bufferDesc.nb_element = colors.size();
                bufferDesc.element_type = RPR_BUFFER_ELEMENT_TYPE_FLOAT32;
                bufferDesc.element_channel_size = 3;

                m_colorsBuffer.reset(context->CreateBuffer(bufferDesc, colors.data(), &status));
                if (!m_colorsBuffer) {
                    RPR_ERROR_CHECK_THROW(status, "Failed to create colors buffer");
                }

                m_objectIdLookupNode.reset(context->CreateMaterialNode(RPR_MATERIAL_NODE_INPUT_LOOKUP, &status));
                if (!m_objectIdLookupNode) {
                    RPR_ERROR_CHECK_THROW(status, "Failed to create objectId lookup node");
                }

                status = m_objectIdLookupNode->SetInput(RPR_MATERIAL_INPUT_VALUE, RPR_MATERIAL_NODE_LOOKUP_OBJECT_ID);
                if (status != RPR_SUCCESS) {
                    RPR_ERROR_CHECK_THROW(status, "Failed to set lookup node input value");
                }

                m_bufferSamplerNode.reset(context->CreateMaterialNode(RPR_MATERIAL_NODE_BUFFER_SAMPLER, &status));
                if (!m_bufferSamplerNode) {
                    RPR_ERROR_CHECK_THROW(status, "Failed to create buffer sampler node");
                }
                RPR_ERROR_CHECK_THROW(m_bufferSamplerNode->SetInput(RPR_MATERIAL_INPUT_DATA, m_colorsBuffer.get()), "Failed to set buffer sampler node input data");
                RPR_ERROR_CHECK_THROW(m_bufferSamplerNode->SetInput(RPR_MATERIAL_INPUT_UV, m_objectIdLookupNode.get()), "Failed to set buffer sampler node input uv");

                m_uberNode.reset(context->CreateMaterialNode(RPR_MATERIAL_NODE_UBERV2, &status));
                if (!m_uberNode) {
                    RPR_ERROR_CHECK_THROW(status, "Failed to create uber node");
                }
                RPR_ERROR_CHECK_THROW(m_uberNode->SetInput(RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR, m_bufferSamplerNode.get()), "Failed to set root material diffuse color");

                m_surfaceNode = m_uberNode.get();
            }

            ~HdRprApiPointsMaterial() final = default;

        private:
            std::unique_ptr<rpr::Buffer> m_colorsBuffer;
            std::unique_ptr<rpr::MaterialNode> m_objectIdLookupNode;
            std::unique_ptr<rpr::MaterialNode> m_bufferSamplerNode;
            std::unique_ptr<rpr::MaterialNode> m_uberNode;
        };

        try {
            return new HdRprApiPointsMaterial(colors, m_rprContext.get());
        } catch (RprUsdError& e) {
            TF_RUNTIME_ERROR("Failed to create points material: %s", e.what());
            return nullptr;
        }
    }

    RprUsdMaterial* CreatePrimvarColorLookupMaterial() {
        if (!m_rprContext) {
            return nullptr;
        }

        LockGuard rprLock(m_rprContext->GetMutex());

        class HdRprApiPrimvarColorLookupMaterial : public RprUsdMaterial {
        public:
            HdRprApiPrimvarColorLookupMaterial(rpr::Context* context) {
                rpr::Status status;

                m_primvarLookupNode.reset(context->CreateMaterialNode(RPR_MATERIAL_NODE_PRIMVAR_LOOKUP, &status));
                if (!m_primvarLookupNode) {
                    RPR_ERROR_CHECK_THROW(status, "Failed to create primvar lookup node");
                }

                // we use zero primvar channel to store vertex color values
                status = m_primvarLookupNode->SetInput(RPR_MATERIAL_INPUT_VALUE, (rpr_uint) 0);
                if (status != RPR_SUCCESS) {
                    RPR_ERROR_CHECK_THROW(status, "Failed to set lookup node input value");
                }

                m_uberNode.reset(context->CreateMaterialNode(RPR_MATERIAL_NODE_UBERV2, &status));
                if (!m_uberNode) {
                    RPR_ERROR_CHECK_THROW(status, "Failed to create uber node");
                }
                RPR_ERROR_CHECK_THROW(m_uberNode->SetInput(RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR, m_primvarLookupNode.get()), "Failed to set root material diffuse color");

                m_surfaceNode = m_uberNode.get();
            };

            ~HdRprApiPrimvarColorLookupMaterial() final = default;

        private:
            std::unique_ptr<rpr::MaterialNode> m_primvarLookupNode;
            std::unique_ptr<rpr::MaterialNode> m_uberNode;
        };

        try {
            return new HdRprApiPrimvarColorLookupMaterial(m_rprContext.get());
        }
        catch (RprUsdError& e) {
            TF_RUNTIME_ERROR("Failed to create points material: %s", e.what());
            return nullptr;
        }
    }

    RprUsdMaterial* CreateRawMaterial(
        rpr::MaterialNodeType nodeType,
        std::vector<std::pair<rpr::MaterialNodeInput, GfVec4f>> const& inputs) {
        if (!m_rprContext) {
            return nullptr;
        }

        LockGuard rprLock(m_rprContext->GetMutex());
        return HdRprApiRawMaterial::Create(m_rprContext.get(), nodeType, inputs);
    }

    void Release(RprUsdMaterial* material) {
        if (material) {
            LockGuard rprLock(m_rprContext->GetMutex());
            delete material;
        }
    }

    HdRprApiVolume* CreateVolume(VtUIntArray const& densityCoords, VtFloatArray const& densityValues, VtVec3fArray const& densityLUT, float densityScale,
                                 VtUIntArray const& albedoCoords, VtFloatArray const& albedoValues, VtVec3fArray const& albedoLUT, float albedoScale,
                                 VtUIntArray const& emissionCoords, VtFloatArray const& emissionValues, VtVec3fArray const& emissionLUT, float emissionScale,
                                 const GfVec3i& gridSize, const GfVec3f& voxelSize, const GfVec3f& gridBBLow) {
        if (!m_rprContext) {
            return nullptr;
        }

        auto cubeMesh = CreateCubeMesh(1.0f, 1.0f, 1.0f);
        if (!cubeMesh) {
            return nullptr;
        }

        LockGuard rprLock(m_rprContext->GetMutex());

        auto rprApiVolume = new HdRprApiVolume;

        rprApiVolume->cubeMeshMaterial.reset(HdRprApiRawMaterial::Create(m_rprContext.get(), RPR_MATERIAL_NODE_TRANSPARENT, {
            {RPR_MATERIAL_INPUT_COLOR, GfVec4f(1.0f)}
        }));
        rprApiVolume->cubeMesh.reset(cubeMesh);

        rpr::Status densityGridStatus;
        rprApiVolume->densityGrid.reset(m_rprContext->CreateGrid(gridSize[0], gridSize[1], gridSize[2],
            &densityCoords[0], densityCoords.size() / 3, RPR_GRID_INDICES_TOPOLOGY_XYZ_U32,
            &densityValues[0], densityValues.size() * sizeof(densityValues[0]), 0, &densityGridStatus));

        rpr::Status albedoGridStatus;
        rprApiVolume->albedoGrid.reset(m_rprContext->CreateGrid(gridSize[0], gridSize[1], gridSize[2],
            &albedoCoords[0], albedoCoords.size() / 3, RPR_GRID_INDICES_TOPOLOGY_XYZ_U32,
            &albedoValues[0], albedoValues.size() * sizeof(albedoValues[0]), 0, &albedoGridStatus));

        rpr::Status emissionGridStatus;
        rprApiVolume->emissionGrid.reset(m_rprContext->CreateGrid(gridSize[0], gridSize[1], gridSize[2],
            &emissionCoords[0], emissionCoords.size() / 3, RPR_GRID_INDICES_TOPOLOGY_XYZ_U32,
            &emissionValues[0], emissionValues.size() * sizeof(emissionValues[0]), 0, &emissionGridStatus));

        rpr::Status status;
        rprApiVolume->heteroVolume.reset(m_rprContext->CreateHeteroVolume(&status));

        if (!rprApiVolume->densityGrid || !rprApiVolume->albedoGrid || !rprApiVolume->emissionGrid ||
            !rprApiVolume->cubeMeshMaterial || !rprApiVolume->cubeMesh ||
            !rprApiVolume->heteroVolume ||

            RPR_ERROR_CHECK(rprApiVolume->heteroVolume->SetDensityGrid(rprApiVolume->densityGrid.get()), "Failed to set density hetero volume grid") ||
            RPR_ERROR_CHECK(rprApiVolume->heteroVolume->SetDensityLookup((float*)densityLUT.data(), densityLUT.size()), "Failed to set density volume lookup values") ||
            RPR_ERROR_CHECK(rprApiVolume->heteroVolume->SetDensityScale(densityScale), "Failed to set volume's density scale") ||

            RPR_ERROR_CHECK(rprApiVolume->heteroVolume->SetAlbedoGrid(rprApiVolume->albedoGrid.get()), "Failed to set albedo hetero volume grid") ||
            RPR_ERROR_CHECK(rprApiVolume->heteroVolume->SetAlbedoLookup((float*)albedoLUT.data(), albedoLUT.size()), "Failed to set albedo volume lookup values") ||
            RPR_ERROR_CHECK(rprApiVolume->heteroVolume->SetAlbedoScale(albedoScale), "Failed to set volume's albedo scale") ||

            RPR_ERROR_CHECK(rprApiVolume->heteroVolume->SetEmissionGrid(rprApiVolume->emissionGrid.get()), "Failed to set emission hetero volume grid") ||
            RPR_ERROR_CHECK(rprApiVolume->heteroVolume->SetEmissionLookup((float*)emissionLUT.data(), emissionLUT.size()), "Failed to set emission volume lookup values") ||
            RPR_ERROR_CHECK(rprApiVolume->heteroVolume->SetEmissionScale(emissionScale), "Failed to set volume's emission scale") ||

            RPR_ERROR_CHECK(rprApiVolume->cubeMesh->SetHeteroVolume(rprApiVolume->heteroVolume.get()), "Failed to set hetero volume to mesh") ||
            RPR_ERROR_CHECK(m_scene->Attach(rprApiVolume->heteroVolume.get()), "Failed attach hetero volume")) {

            RPR_ERROR_CHECK(densityGridStatus, "Failed to create density grid");
            RPR_ERROR_CHECK(albedoGridStatus, "Failed to create albedo grid");
            RPR_ERROR_CHECK(emissionGridStatus, "Failed to create emission grid");
            RPR_ERROR_CHECK(status, "Failed to create hetero volume");

            m_scene->Detach(rprApiVolume->heteroVolume.get());
            delete rprApiVolume;

            return nullptr;
        }

        rprApiVolume->cubeMeshMaterial->AttachTo(rprApiVolume->cubeMesh.get(), false);

        rprApiVolume->voxelsTransform = GfMatrix4f(1.0f);
        rprApiVolume->voxelsTransform.SetScale(GfCompMult(voxelSize, gridSize));
        rprApiVolume->voxelsTransform.SetTranslateOnly(GfCompMult(voxelSize, GfVec3f(gridSize)) / 2.0f + gridBBLow);

        return rprApiVolume;
    }

    void SetTransform(HdRprApiVolume* volume, GfMatrix4f const& transform) {
        auto t = transform * volume->voxelsTransform * GfMatrix4f(m_unitSizeTransform);

        LockGuard rprLock(m_rprContext->GetMutex());
        RPR_ERROR_CHECK(volume->cubeMesh->SetTransform(t.data(), false), "Failed to set cubeMesh transform");
        RPR_ERROR_CHECK(volume->heteroVolume->SetTransform(t.data(), false), "Failed to set heteroVolume transform");
        m_dirtyFlags |= ChangeTracker::DirtyScene;
    }

    void Release(HdRprApiVolume* volume) {
        if (volume) {
            LockGuard rprLock(m_rprContext->GetMutex());

            m_scene->Detach(volume->heteroVolume.get());
            delete volume;

            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }
    }

    void SetName(rpr::ContextObject* object, const char* name) {
        LockGuard rprLock(m_rprContext->GetMutex());
        object->SetName(name);
    }

    void SetName(RprUsdMaterial* object, const char* name) {
        LockGuard rprLock(m_rprContext->GetMutex());
        object->SetName(name);
    }

    void SetName(HdRprApiEnvironmentLight* object, const char* name) {
        LockGuard rprLock(m_rprContext->GetMutex());
        object->light->SetName(name);
        object->image->SetName(name);
    }

    void SetCamera(HdCamera const* camera) {
        auto hdRprCamera = dynamic_cast<HdRprCamera const*>(camera);
        if (!hdRprCamera) {
            TF_CODING_ERROR("HdRprApi can work only with HdRprCamera");
            return;
        }

        if (m_hdCamera != hdRprCamera) {
            m_hdCamera = hdRprCamera;
            m_dirtyFlags |= ChangeTracker::DirtyHdCamera;
        }
    }

    HdCamera const* GetCamera() const {
        return m_hdCamera;
    }

    GfMatrix4d GetCameraViewMatrix() const {
        return m_hdCamera ? (m_hdCamera->GetTransform() * m_unitSizeTransform).GetInverse() : GfMatrix4d(1.0);
    }

    const GfMatrix4d& GetCameraProjectionMatrix() const {
        return m_cameraProjectionMatrix;
    }

    GfVec2i GetViewportSize() const {
        return m_viewportSize;
    }

    void SetViewportSize(GfVec2i const& size) {
        m_viewportSize = size;
        m_dirtyFlags |= ChangeTracker::DirtyViewport;
    }

    void SetAovBindings(HdRenderPassAovBindingVector const& aovBindings) {
        m_aovBindings = aovBindings;
        m_dirtyFlags |= ChangeTracker::DirtyAOVBindings;

        auto retainedOutputRenderBuffers = std::move(m_outputRenderBuffers);
        auto registerAovBinding = [&retainedOutputRenderBuffers, this](HdRenderPassAovBinding const& aovBinding) -> OutputRenderBuffer* {
            OutputRenderBuffer outRb;
            outRb.aovBinding = &aovBinding;

            HdFormat aovFormat;
            if (!GetAovBindingInfo(aovBinding, &outRb.aovName, &outRb.lpe, &aovFormat)) {
                return nullptr;
            }

            decltype(retainedOutputRenderBuffers.begin()) outputRenderBufferIt;
            if (outRb.lpe.empty()) {
                outputRenderBufferIt = std::find_if(retainedOutputRenderBuffers.begin(), retainedOutputRenderBuffers.end(),
                    [&outRb](OutputRenderBuffer const& buffer) { return buffer.aovName == outRb.aovName; });
            } else {
                outputRenderBufferIt = std::find_if(retainedOutputRenderBuffers.begin(), retainedOutputRenderBuffers.end(),
                    [&outRb](OutputRenderBuffer const& buffer) { return buffer.lpe == outRb.lpe; });
            }

            auto rprRenderBuffer = static_cast<HdRprRenderBuffer*>(aovBinding.renderBuffer);
            if (outputRenderBufferIt == retainedOutputRenderBuffers.end()) {
                if (!outRb.lpe.empty()) {
                    // We can bind limited amount of LPE AOVs with RPR.
                    // lpeAovPool controls available AOV binding slots.
                    if (m_lpeAovPool.empty()) {
                        TF_RUNTIME_ERROR("Cannot create \"%s\" LPE AOV: exceeded the number of LPE AOVs at the same time - %d",
                            outRb.lpe.c_str(), +RPR_AOV_LPE_8 - +RPR_AOV_LPE_0 + 1);
                        return nullptr;
                    }

                    outRb.aovName = m_lpeAovPool.back();
                    m_lpeAovPool.pop_back();
                }

                // Create new RPR AOV
                outRb.rprAov = CreateAov(outRb.aovName, rprRenderBuffer->GetWidth(), rprRenderBuffer->GetHeight(), aovFormat);
            } else if (outputRenderBufferIt->rprAov) {
                // Reuse previously created RPR AOV
                std::swap(outRb.rprAov, outputRenderBufferIt->rprAov);
                // Update underlying format if needed
                outRb.rprAov->Resize(rprRenderBuffer->GetWidth(), rprRenderBuffer->GetHeight(), aovFormat);
            }

            if (!outRb.rprAov) return nullptr;

            if (!outRb.lpe.empty() &&
                RPR_ERROR_CHECK(outRb.rprAov->GetAovFb()->GetRprObject()->SetLPE(outRb.lpe.c_str()), "Failed to set LPE")) {
                return nullptr;
            }

            m_outputRenderBuffers.push_back(std::move(outRb));
            return &m_outputRenderBuffers.back();
        };

        for (auto& aovBinding : m_aovBindings) {
            if (!aovBinding.renderBuffer) {
                continue;
            }

            if (auto outputRb = registerAovBinding(aovBinding)) {
                auto rprRenderBuffer = static_cast<HdRprRenderBuffer*>(aovBinding.renderBuffer);
                outputRb->isMultiSampled = rprRenderBuffer->IsMultiSampled();
                outputRb->mappedData = rprRenderBuffer->GetPointerForWriting();
                outputRb->mappedDataSize = HdDataSizeOfFormat(rprRenderBuffer->GetFormat()) * rprRenderBuffer->GetWidth() * rprRenderBuffer->GetHeight();
            }
        }
    }

    HdRenderPassAovBindingVector const& GetAovBindings() const {
        return m_aovBindings;
    }

    void ResolveFramebuffers() {
        auto startTime = std::chrono::high_resolution_clock::now();

        m_resolveData.ForAllAovs([&](ResolveData::AovEntry& e) {
            if (m_isFirstSample || e.isMultiSampled) {
                e.aov->Resolve();
            }
        });

        if (m_rifContext) {
            m_rifContext->ExecuteCommandQueue();
        }

        for (auto& outRb : m_outputRenderBuffers) {
            if (outRb.mappedData && (m_isFirstSample || outRb.isMultiSampled)) {
                outRb.rprAov->GetData(outRb.mappedData, outRb.mappedDataSize);
            }
        }

        m_isFirstSample = false;

        auto resolveTime = std::chrono::high_resolution_clock::now() - startTime;
        m_frameResolveTotalTime += resolveTime;

        if (m_resolveMode == kResolveInRenderUpdateCallback) {
            // When RUC is enabled, we do resolves in between of rendering on the same thread
            // TODO (optimization): move resolves in a background thread (makes sense for non-interactive, GPU-only rendering)
            //
            m_frameRenderTotalTime -= resolveTime;
        }
    }

    void Update() {
        auto rprRenderParam = static_cast<HdRprRenderParam*>(m_delegate->GetRenderParam());

        // In case there is no Lights in scene - create default
        if (m_numLights == 0) {
            AddDefaultLight();
        } else {
            RemoveDefaultLight();
        }

        bool clearAovs = false;
        RenderSetting<bool> enableDenoise;
        RenderSetting<HdRprApiColorAov::TonemapParams> tonemap;
        RenderSetting<HdRprApiColorAov::GammaParams> gamma;
        RenderSetting<bool> instantaneousShutter;
        RenderSetting<TfToken> aspectRatioPolicy;
        RenderSetting<TfToken> cameraMode;
        {
            HdRprConfig* config;
            auto configInstanceLock = m_delegate->LockConfigInstance(&config);

            enableDenoise.isDirty = config->IsDirty(HdRprConfig::DirtyDenoise);
            if (enableDenoise.isDirty) {
                enableDenoise.value = config->GetDenoisingEnable();

                m_denoiseMinIter = config->GetDenoisingMinIter();
                m_denoiseIterStep = config->GetDenoisingIterStep();
            }

            tonemap.isDirty = config->IsDirty(HdRprConfig::DirtyTonemapping);
            if (tonemap.isDirty) {
                tonemap.value.enable = config->GetTonemappingEnable();
                tonemap.value.exposureTime = config->GetTonemappingExposureTime();
                tonemap.value.sensitivity = config->GetTonemappingSensitivity();
                tonemap.value.fstop = config->GetTonemappingFstop();
                tonemap.value.gamma = config->GetTonemappingGamma();
            }

            gamma.isDirty = config->IsDirty(HdRprConfig::DirtyGamma);
            if (gamma.isDirty) {
                gamma.value.enable = config->GetGammaEnable();
                gamma.value.value = config->GetGammaValue();
            }

            cameraMode.isDirty = config->IsDirty(HdRprConfig::DirtyCamera);
            cameraMode.value = config->GetCoreCameraMode();
            aspectRatioPolicy.isDirty = config->IsDirty(HdRprConfig::DirtyUsdNativeCamera);
            aspectRatioPolicy.value = config->GetAspectRatioConformPolicy();
            instantaneousShutter.isDirty = config->IsDirty(HdRprConfig::DirtyUsdNativeCamera);
            instantaneousShutter.value = config->GetInstantaneousShutter();

            if (config->IsDirty(HdRprConfig::DirtyRprExport)) {
                m_rprSceneExportPath = GetPath(config->GetExportPath());
                m_rprExportAsSingleFile = config->GetExportAsSingleFile();
                m_rprExportUseImageCache = config->GetExportUseImageCache();
            }

            if (config->IsDirty(HdRprConfig::DirtyRenderQuality)) {
                m_currentRenderQuality = GetRenderQuality(*config);
                auto newPlugin = GetPluginType(m_currentRenderQuality);
                auto activePlugin = m_rprContextMetadata.pluginType;
                m_state = (newPlugin != activePlugin) ? kStateRestartRequired : kStateRender;
            }

            if (m_state == kStateRender && config->IsDirty(HdRprConfig::DirtyRenderQuality)) {
                TfToken activeRenderQuality;
                if (m_rprContextMetadata.pluginType == kPluginTahoe) {
                    activeRenderQuality = HdRprCoreRenderQualityTokens->Full;
                } else if (m_rprContextMetadata.pluginType == kPluginNorthstar) {
                    activeRenderQuality = HdRprCoreRenderQualityTokens->Northstar;
                } else if (m_rprContextMetadata.pluginType == kPluginHybridPro) {
                    activeRenderQuality = HdRprCoreRenderQualityTokens->HybridPro;
                } else {
                    rpr_uint currentHybridQuality = RPR_RENDER_QUALITY_HIGH;
                    size_t dummy;
                    RPR_ERROR_CHECK(m_rprContext->GetInfo(rpr::ContextInfo(RPR_CONTEXT_RENDER_QUALITY), sizeof(currentHybridQuality), &currentHybridQuality, &dummy), "Failed to query current render quality");
                    if (currentHybridQuality == RPR_RENDER_QUALITY_LOW) {
                        activeRenderQuality = HdRprCoreRenderQualityTokens->Low;
                    } else if (currentHybridQuality == RPR_RENDER_QUALITY_MEDIUM) {
                        activeRenderQuality = HdRprCoreRenderQualityTokens->Medium;
                    } else {
                        activeRenderQuality = HdRprCoreRenderQualityTokens->High;
                    }
                }

                clearAovs = activeRenderQuality != m_currentRenderQuality;
            }

            UpdateSettings(*config);
            config->ResetDirty();
        }
        UpdateCamera(cameraMode, aspectRatioPolicy, instantaneousShutter);
        UpdateAovs(rprRenderParam, enableDenoise, tonemap, gamma, clearAovs);

        m_dirtyFlags = ChangeTracker::Clean;
        if (m_hdCamera) {
            m_hdCamera->CleanDirtyBits();
        }
    }

    rpr_uint GetRprRenderMode(TfToken const& mode) {
        static std::map<TfToken, rpr_render_mode> s_mapping = {
            {HdRprCoreRenderModeTokens->GlobalIllumination, RPR_RENDER_MODE_GLOBAL_ILLUMINATION},
            {HdRprCoreRenderModeTokens->DirectIllumination, RPR_RENDER_MODE_DIRECT_ILLUMINATION},
            {HdRprCoreRenderModeTokens->Wireframe, RPR_RENDER_MODE_WIREFRAME},
            {HdRprCoreRenderModeTokens->MaterialIndex, RPR_RENDER_MODE_MATERIAL_INDEX},
            {HdRprCoreRenderModeTokens->Position, RPR_RENDER_MODE_POSITION},
            {HdRprCoreRenderModeTokens->Normal, RPR_RENDER_MODE_NORMAL},
            {HdRprCoreRenderModeTokens->Texcoord, RPR_RENDER_MODE_TEXCOORD},
            {HdRprCoreRenderModeTokens->AmbientOcclusion, RPR_RENDER_MODE_AMBIENT_OCCLUSION},
            {HdRprCoreRenderModeTokens->Diffuse, RPR_RENDER_MODE_DIFFUSE},
        };

        auto it = s_mapping.find(mode);
        if (it == s_mapping.end()) return RPR_RENDER_MODE_GLOBAL_ILLUMINATION;
        return it->second;
    }

    void UpdateRenderMode(HdRprConfig const& preferences, bool force) {
        if (!preferences.IsDirty(HdRprConfig::DirtyRenderMode) && !force) {
            return;
        }
        m_dirtyFlags |= ChangeTracker::DirtyScene;

        auto& renderMode = preferences.GetCoreRenderMode();

        if (m_rprContextMetadata.pluginType == kPluginNorthstar) {
            if (renderMode == HdRprCoreRenderModeTokens->Contour) {
                if (!m_contourAovs) {
                    m_contourAovs = std::make_unique<ContourRenderModeAovs>();
                    m_contourAovs->normal = CreateAov(HdAovTokens->normal);
                    m_contourAovs->primId = CreateAov(HdAovTokens->primId);
                    m_contourAovs->materialId = CreateAov(HdRprAovTokens->materialId);
                    m_contourAovs->uv = CreateAov(HdRprAovTokens->primvarsSt);
                }

                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_DEBUG_ENABLED, preferences.GetContourDebug()), "Failed to set contour debug");
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_ANTIALIASING, preferences.GetContourAntialiasing()), "Failed to set contour antialiasing");

                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_USE_NORMAL, int(preferences.GetContourUseNormal())), "Failed to set contour use normal");
                if (preferences.GetContourUseNormal()) {
                    RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_LINEWIDTH_NORMAL, preferences.GetContourLinewidthNormal()), "Failed to set contour normal linewidth");
                    RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_NORMAL_THRESHOLD, preferences.GetContourNormalThreshold()), "Failed to set contour normal threshold");
                }

                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_USE_OBJECTID, int(preferences.GetContourUsePrimId())), "Failed to set contour use primId");
                if (preferences.GetContourUsePrimId()) {
                    RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_LINEWIDTH_OBJECTID, preferences.GetContourLinewidthPrimId()), "Failed to set contour primId linewidth");
                }

                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_USE_MATERIALID, int(preferences.GetContourUseMaterialId())), "Failed to set contour use materialId");
                if (preferences.GetContourUseMaterialId()) {
                    RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_LINEWIDTH_MATERIALID, preferences.GetContourLinewidthMaterialId()), "Failed to set contour materialId linewidth");
                }

                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_USE_UV, int(preferences.GetContourUseUv())), "Failed to set contour use UV");
                if (preferences.GetContourUseUv()) {
                    RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_LINEWIDTH_UV, preferences.GetContourLinewidthUv()), "Failed to set contour UV linewidth");
                    RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_CONTOUR_UV_THRESHOLD, preferences.GetContourUvThreshold()), "Failed to set contour UV threshold");
                }

                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_GPUINTEGRATOR, "gpucontour"), "Failed to set gpuintegrator");
                return;
            } else {
                m_contourAovs = nullptr;
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_GPUINTEGRATOR, "gpusimple"), "Failed to set gpuintegrator");
            }
        }

        RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_RENDER_MODE, GetRprRenderMode(renderMode)), "Failed to set render mode");
        if (renderMode == HdRprCoreRenderModeTokens->AmbientOcclusion) {
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_AO_RAY_LENGTH, preferences.GetAmbientOcclusionRadius()), "Failed to set ambient occlusion radius");
        }
    }

    void UpdateTahoeSettings(HdRprConfig const& preferences, bool force) {
        if (preferences.IsDirty(HdRprConfig::DirtyAdaptiveSampling) || force) {
            m_varianceThreshold = preferences.GetAdaptiveSamplingNoiseTreshold();
            m_minSamples = preferences.GetAdaptiveSamplingMinSamples();
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_ADAPTIVE_SAMPLING_THRESHOLD, m_varianceThreshold), "Failed to set as.threshold");
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_ADAPTIVE_SAMPLING_MIN_SPP, m_minSamples), "Failed to set as.minspp");

            if (IsAdaptiveSamplingEnabled()) {
                if (!m_internalAovs.count(HdRprAovTokens->variance)) {
                    if (auto aov = CreateAov(HdRprAovTokens->variance)) {
                        m_internalAovs.emplace(HdRprAovTokens->variance, std::move(aov));
                    } else {
                        TF_RUNTIME_ERROR("Failed to create variance AOV, adaptive sampling will not work");
                    }
                }
            } else {
                m_internalAovs.erase(HdRprAovTokens->variance);
            }

            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }

        if (preferences.IsDirty(HdRprConfig::DirtyQuality) || force) {
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_RECURSION, preferences.GetQualityRayDepth()), "Failed to set max recursion");
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_DIFFUSE, preferences.GetQualityRayDepthDiffuse()), "Failed to set max depth diffuse");
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_GLOSSY, preferences.GetQualityRayDepthGlossy()), "Failed to set max depth glossy");
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_REFRACTION, preferences.GetQualityRayDepthRefraction()), "Failed to set max depth refraction");
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_GLOSSY_REFRACTION, preferences.GetQualityRayDepthGlossyRefraction()), "Failed to set max depth glossy refraction");
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_SHADOW, preferences.GetQualityRayDepthShadow()), "Failed to set max depth shadow");

            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_RAY_CAST_EPSILON, preferences.GetQualityRaycastEpsilon()), "Failed to set ray cast epsilon");
            auto radianceClamp = preferences.GetQualityRadianceClamping() == 0 ? std::numeric_limits<float>::max() : preferences.GetQualityRadianceClamping();
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_RADIANCE_CLAMP, radianceClamp), "Failed to set radiance clamp");

            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_IMAGE_FILTER_RADIUS, preferences.GetQualityImageFilterRadius()), "Failed to set Pixel filter width");

            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }

        if ((preferences.IsDirty(HdRprConfig::DirtyInteractiveMode) ||
            preferences.IsDirty(HdRprConfig::DirtyInteractiveQuality)) || force) {
            m_isInteractive = preferences.GetInteractiveMode();
            auto maxRayDepth = m_isInteractive ? preferences.GetQualityInteractiveRayDepth() : preferences.GetQualityRayDepth();
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_RECURSION, maxRayDepth), "Failed to set max recursion");

            if (m_rprContextMetadata.pluginType == kPluginNorthstar) {
                int downscale = 0;
                if (m_isInteractive) {
                    downscale = preferences.GetQualityInteractiveDownscaleResolution();
                }
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_PREVIEW, uint32_t(downscale)), "Failed to set preview mode");
            } else {
                bool enableDownscale = m_isInteractive && preferences.GetQualityInteractiveDownscaleEnable();
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_PREVIEW, uint32_t(enableDownscale)), "Failed to set preview mode");
            }

            if (preferences.IsDirty(HdRprConfig::DirtyInteractiveMode) || m_isInteractive) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        }

        UpdateRenderMode(preferences, force);

        if (preferences.IsDirty(HdRprConfig::DirtySeed) || force) {
            m_isUniformSeed = preferences.GetUniformSeed();
            m_frameCount = 0;
            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }

        if (m_rprContextMetadata.pluginType == kPluginNorthstar) {
            if (preferences.IsDirty(HdRprConfig::DirtyMotionBlur) || force) {
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_BEAUTY_MOTION_BLUR, uint32_t(preferences.GetBeautyMotionBlurEnable())), "Failed to set beauty motion blur");
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }

            if (preferences.IsDirty(HdRprConfig::DirtyOCIO) || force) {
                std::string ocioConfigPath;

                // OpenColorIO doc recommends to use `OCIO` environment variable to
                // globally define path to OCIO config. See the docs for details about the motivation for this.
                // Houdini handles it in the same while leaving possibility to override it through UI.
                // We allow the OCIO config path to be overridden through render settings.
                if (!GetPath(preferences.GetOcioConfigPath()).empty()) {
                    ocioConfigPath = GetPath(preferences.GetOcioConfigPath());
                } else {
                    ocioConfigPath = TfGetenv("OCIO");
                }

                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_OCIO_CONFIG_PATH, ocioConfigPath.c_str()), "Faled to set OCIO config path");
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_OCIO_RENDERING_COLOR_SPACE, preferences.GetOcioRenderingColorSpace().c_str()), "Faled to set OCIO rendering color space");
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }

            if (preferences.IsDirty(HdRprConfig::DirtyCryptomatte) || force) {
#ifdef RPR_EXR_EXPORT_ENABLED
                m_cryptomatteOutputPath = GetPath(preferences.GetCryptomatteOutputPath());
                m_cryptomattePreviewLayer = preferences.GetCryptomattePreviewLayer();
#else
                if (!preferences.GetCryptomatteOutputPath().empty()) {
                    fprintf(stderr, "Cryptomatte export is not supported: hdRpr compiled without .exr support\n");
                }
#endif // RPR_EXR_EXPORT_ENABLED
            }
        }
    }

    void UpdateHybridSettings(HdRprConfig const& preferences, bool force) {
        if (m_rprContextMetadata.pluginType == kPluginHybridPro) {
            if (preferences.IsDirty(HdRprConfig::DirtyQuality) || force) {
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_RECURSION, preferences.GetQualityRayDepth()), "Failed to set max recursion");
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_DIFFUSE, preferences.GetQualityRayDepthDiffuse()), "Failed to set max depth diffuse");
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_GLOSSY, preferences.GetQualityRayDepthGlossy()), "Failed to set max depth glossy");
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_REFRACTION, preferences.GetQualityRayDepthRefraction()), "Failed to set max depth refraction");
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_GLOSSY_REFRACTION, preferences.GetQualityRayDepthGlossyRefraction()), "Failed to set max depth glossy refraction");

                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_RAY_CAST_EPSILON, preferences.GetQualityRaycastEpsilon()), "Failed to set ray cast epsilon");
                auto radianceClamp = preferences.GetQualityRadianceClamping() == 0 ? std::numeric_limits<float>::max() : preferences.GetQualityRadianceClamping();
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_RADIANCE_CLAMP, radianceClamp), "Failed to set radiance clamp");

                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }

            if ((preferences.IsDirty(HdRprConfig::DirtyInteractiveMode) ||
                preferences.IsDirty(HdRprConfig::DirtyInteractiveQuality)) || force) {
                m_isInteractive = preferences.GetInteractiveMode();
                auto maxRayDepth = m_isInteractive ? preferences.GetQualityInteractiveRayDepth() : preferences.GetQualityRayDepth();
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_RECURSION, maxRayDepth), "Failed to set max recursion");

                if (preferences.IsDirty(HdRprConfig::DirtyInteractiveMode) || m_isInteractive) {
                    m_dirtyFlags |= ChangeTracker::DirtyScene;
                }
            }
        }

        if (preferences.IsDirty(HdRprConfig::DirtyRenderQuality) || force) {
            rpr_uint hybridRenderQuality = -1;
            if (m_currentRenderQuality == HdRprCoreRenderQualityTokens->High) {
                hybridRenderQuality = RPR_RENDER_QUALITY_HIGH;
            } else if (m_currentRenderQuality == HdRprCoreRenderQualityTokens->Medium) {
                hybridRenderQuality = RPR_RENDER_QUALITY_MEDIUM;
            } else if (m_currentRenderQuality == HdRprCoreRenderQualityTokens->Low) {
                hybridRenderQuality = RPR_RENDER_QUALITY_LOW;
            }

            if (hybridRenderQuality != -1) {
                RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_RENDER_QUALITY), hybridRenderQuality), "Fail to set context hybrid render quality");
            }
        }

        if (preferences.IsDirty(HdRprConfig::DirtyHybrid) || force) {
            auto value = preferences.GetHybridTonemapping();
            if (value == HdRprHybridTonemappingTokens->None) {
                RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_TONE_MAPPING), RPR_TONE_MAPPING_NONE), "Failed to set tonemapping");
            } else if (value == HdRprHybridTonemappingTokens->Filmic) {
                RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_TONE_MAPPING), RPR_TONE_MAPPING_FILMIC), "Failed to set tonemapping");
            } else if (value == HdRprHybridTonemappingTokens->Aces) {
                RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_TONE_MAPPING), RPR_TONE_MAPPING_ACES), "Failed to set tonemapping");
            } else if (value == HdRprHybridTonemappingTokens->Reinhard) {
                RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_TONE_MAPPING), RPR_TONE_MAPPING_REINHARD), "Failed to set tonemapping");
            } else if (value == HdRprHybridTonemappingTokens->Photolinear) {
                RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_TONE_MAPPING), RPR_TONE_MAPPING_PHOTO_LINEAR), "Failed to set tonemapping");
            }
        }

        if (preferences.IsDirty(HdRprConfig::DirtyHybrid) || force) {
            auto value = preferences.GetHybridDenoising();
            if (value == HdRprHybridDenoisingTokens->None) {
                RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_PT_DENOISER), RPR_DENOISER_NONE), "Failed to set denoiser");
            } else if (value == HdRprHybridDenoisingTokens->SVGF) {
                RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_PT_DENOISER), RPR_DENOISER_SVGF), "Failed to set denoiser");
            } else if (value == HdRprHybridDenoisingTokens->ASVGF) {
                RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_PT_DENOISER), RPR_DENOISER_ASVGF), "Failed to set denoiser");
            }
        }

        if (preferences.IsDirty(HdRprConfig::DirtyQuality) || force) {
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_RECURSION, preferences.GetQualityRayDepth()), "Failed to set max recursion");
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_DIFFUSE, preferences.GetQualityRayDepthDiffuse()), "Failed to set max depth diffuse");
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_GLOSSY, preferences.GetQualityRayDepthGlossy()), "Failed to set max depth glossy");
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_REFRACTION, preferences.GetQualityRayDepthRefraction()), "Failed to set max depth refraction");
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_GLOSSY_REFRACTION, preferences.GetQualityRayDepthGlossyRefraction()), "Failed to set max depth glossy refraction");
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_DEPTH_SHADOW, preferences.GetQualityRayDepthShadow()), "Failed to set max depth shadow");

            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_RAY_CAST_EPSILON, preferences.GetQualityRaycastEpsilon()), "Failed to set ray cast epsilon");
            auto radianceClamp = preferences.GetQualityRadianceClamping() == 0 ? std::numeric_limits<float>::max() : preferences.GetQualityRadianceClamping();
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_RADIANCE_CLAMP, radianceClamp), "Failed to set radiance clamp");

            m_dirtyFlags |= ChangeTracker::DirtyScene;
        }

        if ((preferences.IsDirty(HdRprConfig::DirtyInteractiveMode) ||
            preferences.IsDirty(HdRprConfig::DirtyInteractiveQuality)) || force) {
            m_isInteractive = preferences.GetInteractiveMode();
            auto maxRayDepth = m_isInteractive ? preferences.GetQualityInteractiveRayDepth() : preferences.GetQualityRayDepth();
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_MAX_RECURSION, maxRayDepth), "Failed to set max recursion");

            if (m_rprContextMetadata.pluginType == kPluginNorthstar) {
                int downscale = 0;
                if (m_isInteractive) {
                    downscale = preferences.GetQualityInteractiveDownscaleResolution();
                }
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_PREVIEW, uint32_t(downscale)), "Failed to set preview mode");
            } else {
                bool enableDownscale = m_isInteractive && preferences.GetQualityInteractiveDownscaleEnable();
                RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_PREVIEW, uint32_t(enableDownscale)), "Failed to set preview mode");
            }

            if (preferences.IsDirty(HdRprConfig::DirtyInteractiveMode) || m_isInteractive) {
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        }

        // RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_RESERVOIR_SAMPLING), 2), "Failed to set reservoir sampling");
        RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_DISPLAY_GAMMA, 2.2f), "Failed to set display gamma");
    }

    void UpdateSettings(HdRprConfig const& preferences, bool force = false) {
        if (preferences.IsDirty(HdRprConfig::DirtySampling) || force) {
            m_maxSamples = preferences.GetMaxSamples();
            if (m_maxSamples < m_numSamples) {
                // Force framebuffers clear to render required number of samples
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        }

        if (m_rprContextMetadata.pluginType == kPluginTahoe ||
            m_rprContextMetadata.pluginType == kPluginNorthstar) {
            UpdateTahoeSettings(preferences, force);
        } else if (RprUsdIsHybrid(m_rprContextMetadata.pluginType)) {
            UpdateHybridSettings(preferences, force);
        }

        if (preferences.IsDirty(HdRprConfig::DirtyAlpha) || force ||
            (m_rprContextMetadata.pluginType == kPluginNorthstar && preferences.IsDirty(HdRprConfig::DirtyRenderMode))) {
            m_isAlphaEnabled = preferences.GetAlphaEnable();

            UpdateColorAlpha(m_colorAov.get());
        }

        if (preferences.IsDirty(HdRprConfig::DirtySession) || force) {
            m_isProgressive = preferences.GetProgressive();
            bool isBatch = preferences.GetRenderMode() == HdRprRenderModeTokens->batch;
            if (m_isBatch != isBatch) {
                m_isBatch = isBatch;
                m_batchREM = isBatch ? std::make_unique<BatchRenderEventManager>() : nullptr;
                m_dirtyFlags |= ChangeTracker::DirtyScene;
            }
        }

        if (preferences.IsDirty(HdRprConfig::DirtySession) || preferences.IsDirty(HdRprConfig::DirtyCryptomatte) || force) {
            if (!m_cryptomatteOutputPath.empty() &&
                (m_isBatch || preferences.GetCryptomatteOutputMode() == HdRprCryptomatteOutputModeTokens->Interactive) &&
                m_rprContextMetadata.pluginType == kPluginNorthstar) {
                if (!m_cryptomatteAovs) {
                    auto cryptomatteAovs = std::make_unique<CryptomatteAovs>();
                    cryptomatteAovs->obj.aov[0] = CreateAov(HdRprAovTokens->cryptomatteObj0);
                    cryptomatteAovs->obj.aov[1] = CreateAov(HdRprAovTokens->cryptomatteObj1);
                    cryptomatteAovs->obj.aov[2] = CreateAov(HdRprAovTokens->cryptomatteObj2);
                    cryptomatteAovs->mat.aov[0] = CreateAov(HdRprAovTokens->cryptomatteMat0);
                    cryptomatteAovs->mat.aov[1] = CreateAov(HdRprAovTokens->cryptomatteMat1);
                    cryptomatteAovs->mat.aov[2] = CreateAov(HdRprAovTokens->cryptomatteMat2);
                    for (int i = 0; i < 3; ++i) {
                        if (!cryptomatteAovs->obj.aov[i] ||
                            !cryptomatteAovs->mat.aov[i]) {
                            cryptomatteAovs = nullptr;
                            break;
                        }
                    }
                    m_cryptomatteAovs = std::move(cryptomatteAovs);
                }
            } else {
                m_cryptomatteAovs = nullptr;
            }
        }
    }

    bool GetRprCameraMode(TfToken const& mode, rpr_camera_mode* out) {
        static std::map<TfToken, rpr_camera_mode> s_mapping = {
            {HdRprCoreCameraModeTokens->LatitudeLongitude360, RPR_CAMERA_MODE_LATITUDE_LONGITUDE_360},
            {HdRprCoreCameraModeTokens->LatitudeLongitudeStereo, RPR_CAMERA_MODE_LATITUDE_LONGITUDE_STEREO},
            {HdRprCoreCameraModeTokens->Cubemap, RPR_CAMERA_MODE_CUBEMAP},
            {HdRprCoreCameraModeTokens->CubemapStereo, RPR_CAMERA_MODE_CUBEMAP_STEREO},
            {HdRprCoreCameraModeTokens->Fisheye, RPR_CAMERA_MODE_FISHEYE},
        };

        auto it = s_mapping.find(mode);
        if (it == s_mapping.end()) return false;
        *out = it->second;
        return true;
    }

    void UpdateCamera(
        RenderSetting<TfToken> const& cameraMode,
        RenderSetting<TfToken> const& aspectRatioPolicy,
        RenderSetting<bool> const& instantaneousShutter) {
        if (!m_hdCamera || !m_camera) {
            return;
        }

        if (cameraMode.isDirty ||
            aspectRatioPolicy.isDirty ||
            instantaneousShutter.isDirty) {
            m_dirtyFlags |= ChangeTracker::DirtyHdCamera;
        }

        if ((m_dirtyFlags & ChangeTracker::DirtyViewport) == 0 &&
            !IsCameraChanged()) {
            return;
        }

        double shutterOpen = 0.0;
        double shutterClose = 0.0;
        if (!instantaneousShutter.value) {
            m_hdCamera->GetShutterOpen(&shutterOpen);
            m_hdCamera->GetShutterClose(&shutterClose);
        }
        double exposure = std::max(shutterClose - shutterOpen, 0.0);

        // Hydra always sample transforms in such a way that
        // starting transform matches shutterOpen and
        // ending transform matches shutterClose
        RPR_ERROR_CHECK(m_camera->SetExposure(1.0f), "Failed to set camera exposure");

        auto setCameraLookAt = [this](GfMatrix4d const& viewMatrix, GfMatrix4d const& inverseViewMatrix) {
            auto& iwvm = inverseViewMatrix;
            auto& wvm = viewMatrix;
            GfVec3f eye(iwvm[3][0], iwvm[3][1], iwvm[3][2]);
            GfVec3f up(wvm[0][1], wvm[1][1], wvm[2][1]);
            GfVec3f n(wvm[0][2], wvm[1][2], wvm[2][2]);
            GfVec3f at(eye - n);
            RPR_ERROR_CHECK(m_camera->LookAt(eye[0], eye[1], eye[2], at[0], at[1], at[2], up[0], up[1], up[2]), "Failed to set camera Look At");
        };

        if (exposure != 0.0 && m_hdCamera->GetTransformSamples().count > 1) {
            auto& transformSamples = m_hdCamera->GetTransformSamples();

            // XXX (RPR): there is no way to sample all transforms via current RPR API
            auto startTransform = transformSamples.values.front() * m_unitSizeTransform;
            auto endTransform = transformSamples.values.back() * m_unitSizeTransform;

            GfVec3f linearMotion, scaleMotion, rotateAxis;
            float rotateAngle;
            GetMotion(startTransform, endTransform, &linearMotion, &scaleMotion, &rotateAxis, &rotateAngle);

            setCameraLookAt(startTransform.GetInverse(), startTransform);
            RPR_ERROR_CHECK(m_camera->SetLinearMotion(linearMotion[0], linearMotion[1], linearMotion[2]), "Failed to set camera linear motion");
            RPR_ERROR_CHECK(m_camera->SetAngularMotion(rotateAxis[0], rotateAxis[1], rotateAxis[2], rotateAngle), "Failed to set camera angular motion");
        } else {
            setCameraLookAt(m_hdCamera->GetTransform().GetInverse() * m_unitSizeTransform, m_hdCamera->GetTransform() * m_unitSizeTransform);
            RPR_ERROR_CHECK(m_camera->SetLinearMotion(0.0f, 0.0f, 0.0f), "Failed to set camera linear motion");
            RPR_ERROR_CHECK(m_camera->SetAngularMotion(1.0f, 0.0f, 0.0f, 0.0f), "Failed to set camera angular motion");
        }

        auto aspectRatio = double(m_viewportSize[0]) / m_viewportSize[1];

#if PXR_VERSION >= 2203
        GfMatrix4d projectionMatrix = m_hdCamera->ComputeProjectionMatrix();
#else
        GfMatrix4d projectionMatrix = m_hdCamera->GetProjectionMatrix();
#endif
        m_cameraProjectionMatrix = CameraUtilConformedWindow(projectionMatrix, m_hdCamera->GetWindowPolicy(), aspectRatio);

        float sensorWidth;
        float sensorHeight;
        float focalLength;

        float nearPlane;
        float farPlane;

        GfVec2f apertureSize;
        GfVec2f apertureOffset(0.0f);
        HdRprCamera::Projection projection;
        if (m_hdCamera->GetFocalLength(&focalLength) &&
            m_hdCamera->GetApertureSize(&apertureSize) &&
            m_hdCamera->GetApertureOffset(&apertureOffset) &&
            m_hdCamera->GetProjection(&projection)) {
            ApplyAspectRatioPolicy(m_viewportSize, aspectRatioPolicy.value, apertureSize);
            sensorWidth = apertureSize[0];
            sensorHeight = apertureSize[1];

            apertureOffset[0] /= apertureSize[0];
            apertureOffset[1] /= apertureSize[1];
        } else {
            bool isOrthographic = round(m_cameraProjectionMatrix[3][3]) == 1.0;
            if (isOrthographic) {
                projection = HdRprCamera::Orthographic;

                GfVec3f ndcTopLeft(-1.0f, 1.0f, 0.0f);
                GfVec3f nearPlaneTrace = m_cameraProjectionMatrix.GetInverse().Transform(ndcTopLeft);

                sensorWidth = std::abs(nearPlaneTrace[0]) * 2.0;
                sensorHeight = std::abs(nearPlaneTrace[1]) * 2.0;

                nearPlane = (1.0 + m_cameraProjectionMatrix[3][2]) / m_cameraProjectionMatrix[2][2];
                farPlane = -(1.0 - m_cameraProjectionMatrix[3][2]) / m_cameraProjectionMatrix[2][2];
            } else {
                projection = HdRprCamera::Perspective;

                sensorWidth = 1.0f;
                sensorHeight = 1.0f / aspectRatio;
                focalLength = m_cameraProjectionMatrix[1][1] / (2.0 * aspectRatio);

                nearPlane = m_cameraProjectionMatrix[3][2] / (m_cameraProjectionMatrix[2][2] - 1);
                farPlane = m_cameraProjectionMatrix[3][2] / (m_cameraProjectionMatrix[2][2] + 1);
            }
        }

		// we need to scale far plane, near plane and focus distance using scene units scale coefficient
        GfRange1f clippingRange(0.01f, 100000000.0f);
        if (m_hdCamera->GetClippingRange(&clippingRange)) {
            RPR_ERROR_CHECK(m_camera->SetNearPlane(clippingRange.GetMin() * m_unitSizeTransform[0][0]), "Failed to set camera near plane");
            RPR_ERROR_CHECK(m_camera->SetFarPlane(clippingRange.GetMax() * m_unitSizeTransform[0][0]), "Failed to set camera far plane");
        }
        else {
            RPR_ERROR_CHECK(m_camera->SetNearPlane(nearPlane * m_unitSizeTransform[0][0]), "Failed to set camera near plane");
            RPR_ERROR_CHECK(m_camera->SetFarPlane(farPlane * m_unitSizeTransform[0][0]), "Failed to set camera far plane");
        }

        RPR_ERROR_CHECK(m_camera->SetLensShift(apertureOffset[0], apertureOffset[1]), "Failed to set camera lens shift");

        rpr_camera_mode rprCameraMode;
        if (GetRprCameraMode(cameraMode.value, &rprCameraMode)) {
            RPR_ERROR_CHECK(m_camera->SetMode(rprCameraMode), "Failed to set camera mode");
        } else if (projection == HdRprCamera::Orthographic) {
            RPR_ERROR_CHECK(m_camera->SetMode(RPR_CAMERA_MODE_ORTHOGRAPHIC), "Failed to set camera mode");
            RPR_ERROR_CHECK(m_camera->SetOrthoWidth(sensorWidth), "Failed to set camera ortho width");
            RPR_ERROR_CHECK(m_camera->SetOrthoHeight(sensorHeight), "Failed to set camera ortho height");
        } else {
            RPR_ERROR_CHECK(m_camera->SetMode(RPR_CAMERA_MODE_PERSPECTIVE), "Failed to set camera mode");

            float focusDistance = 1.0f;
            m_hdCamera->GetFocusDistance(&focusDistance);
            if (focusDistance > 0.0f) {
                RPR_ERROR_CHECK(m_camera->SetFocusDistance(focusDistance * m_unitSizeTransform[0][0]), "Failed to set camera focus distance");
            }

            float fstop = 0.0f;
            m_hdCamera->GetFStop(&fstop);
            bool dofEnabled = fstop != 0.0f && fstop != std::numeric_limits<float>::max();

            if (dofEnabled) {
                uint32_t apertureBlades = m_hdCamera->GetApertureBlades();
                if (apertureBlades < 4 || apertureBlades > 32) {
                    apertureBlades = 16;
                }
                RPR_ERROR_CHECK(m_camera->SetApertureBlades(apertureBlades), "Failed to set camera aperture blades");
            } else {
                fstop = std::numeric_limits<float>::max();
            }
            RPR_ERROR_CHECK(m_camera->SetFStop(fstop), "Failed to set camera FStop");

            // Convert to millimeters
            focalLength *= 1e3f;
            sensorWidth *= 1e3f;
            sensorHeight *= 1e3f;

            RPR_ERROR_CHECK(m_camera->SetFocalLength(focalLength), "Fail to set camera focal length");
            RPR_ERROR_CHECK(m_camera->SetSensorSize(sensorWidth, sensorHeight), "Failed to set camera sensor size");
        }
    }

    VtValue GetAovSetting(TfToken const& settingName, HdRenderPassAovBinding const& aovBinding) {
        auto settingsIt = aovBinding.aovSettings.find(settingName);
        if (settingsIt == aovBinding.aovSettings.end()) {
            return VtValue();
        }
        return settingsIt->second;
    }

    bool GetAovBindingInfo(HdRenderPassAovBinding const& aovBinding, TfToken* aovName, std::string* lpe, HdFormat* format) {
        // Check for UsdRenderVar: take aovName from `sourceName`, format from `dataType`
        auto sourceType = GetAovSetting(UsdRenderTokens->sourceType, aovBinding);
        if (sourceType == UsdRenderTokens->raw) {
            auto name = TfToken(GetAovSetting(UsdRenderTokens->sourceName, aovBinding).Get<std::string>());
            auto& aovDesc = HdRprAovRegistry::GetInstance().GetAovDesc(name);
            if (aovDesc.id != kAovNone && aovDesc.format != HdFormatInvalid) {
                *aovName = name;
                *format = ConvertUsdRenderVarDataType(GetAovSetting(UsdRenderTokens->dataType, aovBinding).Get<TfToken>());
                return *format != HdFormatInvalid;
            } else {
                TF_RUNTIME_ERROR("Unsupported UsdRenderVar sourceName: %s", name.GetText());
            }
        } else if (sourceType == UsdRenderTokens->lpe) {
            auto sourceName = GetAovSetting(UsdRenderTokens->sourceName, aovBinding).Get<std::string>();
            if (sourceName.empty()) {
                return false;
            }

            *format = ConvertUsdRenderVarDataType(GetAovSetting(UsdRenderTokens->dataType, aovBinding).Get<TfToken>());

            if (sourceName == "C.*") {
                // We can use RPR_AOV_COLOR here instead of reserving one of the available LPE AOV slots
                *aovName = HdAovTokens->color;
            } else {
                if (m_rprContextMetadata.pluginType != kPluginNorthstar) {
                    TF_RUNTIME_ERROR("LPE AOV is supported in Northstar only");
                    return false;
                }

                *lpe = sourceName;
            }

            return *format != HdFormatInvalid;
        }

        // Default AOV: aovName from `aovBinding.aovName`, format is controlled by HdRenderBuffer
        auto& aovDesc = HdRprAovRegistry::GetInstance().GetAovDesc(aovBinding.aovName);
        if (aovDesc.id != kAovNone && aovDesc.format != HdFormatInvalid) {
            *aovName = aovBinding.aovName;
            *format = static_cast<HdRprRenderBuffer*>(aovBinding.renderBuffer)->GetFormat();
            return true;
        }

        return false;
    }

    void UpdateAovs(HdRprRenderParam* rprRenderParam, RenderSetting<bool> enableDenoise, RenderSetting<HdRprApiColorAov::TonemapParams> tonemap,  RenderSetting<HdRprApiColorAov::GammaParams> gamma, bool clearAovs) {
        UpdateDenoising(enableDenoise);

        if (tonemap.isDirty) {
            m_colorAov->SetTonemap(tonemap.value);
        }

        if (gamma.isDirty) {
            m_colorAov->SetGamma(gamma.value);
        }

        if (m_dirtyFlags & (ChangeTracker::DirtyAOVBindings | ChangeTracker::DirtyAOVRegistry)) {
            m_resolveData.rawAovs.clear();
            m_resolveData.computedAovs.clear();
            for (auto it = m_aovRegistry.begin(); it != m_aovRegistry.end();) {
                if (auto aov = it->second.lock()) {
                    bool isMultiSampled = aov->GetDesc().multiSampled;

                    // An opinion of a user overrides an AOV descriptor opinion
                    if (auto outputRb = GetOutputRenderBuffer(it->first)) {
                        isMultiSampled = outputRb->isMultiSampled;
                    }

                    if (aov->GetDesc().computed) {
                        m_resolveData.computedAovs.push_back({aov.get(), isMultiSampled});
                    } else {
                        m_resolveData.rawAovs.push_back({aov.get(), isMultiSampled});
                    }
                    ++it;
                } else {
                    it = m_aovRegistry.erase(it);
                }
            }
        }

        if (m_dirtyFlags & ChangeTracker::DirtyViewport) {
            m_resolveData.ForAllAovs([this](ResolveData::AovEntry const& e) {
                e.aov->Resize(m_viewportSize[0], m_viewportSize[1], e.aov->GetFormat());
            });

            // If AOV bindings are dirty then we already committed HdRprRenderBuffers, see SetAovBindings
            if ((m_dirtyFlags & ChangeTracker::DirtyAOVBindings) == 0) {
                for (auto& outputRb : m_outputRenderBuffers) {
                    auto rprRenderBuffer = static_cast<HdRprRenderBuffer*>(outputRb.aovBinding->renderBuffer);
                    outputRb.mappedData = rprRenderBuffer->GetPointerForWriting();
                    outputRb.mappedDataSize = HdDataSizeOfFormat(rprRenderBuffer->GetFormat()) * rprRenderBuffer->GetWidth() * rprRenderBuffer->GetHeight();
                }
            }
        }

        if (m_dirtyFlags & ChangeTracker::DirtyScene ||
            m_dirtyFlags & ChangeTracker::DirtyAOVRegistry ||
            m_dirtyFlags & ChangeTracker::DirtyAOVBindings ||
            m_dirtyFlags & ChangeTracker::DirtyViewport ||
            IsCameraChanged()) {
            clearAovs = true;
        }

        auto rprApi = rprRenderParam->GetRprApi();
        m_resolveData.ForAllAovs([=](ResolveData::AovEntry const& e) {
            e.aov->Update(rprApi, m_rifContext.get());
        });

        if (clearAovs) {
            Restart();
        }
    }

    void UpdateDenoising(RenderSetting<bool> enableDenoise) {
        // Disable denoiser to prevent possible crashes due to incorrect AI models
        if (!m_rifContext || m_rifContext->GetModelPath().empty()) {
            return;
        }

        if (!enableDenoise.isDirty ||
            m_isDenoiseEnabled == enableDenoise.value) {
            return;
        }

        m_isDenoiseEnabled = enableDenoise.value;
        if (!m_isDenoiseEnabled) {
            m_colorAov->DeinitDenoise(m_rifContext.get());
            return;
        }

        rif::FilterType filterType = rif::FilterType::EawDenoise;
        if (RprUsdIsGpuUsed(m_rprContextMetadata)) {
            filterType = rif::FilterType::AIDenoise;
        }

        if (filterType == rif::FilterType::EawDenoise) {
            m_colorAov->InitEAWDenoise(CreateAov(HdRprAovTokens->albedo),
                                     CreateAov(HdAovTokens->normal),
                                     CreateAov(HdRprGetCameraDepthAovName()),
                                     CreateAov(HdAovTokens->primId),
                                     CreateAov(HdRprAovTokens->worldCoordinate));
        } else {
            m_colorAov->InitAIDenoise(CreateAov(HdRprAovTokens->albedo),
                                    CreateAov(HdAovTokens->normal),
                                    CreateAov(HdRprGetCameraDepthAovName()));
        }
    }

    using RenderUpdateCallbackFunc = void (*)(float progress, void* userData);
    void EnableRenderUpdateCallback(RenderUpdateCallbackFunc rucFunc) {
        if (m_rprContextMetadata.pluginType != kPluginNorthstar) {
            m_isRenderUpdateCallbackEnabled = false;
            return;
        }

        m_rucData.rprApi = this;
        RPR_ERROR_CHECK_THROW(m_rprContext->SetParameter(RPR_CONTEXT_RENDER_UPDATE_CALLBACK_FUNC, (void*)rucFunc), "Failed to set northstar RUC func");
        RPR_ERROR_CHECK_THROW(m_rprContext->SetParameter(RPR_CONTEXT_RENDER_UPDATE_CALLBACK_DATA, &m_rucData), "Failed to set northstar RUC data");
        m_isRenderUpdateCallbackEnabled = true;
    }

    bool EnableAborting() {
        m_isAbortingEnabled.store(true);

        // If abort was called when it was disabled, abort now
        if (m_abortRender) {
            AbortRender();
            return true;
        }
        return false;
    }

    void IncrementFrameCount(bool isAdaptiveSamplingEnabled) {
        if (RprUsdIsHybrid(m_rprContextMetadata.pluginType)) {
            return;
        }

        uint32_t frameCount = m_frameCount++;

        // XXX: When adaptive sampling is enabled,
        // Tahoe requires RPR_CONTEXT_FRAMECOUNT to be set to 0 on the very first sample,
        // otherwise internal adaptive sampling buffers is never reset
        if (m_rprContextMetadata.pluginType == kPluginTahoe &&
            isAdaptiveSamplingEnabled && m_numSamples == 0) {
            frameCount = 0;
        }

        RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_FRAMECOUNT, frameCount), "Failed to set framecount");
    }

    static void BatchRenderUpdateCallback(float progress, void* dataPtr) {
        auto data = static_cast<RenderUpdateCallbackData*>(dataPtr);

        if (data->rprApi->m_numSamples == 0) {
            data->rprApi->EnableAborting();
        }

        if (data->rprApi->m_resolveMode == kResolveInRenderUpdateCallback) {
            // In batch, we do resolve from RUC only by request
            if (data->rprApi->m_batchREM->IsResolveRequested()) {
                data->rprApi->ResolveFramebuffers();
                data->rprApi->m_batchREM->OnResolve();
            }
        }

        data->previousProgress = progress;
    }

    bool CommonRenderImplPrologue() {
        if (m_numSamples == 0) {
            // Default resolve mode
            m_resolveMode = kResolveAfterRender;

            // When we want to have a uniform seed across all frames,
            // we need to make sure that RPR_CONTEXT_FRAMECOUNT sequence is the same for all of them
            if (m_isUniformSeed) {
                m_frameCount = 0;
            }

            // Disable aborting on the very first sample
            //
            // Ideally, aborting the first sample should not be the problem.
            // We would like to be able to abort it: to reduce response time,
            // to avoid doing calculations that may be discarded by the following changes.
            // But in reality, aborting the very first sample may cause crashes or
            // unwanted behavior when we will call rprContextRender next time.
            // So until RPR core fixes these issues, we are not aborting the first sample.
            m_isAbortingEnabled.store(false);
        }
        m_abortRender.store(false);

        // If the changes that were made by the user did not reset our AOVs,
        // we can just resolve them to the current render buffers and we are done with the rendering
        if (IsConverged()) {
            ResolveFramebuffers();
            return false;
        }

        return true;
    }

    uint32_t cryptomatte_avoid_bad_float_hash(uint32_t hash) {
        // from Cryptomatte Specification version 1.2.0
        // This is for avoiding nan, inf, subnormals
        // 
        // if all exponent bits are 0 (subnormals, +zero, -zero) set exponent to 1
        // if all exponent bits are 1 (NaNs, +inf, -inf) set exponent to 254
        uint32_t exponent = hash >> 23 & 255; // extract exponent (8 bits)
        if (exponent == 0 || exponent == 255) {
            hash ^= 1 << 23; // toggle bit
        }
        return hash;
    }
    std::string cryptomatte_hash_name(const char* name, size_t size) {
        uint32_t m3hash = 0;
        MurmurHash3_x86_32(name, size, 0, &m3hash);
        m3hash = cryptomatte_avoid_bad_float_hash(m3hash);
        return TfStringPrintf("%08x", m3hash);
    }
    template <size_t size>
    std::string cryptomatte_hash_name(char (&name)[size]) {
        return cryptomatte_hash_name(name, size);
    }
    std::string cryptomatte_hash_name(std::string const& name) {
        return cryptomatte_hash_name(name.c_str(), name.size());
    }

    void SaveCryptomatte() {
#ifdef RPR_EXR_EXPORT_ENABLED
        if (m_cryptomatteAovs &&
            m_numSamples == m_maxSamples) {

            std::string outputPath = m_cryptomatteOutputPath;
            std::string filename = TfGetBaseName(outputPath);
            if (filename.empty()) {
                TF_WARN("Cryptomatte output path should be a path to .exr file");
                outputPath = TfStringCatPaths(outputPath, "cryptomatte.exr");
            } else if (!TfStringEndsWith(outputPath, ".exr")) {
                TF_WARN("Cryptomatte output path should be a path to .exr file");
                outputPath += ".exr";
            }

            if (!CreateIntermediateDirectories(outputPath)) {
                fprintf(stderr, "Failed to save cryptomatte aov: cannot create intermediate directories - %s\n", outputPath.c_str());
                return;
            }

            // Generate manifests
            std::string objectManifestEncoded;
            std::string materialManifestEncoded;

            if (auto shapes = RprUsdGetListInfo<rpr_shape>(m_rprContext.get(), RPR_CONTEXT_LIST_CREATED_SHAPES)) {
                json objectManifest;
                json materialManifest;
                for (size_t iShape = 0; iShape < shapes.size; ++iShape) {
                    auto shape = RprUsdGetRprObject<rpr::Shape>(shapes.data[iShape]);
                    if (auto name = RprUsdGetListInfo<char>(shape, RPR_SHAPE_NAME)) {
                        objectManifest[name.data.get()] = cryptomatte_hash_name(name.data.get(), name.size - 1);
                    }

                    if (rpr_material_node rprMaterialNode = RprUsdGetInfo<rpr_material_node>(shape, RPR_SHAPE_MATERIAL)) {
                        auto name = RprUsdGetListInfo<char>(
                            [rprMaterialNode](size_t size, void* data, size_t* size_ret) {
                                return rprMaterialNodeGetInfo(rprMaterialNode, RPR_MATERIAL_NODE_NAME, size, data, size_ret);
                            }
                        );
                        if (name) {
                            materialManifest[name.data.get()] = cryptomatte_hash_name(name.data.get(), name.size - 1);
                        }
                    }
                }
                objectManifestEncoded = objectManifest.dump();
                materialManifestEncoded = materialManifest.dump();
            }

            try {
                namespace exr = OPENEXR_IMF_NAMESPACE;
                exr::FrameBuffer exrFb;
                exr::Header exrHeader(m_viewportSize[0], m_viewportSize[1]);
                exrHeader.compression() = exr::ZIPS_COMPRESSION;

                const int kNumComponents = 4;
                const char* kComponentNames[kNumComponents] = {".R", ".G", ".B", ".A"};

                size_t linesize = m_viewportSize[0] * kNumComponents * sizeof(float);
                size_t layersize = m_viewportSize[1] * linesize;
                std::unique_ptr<char[]> flipBuffer;
                if (m_isOutputFlipped) {
                    flipBuffer = std::make_unique<char[]>(layersize);
                }

                std::vector<std::unique_ptr<char[]>> cryptomatteLayers;
                std::vector<std::unique_ptr<GfVec4f[]>> previewLayers;

                auto addCryptomatte = [&](const char* name, CryptomatteAov const& cryptomatte, std::string const& manifest) {
                    std::string nameHash = cryptomatte_hash_name(name).substr(0, 7);
                    std::string cryptoPrefix = "cryptomatte/" + nameHash + "/";
                    exrHeader.insert(cryptoPrefix + "name", exr::StringAttribute(name));
                    exrHeader.insert(cryptoPrefix + "hash", exr::StringAttribute("MurmurHash3_32"));
                    exrHeader.insert(cryptoPrefix + "conversion", exr::StringAttribute("uint32_to_float32"));
                    exrHeader.insert(cryptoPrefix + "manifest", exr::StringAttribute(manifest));

                    auto addLayer = [&](const char* layerName, char* basePtr) {
                        for (int iComponent = 0; iComponent < kNumComponents; ++iComponent) {
                            std::string channelName = TfStringPrintf("%s%s", layerName, kComponentNames[iComponent]);
                            exrHeader.channels().insert(channelName.c_str(), exr::Channel(exr::FLOAT));

                            char* dataPtr = &basePtr[iComponent * sizeof(float)];
                            size_t xStride = kNumComponents * sizeof(float);
                            size_t yStride = xStride * m_viewportSize[0];
                            exrFb.insert(channelName.c_str(), exr::Slice(exr::FLOAT, dataPtr, xStride, yStride));
                        }
                    };

                    for (int i = 0; i < 3; ++i) {
                        auto rprLayer = cryptomatte.aov[i]->GetResolvedFb();
                        cryptomatteLayers.push_back(std::make_unique<char[]>(layersize));
                        if (!rprLayer->GetData(cryptomatteLayers.back().get(), layersize)) {
                            throw std::runtime_error("failed to get RPR fb data");
                        }

                        if (m_isOutputFlipped) {
                            for (int y = 0; y < m_viewportSize[1]; ++y) {
                                void* src = &cryptomatteLayers.back()[y * linesize];
                                void* dst = &flipBuffer[(m_viewportSize[1] - y - 1) * linesize];
                                std::memcpy(dst, src, linesize);
                            }
                            std::swap(flipBuffer, cryptomatteLayers.back());
                        }

                        std::string layerName = TfStringPrintf("%s0%d", name, i);
                        addLayer(layerName.c_str(), cryptomatteLayers.back().get());
                    }

                    if (m_cryptomattePreviewLayer) {
                        size_t numPixels = m_viewportSize[0] * m_viewportSize[1];
                        previewLayers.push_back(std::make_unique<GfVec4f[]>(numPixels));
                        std::memset(previewLayers.back().get(), 0, numPixels * sizeof(GfVec4f));

                        size_t channelOffset = cryptomatteLayers.size() - 3;

                        GfVec4f* previewLayer = previewLayers.back().get();
                        WorkParallelForN(numPixels,
                            [&](size_t begin, size_t end) {
                                for (size_t iChannel = 0; iChannel < 3; ++iChannel) {
                                    auto channelData = cryptomatteLayers[channelOffset + iChannel].get();
                                    for (size_t iPixel = begin; iPixel < end; ++iPixel) {
                                        size_t offset = iPixel * kNumComponents * sizeof(float);

                                        uint32_t id;
                                        float contrib;
                                        UnalignedRead(channelData, &offset, &id);
                                        UnalignedRead(channelData, &offset, &contrib);
                                        GfVec4f color = ColorizeId(id) * contrib;

                                        UnalignedRead(channelData, &offset, &id);
                                        UnalignedRead(channelData, &offset, &contrib);
                                        color += ColorizeId(id) * contrib;

                                        previewLayer[iPixel] += color;
                                    }
                                }
                            }
                        );

                        addLayer(name, (char*)previewLayer);
                    }
                };

                addCryptomatte("CryptoObject", m_cryptomatteAovs->obj, objectManifestEncoded);
                addCryptomatte("CryptoMaterial", m_cryptomatteAovs->mat, materialManifestEncoded);

                ArchUnlinkFile(outputPath.c_str());
                exr::OutputFile exrFile(outputPath.c_str(), exrHeader);
                exrFile.setFrameBuffer(exrFb);
                exrFile.writePixels(m_viewportSize[1]);
            } catch (std::exception& e) {
                fprintf(stderr, "Failed to save cryptomatte: %s", e.what());
            }
        }
#endif // RPR_EXR_EXPORT_ENABLED
    }

    void BatchRenderImpl(HdRprRenderThread* renderThread) {
        if (!CommonRenderImplPrologue()) {
            return;
        }

        auto renderScope = m_batchREM->EnterRenderScope();

        EnableRenderUpdateCallback(BatchRenderUpdateCallback);

        // Also, we try to maximize the number of samples rendered with one rprContextRender call.
        bool isMaximizingContextIterations = false;

        // In a batch session, we disable resolving of all samples except the first and last.
        // Also, if the user will keep progressive mode on, we support snapshoting (resolving intermediate renders)
        //
        // User might decide to completely disable progress updates to maximize performance.
        // In this case, snapshoting is not available.
        if (m_isProgressive) {
            // We can keep the ability to log progress and do snapshots
            // while maximizing RPR_CONTEXT_ITERATIONS, only if RUC is supported.
            if (m_isRenderUpdateCallbackEnabled) {
                isMaximizingContextIterations = true;
            }
        } else {
            // If the user is willing to sacrifice progress logging and snapshots,
            // we minimize the amount of chore work needed to get renders.
            isMaximizingContextIterations = true;
            if (m_isRenderUpdateCallbackEnabled) {
                m_isRenderUpdateCallbackEnabled = false;
                RPR_ERROR_CHECK_THROW(m_rprContext->SetParameter(RPR_CONTEXT_RENDER_UPDATE_CALLBACK_FUNC, (void*)nullptr), "Failed to disable RUC func");
            }
        }

        if (m_contourAovs) {
            // In contour rendering mode we must render with RPR_CONTEXT_ITERATIONS=1
            if (isMaximizingContextIterations) {
                isMaximizingContextIterations = false;
                if (m_isRenderUpdateCallbackEnabled) {
                    m_isRenderUpdateCallbackEnabled = false;
                    RPR_ERROR_CHECK_THROW(m_rprContext->SetParameter(RPR_CONTEXT_RENDER_UPDATE_CALLBACK_FUNC, (void*)nullptr), "Failed to disable RUC func");
                }
            }
        }

        // Though if adaptive sampling is enabled in a batch session we first render m_minSamples samples
        // and after that, if needed, render 1 sample at a time because we want to check the current amount of
        // active pixels as often as possible
        const bool isAdaptiveSamplingEnabled = IsAdaptiveSamplingEnabled();
        const bool isActivePixelCountCheckRequired = isAdaptiveSamplingEnabled && m_rprContextMetadata.pluginType == kPluginTahoe;

        // In a batch session, we do denoise once at the end
        auto rprApi = static_cast<HdRprRenderParam*>(m_delegate->GetRenderParam())->GetRprApi();
        if (m_isDenoiseEnabled) {
            m_colorAov->SetDenoise(false, rprApi, m_rifContext.get());
        }

        while (!IsConverged()) {
            if (renderThread->IsStopRequested()) {
                break;
            }

            IncrementFrameCount(isAdaptiveSamplingEnabled);

            auto startTime = std::chrono::high_resolution_clock::now();

            m_rucData.previousProgress = -1.0f;
            auto status = m_rprContext->Render();
            m_rucData.previousProgress = -1.0f;

            m_frameRenderTotalTime += std::chrono::high_resolution_clock::now() - startTime;

            if (status != RPR_SUCCESS && status != RPR_ERROR_ABORTED) {
                RPR_ERROR_CHECK(status, "Failed to render", m_rprContext.get());
                break;
            }

            if (status == RPR_ERROR_ABORTED) {
                break;
            }

            // As soon as the first sample has been rendered, we enable aborting
            m_isAbortingEnabled.store(true);

            m_numSamples += m_numSamplesPerIter;

            if (m_resolveMode == kResolveAfterRender) {
                // In batch mode resolve only the first and the last frames or
                // when the host app requested it explicitly
                if (m_isFirstSample || m_batchREM->IsResolveRequested()) {
                    ResolveFramebuffers();
                    m_batchREM->OnResolve();
                }
            }

            if (isMaximizingContextIterations) {
                int oldNumSamplesPerIter = m_numSamplesPerIter;

                // When singlesampled AOVs already rendered, we can fire up rendering of as many samples as possible
                if (m_numSamples == 1) {
                    // Render as many samples as possible per Render call
                    if (isActivePixelCountCheckRequired) {
                        m_numSamplesPerIter = m_minSamples - m_numSamples;
                    } else {
                        m_numSamplesPerIter = m_maxSamples - m_numSamples;
                    }

                    // And disable resolves after render if possible
                    if (m_isRenderUpdateCallbackEnabled) {
                        m_resolveMode = kResolveInRenderUpdateCallback;
                    }
                } else {
                    // When adaptive sampling is enabled, after reaching m_minSamples we want query RPR_CONTEXT_ACTIVE_PIXEL_COUNT each sample
                    if (isActivePixelCountCheckRequired && m_numSamples == m_minSamples) {
                        m_numSamplesPerIter = 1;
                        isMaximizingContextIterations = false;
                    }
                }

                if (m_numSamplesPerIter != oldNumSamplesPerIter) {
                    // Make sure we will not oversample the image
                    int numSamplesLeft = m_maxSamples - m_numSamples;
                    m_numSamplesPerIter = std::min(m_numSamplesPerIter, numSamplesLeft);
                    if (m_numSamplesPerIter > 0) {
                        RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_ITERATIONS, m_numSamplesPerIter), "Failed to set context iterations");
                    }
                }
            }

            if (isActivePixelCountCheckRequired && m_numSamples >= m_minSamples &&
                RPR_ERROR_CHECK(m_rprContext->GetInfo(RPR_CONTEXT_ACTIVE_PIXEL_COUNT, sizeof(m_activePixels), &m_activePixels, NULL), "Failed to query active pixels")) {
                m_activePixels = -1;
            }
        }

        if (m_isDenoiseEnabled) {
            m_colorAov->SetDenoise(true, rprApi, m_rifContext.get());
        }

        ResolveFramebuffers();
    }

    static void RenderUpdateCallback(float progress, void* dataPtr) {
        auto data = static_cast<RenderUpdateCallbackData*>(dataPtr);

        if (data->rprApi->m_numSamples == 0 &&
            !data->rprApi->m_isInteractive) {
            data->rprApi->EnableAborting();
        }

        if (data->rprApi->m_resolveMode == kResolveInRenderUpdateCallback) {
            static const float kResolveFrequency = 0.1f;
            int previousStep = static_cast<int>(data->previousProgress / kResolveFrequency);
            int currentStep = static_cast<int>(progress / kResolveFrequency);
            if (currentStep != previousStep) {
                data->rprApi->ResolveFramebuffers();
            }
        }

        data->previousProgress = progress;
    }

    void RenderImpl(HdRprRenderThread* renderThread) {
        if (!CommonRenderImplPrologue()) {
            return;
        }

        EnableRenderUpdateCallback(RenderUpdateCallback);

        const bool progressivelyIncreaseSamplesPerIter =
            // Progressively increasing RPR_CONTEXT_ITERATIONS makes sense only for Northstar
            // because, first, it highly improves its performance (internal optimization)
            // and, second, it supports render update callback that allows us too resolve intermediate results
            m_rprContextMetadata.pluginType == kPluginNorthstar &&
            // in interactive mode we want to be able to abort ASAP
            !m_isInteractive &&
            // in contour rendering mode we must render only with RPR_CONTEXT_ITERATIONS=1
            !m_contourAovs;

        auto rprApi = static_cast<HdRprRenderParam*>(m_delegate->GetRenderParam())->GetRprApi();
        int iteration = 0;

        while (!IsConverged()) {
            // In interactive mode, always render at least one frame, otherwise
            // disturbing full-screen-flickering will be visible or
            // viewport will not update because of fast exit due to abort
            const bool forceRender = m_isInteractive && m_numSamples == 0;

            renderThread->WaitUntilPaused();
            if (renderThread->IsStopRequested() && !forceRender) {
                break;
            }

            IncrementFrameCount(IsAdaptiveSamplingEnabled());

            if (progressivelyIncreaseSamplesPerIter) {
                // 1, 1, 2, 4, 8, ...
                int numSamplesPerIter = std::max(int(pow(2, int(log2(m_numSamples)))), 1);

                // Make sure we will not oversample the image
                int numSamplesLeft = std::min(numSamplesPerIter, m_maxSamples - m_numSamples);
                numSamplesPerIter = numSamplesLeft > 0 ? numSamplesLeft : numSamplesPerIter;
                if (m_numSamplesPerIter != numSamplesPerIter) {
                    m_numSamplesPerIter = numSamplesPerIter;
                    RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_ITERATIONS, m_numSamplesPerIter), "Failed to set context iterations");

                    if (m_isRenderUpdateCallbackEnabled) {
                        // Enable resolves in the render update callback after RPR_CONTEXT_ITERATIONS gets high enough
                        // to get interactive updates even when RPR_CONTEXT_ITERATIONS huge
                        if (m_numSamplesPerIter >= 32) {
                            m_resolveMode = kResolveInRenderUpdateCallback;
                        }
                    }
                }
            }

            auto startTime = std::chrono::high_resolution_clock::now();

            m_rucData.previousProgress = -1.0f;
            auto status = m_rprContext->Render();

            m_frameRenderTotalTime += std::chrono::high_resolution_clock::now() - startTime;

            if (status != RPR_SUCCESS && status != RPR_ERROR_ABORTED) {
                RPR_ERROR_CHECK(status, "Failed to render", m_rprContext.get());
                break;
            }

            if (status == RPR_ERROR_ABORTED && !forceRender) {
                break;
            }

            bool doDenoisedResolve = false;
            if (m_colorAov) {
                if (m_isDenoiseEnabled) {
                    ++iteration;
                    if (iteration >= m_denoiseMinIter) {
                        int relativeIter = iteration - m_denoiseMinIter;
                        if (relativeIter % m_denoiseIterStep == 0) {
                            doDenoisedResolve = true;
                        }
                    }

                    // Always force denoise on the last sample because it's quite hard to match
                    // the max amount of samples and denoise controls (min iter and iter step)
                    if (m_numSamples + m_numSamplesPerIter == m_maxSamples) {
                        doDenoisedResolve = true;
                    }
                }

                m_colorAov->SetDenoise(doDenoisedResolve, rprApi, m_rifContext.get());
            }

            if (m_resolveMode == kResolveAfterRender ||
                doDenoisedResolve) {
                ResolveFramebuffers();
            }

            if (IsAdaptiveSamplingEnabled() && m_numSamples >= m_minSamples &&
                RPR_ERROR_CHECK(m_rprContext->GetInfo(RPR_CONTEXT_ACTIVE_PIXEL_COUNT, sizeof(m_activePixels), &m_activePixels, NULL), "Failed to query active pixels")) {
                m_activePixels = -1;
            }

            // As soon as the first sample has been rendered, we enable aborting
            m_isAbortingEnabled.store(true);

            m_numSamples += m_numSamplesPerIter;
        }
    }

#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
    void InteropRenderImpl(HdRprRenderThread* renderThread) {
        if (!RprUsdIsHybrid(m_rprContextMetadata.pluginType)) {
            TF_CODING_ERROR("InteropRenderImpl should be called for Hybrid plugin only");
            return;
        }
        bool isRendered = false;
        while (true) {
            // In interactive mode, always render at least one frame, otherwise
            // disturbing full-screen-flickering will be visible or
            // viewport will not update because of fast exit due to abort
            const bool forceRender = m_isInteractive && m_numSamples == 0;

            renderThread->WaitUntilPaused();
            if (renderThread->IsStopRequested() && !forceRender) {
                break;
            }

            if (!m_vulkanInteropBufferReady)
            {
                m_rucData.previousProgress = -1.0f;
                rpr::Status status = m_rprContext->Render();
                if (rpr::Status::RPR_SUCCESS == status) {
                    m_numSamples += m_numSamplesPerIter;
                    rpr_int status = m_rprContextFlushFrameBuffers(rpr::GetRprObject(m_rprContext.get()));
                    if (RPR_SUCCESS == status) {
                        m_vulkanInteropBufferReady = true;
                    }
                    else {
                        TF_WARN("rprContextFlushFrameBuffers returns: %d", status);
                    }
                }
                else {
                    TF_WARN("rprContextRender returns: %d", status);
                }
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT

    void RenderFrame(HdRprRenderThread* renderThread) {
        if (!m_rprContext) {
            return;
        }

        try {
            Update();
        } catch (std::runtime_error const& e) {
            TF_RUNTIME_ERROR("Failed to update: %s", e.what());
            m_dirtyFlags = ChangeTracker::Clean;
            if (m_hdCamera) {
                m_hdCamera->CleanDirtyBits();
            }
            return;
        }

        if (!m_rprSceneExportPath.empty()) {
            return ExportRpr();
        }

        if (m_aovRegistry.empty()) {
            return;
        }

        if (m_state == kStateRender) {
            try {
                if (m_isBatch) {
                    BatchRenderImpl(renderThread);
                } else {
                    if (RprUsdIsHybrid(m_rprContextMetadata.pluginType) &&
                        m_rprContextMetadata.interopInfo) {
#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
                        InteropRenderImpl(renderThread);
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
                    } else {
                        RenderImpl(renderThread);
                    }
                }
                SaveCryptomatte();
            } catch (std::runtime_error const& e) {
                TF_RUNTIME_ERROR("Failed to render frame: %s", e.what());
            }
        } else if (m_state == kStateRestartRequired) {
            {
                RprUsdConfig* config;
                auto configLock = RprUsdConfig::GetInstance(&config);

                if (config->IsRestartWarningEnabled()) {
                    std::string message =
                        R"(Restart required when you change "Render Device" or "Render Quality" (To "Full" or vice versa).
You can revert your changes now and you will not lose any rendering progress.
You can restart renderer by pressing "RPR Persp" - "Restart Render".

Don't show this message again?
)";

                    if (HdRprShowMessage("Restart required", message)) {
                        config->SetRestartWarning(false);
                    }
                }
            }

            PlugPluginPtr plugin = PLUG_THIS_PLUGIN;
            auto imagesPath = PlugFindPluginResource(plugin, "images", false);
            auto path = imagesPath + "/restartRequired.png";
            if (!RenderImage(path)) {
                fprintf(stderr, "Please restart render\n");
            }
        }
    }

    void ExportRpr() {
#ifdef RPR_LOADSTORE_AVAILABLE

#ifdef BUILD_AS_HOUDINI_PLUGIN
        if (HOM().isApprentice()) {
            fprintf(stderr, "Cannot export .rpr from Apprentice");
            return;
        }
#endif // BUILD_AS_HOUDINI_PLUGIN

        // Houdini does not resolve the relative path to an inexisting file, so we need to do it manually.
        if (TfIsRelativePath(m_rprSceneExportPath)) {
            std::string usdFilename = m_delegate->GetRenderSetting(_tokens->usdFilename, std::string());
            if (!usdFilename.empty()) {
                m_rprSceneExportPath = TfNormPath(TfGetPathName(usdFilename) + "/" + m_rprSceneExportPath);
            }
        }

        if (!CreateIntermediateDirectories(m_rprSceneExportPath)) {
            fprintf(stderr, "Failed to create .rpr export output directory\n");
        }

        uint32_t currentYFlip;
        if (m_isOutputFlipped) {
            currentYFlip = RprUsdGetInfo<uint32_t>(m_rprContext.get(), RPR_CONTEXT_Y_FLIP);

            currentYFlip = !currentYFlip;
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_Y_FLIP, currentYFlip), "Failed to set context Y FLIP parameter");
        }

        unsigned int rprsFlags = 0;
        if (!m_rprExportAsSingleFile) {
            rprsFlags |= RPRLOADSTORE_EXPORTFLAG_EXTERNALFILES;
        }
        if (m_rprExportUseImageCache) {
            rprsFlags |= RPRLOADSTORE_EXPORTFLAG_USE_IMAGE_CACHE;
        }

        auto rprContextHandle = rpr::GetRprObject(m_rprContext.get());
        auto rprSceneHandle = rpr::GetRprObject(m_scene.get());
        if (RPR_ERROR_CHECK(rprsExport(m_rprSceneExportPath.c_str(), rprContextHandle, rprSceneHandle, 0, nullptr, nullptr, 0, nullptr, nullptr, rprsFlags, nullptr), "Failed to export .rpr file")) {
            return;
        }

        if (m_isOutputFlipped) {
            currentYFlip = !currentYFlip;
            RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_Y_FLIP, currentYFlip), "Failed to set context Y FLIP parameter");
        }

        TfStringReplace(m_rprSceneExportPath, ".rpr", ".json");
        auto configFilename = m_rprSceneExportPath.substr(0, m_rprSceneExportPath.size() - 4) + ".json";
        std::ofstream configFile(configFilename);
        if (!configFile.is_open()) {
            fprintf(stderr, "Failed to create config file: %s\n", configFilename.c_str());
            return;
        }

        try {
            json config;
            config["width"] = m_viewportSize[0];
            config["height"] = m_viewportSize[1];
            config["iterations"] = m_maxSamples;
            config["batchsize"] = m_maxSamples;

            auto basename = TfStringGetBeforeSuffix(TfGetBaseName(m_rprSceneExportPath));
            config["output"] = basename + ".exr";
            config["output.json"] = basename + ".output.json";

            static std::map<rpr_creation_flags, const char*> kRprsContextFlags = {
                {RPR_CREATION_FLAGS_ENABLE_CPU, "cpu"},
                {RPR_CREATION_FLAGS_ENABLE_DEBUG, "debug"},
                {RPR_CREATION_FLAGS_ENABLE_GPU0, "gpu0"},
                {RPR_CREATION_FLAGS_ENABLE_GPU1, "gpu1"},
                {RPR_CREATION_FLAGS_ENABLE_GPU2, "gpu2"},
                {RPR_CREATION_FLAGS_ENABLE_GPU3, "gpu3"},
                {RPR_CREATION_FLAGS_ENABLE_GPU4, "gpu4"},
                {RPR_CREATION_FLAGS_ENABLE_GPU5, "gpu5"},
                {RPR_CREATION_FLAGS_ENABLE_GPU6, "gpu6"},
                {RPR_CREATION_FLAGS_ENABLE_GPU7, "gpu7"},
                {RPR_CREATION_FLAGS_ENABLE_GPU8, "gpu8"},
                {RPR_CREATION_FLAGS_ENABLE_GPU9, "gpu9"},
                {RPR_CREATION_FLAGS_ENABLE_GPU10, "gpu10"},
                {RPR_CREATION_FLAGS_ENABLE_GPU11, "gpu11"},
                {RPR_CREATION_FLAGS_ENABLE_GPU12, "gpu12"},
                {RPR_CREATION_FLAGS_ENABLE_GPU13, "gpu13"},
                {RPR_CREATION_FLAGS_ENABLE_GPU14, "gpu14"},
                {RPR_CREATION_FLAGS_ENABLE_GPU15, "gpu15"},
            };

            json context;
            auto creationFlags = RprUsdGetInfo<uint32_t>(m_rprContext.get(), RPR_CONTEXT_CREATION_FLAGS);
            for (auto& entry : kRprsContextFlags) {
                if (creationFlags & entry.first) {
                    context[entry.second] = 1;
                }
            }
            if (creationFlags & RPR_CREATION_FLAGS_ENABLE_CPU) {
                context["threads"] = RprUsdGetInfo<uint32_t>(m_rprContext.get(), RPR_CONTEXT_CPU_THREAD_LIMIT);
            }
            config["context"] = context;

            static const char* kRprsAovNames[] = {
                /* RPR_AOV_COLOR = */ "color",
                /* RPR_AOV_OPACITY = */ "opacity",
                /* RPR_AOV_WORLD_COORDINATE = */ "world.coordinate",
                /* RPR_AOV_UV = */ "uv",
                /* RPR_AOV_MATERIAL_ID = */ "material.id",
                /* RPR_AOV_GEOMETRIC_NORMAL = */ "normal.geom",
                /* RPR_AOV_SHADING_NORMAL = */ "normal",
                /* RPR_AOV_DEPTH = */ "depth",
                /* RPR_AOV_OBJECT_ID = */ "object.id",
                /* RPR_AOV_OBJECT_GROUP_ID = */ "group.id",
                /* RPR_AOV_SHADOW_CATCHER = */ "shadow.catcher",
                /* RPR_AOV_BACKGROUND = */ "background",
                /* RPR_AOV_EMISSION = */ "emission",
                /* RPR_AOV_VELOCITY = */ "velocity",
                /* RPR_AOV_DIRECT_ILLUMINATION = */ "direct.illumination",
                /* RPR_AOV_INDIRECT_ILLUMINATION = */ "indirect.illumination",
                /* RPR_AOV_AO = */ "ao",
                /* RPR_AOV_DIRECT_DIFFUSE = */ "direct.diffuse",
                /* RPR_AOV_DIRECT_REFLECT = */ "direct.reflect",
                /* RPR_AOV_INDIRECT_DIFFUSE = */ "indirect.diffuse",
                /* RPR_AOV_INDIRECT_REFLECT = */ "indirect.reflect",
                /* RPR_AOV_REFRACT = */ "refract",
                /* RPR_AOV_VOLUME = */ "volume",
                /* RPR_AOV_LIGHT_GROUP0 = */ "light.group0",
                /* RPR_AOV_LIGHT_GROUP1 = */ "light.group1",
                /* RPR_AOV_LIGHT_GROUP2 = */ "light.group2",
                /* RPR_AOV_LIGHT_GROUP3 = */ "light.group3",
                /* RPR_AOV_DIFFUSE_ALBEDO = */ "albedo.diffuse",
                /* RPR_AOV_VARIANCE = */ "variance",
                /* RPR_AOV_VIEW_SHADING_NORMAL = */ "view.shading.normal",
                /* RPR_AOV_REFLECTION_CATCHER = */ "reflection.catcher",
                /* RPR_AOV_COLOR_RIGHT = */ "color.right",
                /* RPR_AOV_LPE_0 = */ "lpe0",
                /* RPR_AOV_LPE_1 = */ "lpe1",
                /* RPR_AOV_LPE_2 = */ "lpe2",
                /* RPR_AOV_LPE_3 = */ "lpe3",
                /* RPR_AOV_LPE_4 = */ "lpe4",
                /* RPR_AOV_LPE_5 = */ "lpe5",
                /* RPR_AOV_LPE_6 = */ "lpe6",
                /* RPR_AOV_LPE_7 = */ "lpe7",
                /* RPR_AOV_LPE_8 = */ "lpe8",
                /* RPR_AOV_CAMERA_NORMAL = */ "camera.normal",
                /* RPR_AOV_CRYPTOMATTE_MAT0 = */ "CryptoMaterial00",
                /* RPR_AOV_CRYPTOMATTE_MAT1 = */ "CryptoMaterial01",
                /* RPR_AOV_CRYPTOMATTE_MAT2 = */ "CryptoMaterial02",
                /* RPR_AOV_CRYPTOMATTE_OBJ0 = */ "CryptoObject00",
                /* RPR_AOV_CRYPTOMATTE_OBJ1 = */ "CryptoObject01",
                /* RPR_AOV_CRYPTOMATTE_OBJ2 = */ "CryptoObject02",
            };
            static const size_t kNumRprsAovNames = sizeof(kRprsAovNames) / sizeof(kRprsAovNames[0]);

            json configAovs;
            for (auto& outputRb : m_outputRenderBuffers) {
                auto aovDesc = &outputRb.rprAov->GetDesc();
                if (aovDesc->computed) {
                    if (aovDesc->id == kColorAlpha) {
                        aovDesc = &HdRprAovRegistry::GetInstance().GetAovDesc(RPR_AOV_COLOR, false);
                    } else if (aovDesc->id == kNdcDepth) {
                        // XXX: RprsRender does not support it but for the users, it is more expected if we map it to the linear depth instead of ignoring it at all
                        aovDesc = &HdRprAovRegistry::GetInstance().GetAovDesc(RPR_AOV_DEPTH, false);
                    } else {
                        fprintf(stderr, "Unprocessed computed AOV: %u\n", aovDesc->id);
                        continue;
                    }
                }

                if (TF_VERIFY(aovDesc->id != kAovNone) &&
                    TF_VERIFY(aovDesc->id < kNumRprsAovNames) &&
                    TF_VERIFY(!aovDesc->computed)) {
                    auto aovName = kRprsAovNames[aovDesc->id];
                    if (aovDesc->id >= RPR_AOV_LPE_0 && aovDesc->id <= RPR_AOV_LPE_8) {
                        try {
                            auto name = outputRb.aovBinding->aovName.GetText();
                            if (!*name) {
                                name = aovName;
                            }

                            json lpeAov;
                            lpeAov["output"] = TfStringPrintf("%s.%s.exr", basename.c_str(), name);
                            lpeAov["lpe"] = RprUsdGetStringInfo(outputRb.rprAov->GetAovFb()->GetRprObject(), RPR_FRAMEBUFFER_LPE);
                            configAovs[aovName] = lpeAov;
                        } catch (RprUsdError& e) {
                            fprintf(stderr, "Failed to export %s AOV: %s\n", aovName, e.what());
                        }
                    } else {
                        configAovs[aovName] = TfStringPrintf("%s.%s.exr", basename.c_str(), aovName);
                    }
                }
            }
            config["aovs"] = configAovs;

            if (m_contourAovs) {
                HdRprConfig* rprConfig;
                auto configInstanceLock = m_delegate->LockConfigInstance(&rprConfig);

                json contour;
                contour["object.id"] = int(rprConfig->GetContourUsePrimId());
                contour["material.id"] = int(rprConfig->GetContourUseMaterialId());
                contour["normal"] = int(rprConfig->GetContourUseNormal());
                contour["threshold.normal"] = rprConfig->GetContourNormalThreshold();
                contour["linewidth.objid"] = rprConfig->GetContourLinewidthPrimId();
                contour["linewidth.matid"] = rprConfig->GetContourLinewidthMaterialId();
                contour["linewidth.normal"] = rprConfig->GetContourLinewidthNormal();
                contour["antialiasing"] = rprConfig->GetContourAntialiasing();
                contour["debug"] = int(rprConfig->GetContourDebug());

                config["contour"] = contour;
            }

            configFile << config;
        } catch (json::exception& e) {
            fprintf(stderr, "Failed to fill config file: %s\n", configFilename.c_str());
        }
#else // !defined(RPR_LOADSTORE_AVAILABLE)
        TF_RUNTIME_ERROR(".rpr export is not supported: hdRpr compiled without rprLoadStore library");
#endif // RPR_LOADSTORE_AVAILABLE
    }

    void CommitResources() {
        if (!m_rprContext) {
            return;
        }

        RprUsdMaterialRegistry::GetInstance().CommitResources(m_imageCache.get());
    }

    void Resolve(SdfPath const& aovId) {
        // hdRpr's rendering is implemented asynchronously - rprContextRender spins in the background thread.
        //
        // Resolve works differently depending on the current rendering session type:
        //   * In a non-interactive (i.e. batch) session, it blocks execution until
        //      AOV framebuffer resolved to corresponding HdRenderBuffers.
        //   * In an interactive session, it simply does nothing because we always resolve
        //      data to HdRenderBuffer as fast as possible. It might change in the future though.
        //
        if (m_isBatch) {
            m_batchREM->WaitForResolve(aovId);
        }
    }

    void Render(HdRprRenderThread* renderThread) {
        RenderFrame(renderThread);

        for (auto& aovBinding : m_aovBindings) {
            if (auto rb = static_cast<HdRprRenderBuffer*>(aovBinding.renderBuffer)) {
                rb->SetConverged(true);
            }
        }
    }

    void AbortRender() {
        if (!m_rprContext) {
            return;
        }

        if (m_isAbortingEnabled) {
            if (m_rprContextMetadata.pluginType == kPluginHybridPro) {
                return;
            }

            RPR_ERROR_CHECK(m_rprContext->AbortRender(), "Failed to abort render");
        } else {
            // In case aborting is disabled, we postpone abort until it's enabled
            m_abortRender.store(true);
        }
    }

    HdRprApi::RenderStats GetRenderStats() const {
        HdRprApi::RenderStats stats = {};

        // rprsExport has no progress callback
        if (!m_rprSceneExportPath.empty()) {
            return stats;
        }

        double progress = double(m_numSamples) / m_maxSamples;
        if (m_activePixels != -1) {
            int numPixels = m_viewportSize[0] * m_viewportSize[1];
            progress = std::max(progress, double(numPixels - m_activePixels) / numPixels);
        } else if (m_isRenderUpdateCallbackEnabled && m_rucData.previousProgress > 0.0f) {
            progress += m_rucData.previousProgress * (double(m_numSamplesPerIter) / m_maxSamples);
        }
        stats.percentDone = 100.0 * progress;

        if (m_numSamples > 0) {
            double numRenderedSamples = progress * m_maxSamples;
            auto resolveTime = m_frameResolveTotalTime / numRenderedSamples;
            auto renderTime = m_frameRenderTotalTime / numRenderedSamples;

            using FloatingPointSecond = std::chrono::duration<double>;
            stats.averageRenderTimePerSample = std::chrono::duration_cast<FloatingPointSecond>(renderTime).count();
            stats.averageResolveTimePerSample = std::chrono::duration_cast<FloatingPointSecond>(resolveTime).count();
        }

        return stats;
    }

    bool IsCameraChanged() const {
        if (!m_hdCamera) {
            return false;
        }

        return (m_dirtyFlags & ChangeTracker::DirtyHdCamera) != 0 || m_hdCamera->GetDirtyBits() != HdCamera::Clean;
    }

    bool IsChanged() const {
        if (m_dirtyFlags != ChangeTracker::Clean ||
            IsCameraChanged()) {
            return true;
        }

        HdRprConfig* config;
        auto configInstanceLock = m_delegate->LockConfigInstance(&config);
        return config->IsDirty(HdRprConfig::DirtyAll);
    }

    bool IsConverged() const {
        if (m_currentRenderQuality == HdRprCoreRenderQualityTokens->Low ||
            m_currentRenderQuality == HdRprCoreRenderQualityTokens->Medium) {
            return m_numSamples == 1;
        }

        return (m_numSamples >= m_maxSamples) || (m_activePixels == 0);
    }

    bool IsAdaptiveSamplingEnabled() const {
        return m_rprContext && m_varianceThreshold > 0.0f &&
              (m_rprContextMetadata.pluginType == kPluginTahoe || m_rprContextMetadata.pluginType == kPluginNorthstar);
    }

    bool IsGlInteropEnabled() const {
        return m_rprContext && m_rprContextMetadata.isGlInteropEnabled;
    }

    bool IsVulkanInteropEnabled() const {
        return m_rprContext && RprUsdIsHybrid(m_rprContextMetadata.pluginType) && m_rprContextMetadata.interopInfo;
    }

    bool IsArbitraryShapedLightSupported() const {
        return !RprUsdIsHybrid(m_rprContextMetadata.pluginType);
    }

    bool IsSphereAndDiskLightSupported() const {
        return m_rprContextMetadata.pluginType == kPluginNorthstar || m_rprContextMetadata.pluginType == kPluginHybridPro;
    }

    TfToken const& GetCurrentRenderQuality() const {
        return m_currentRenderQuality;
    }

    void SetInteropInfo(void* interopInfo) {
#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
        m_rprContextMetadata.interopInfo = interopInfo;
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
    }

    rpr::FrameBuffer* GetRawColorFramebuffer() {
        auto it = m_aovRegistry.find(HdRprAovTokens->rawColor);
        if (it != m_aovRegistry.end()) {
            if (auto aov = it->second.lock()) {
                if (auto fb = aov->GetAovFb()) {
                    return fb->GetRprObject();
                }
            }
        }

        return nullptr;
    }

    rpr::FrameBuffer* GetPrimIdFramebuffer() {
        auto it = m_aovRegistry.find(HdAovTokens->primId);
        if (it != m_aovRegistry.end()) {
            if (auto aov = it->second.lock()) {
                if (auto fb = aov->GetAovFb()) {
                    return fb->GetRprObject();
                }
            }
        }

        return nullptr;
    }

#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
    bool GetInteropSemaphore(VkSemaphore& rInteropSemaphore, uint32_t& rInteropSemaphoreIndex) {
        rInteropSemaphore = VK_NULL_HANDLE;

        while (!m_vulkanInteropBufferReady) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        m_rprContext->GetInfo(static_cast<rpr::ContextInfo>(RPR_CONTEXT_INTEROP_SEMAPHORE_INDEX), sizeof(rInteropSemaphoreIndex), &rInteropSemaphoreIndex, nullptr);

        const unsigned int interopSemaphoreCount = 3; // Awful, has to be "global" constant
        if (rInteropSemaphoreIndex < interopSemaphoreCount) {
            VkSemaphore interopSemaphoreArray[interopSemaphoreCount];
            m_rprContext->GetInfo(static_cast<rpr::ContextInfo>(RPR_CONTEXT_FRAMEBUFFERS_READY_SEMAPHORES), sizeof(interopSemaphoreArray), (void*)&interopSemaphoreArray, nullptr);
            rInteropSemaphore = interopSemaphoreArray[rInteropSemaphoreIndex];
        }

        m_vulkanInteropBufferReady = false;

        return (VK_NULL_HANDLE != rInteropSemaphore);
    }
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT

    void Restart() {
        m_resolveData.ForAllAovs([=](ResolveData::AovEntry const& e) {
            e.aov->Clear();
        });

        m_numSamples = 0;
        m_activePixels = -1;
        m_isFirstSample = true;
        m_frameRenderTotalTime = {};
        m_frameResolveTotalTime = {};

        // Always start from RPR_CONTEXT_ITERATIONS=1 to be able to resolve singlesampled AOVs correctly
        m_numSamplesPerIter = 1;
        RPR_ERROR_CHECK(m_rprContext->SetParameter(RPR_CONTEXT_ITERATIONS, m_numSamplesPerIter), "Failed to set context iterations");
    }

private:
    static RprUsdPluginType GetPluginType(TfToken const& renderQuality) {
        if (renderQuality == HdRprCoreRenderQualityTokens->Full) {
            return kPluginTahoe;
        } else if (renderQuality == HdRprCoreRenderQualityTokens->Northstar) {
            return kPluginNorthstar;
        } else if (renderQuality == HdRprCoreRenderQualityTokens->HybridPro) {
            return kPluginHybridPro;
        } else {
            return kPluginHybrid;
        }
    }

    static void RprContextDeleter(rpr::Context* ctx) {
        if (RprUsdIsLeakCheckEnabled()) {
            typedef rpr::Status (GetInfoFnc)(void*, uint32_t, size_t, void*, size_t*);

            struct ListDescriptor {
                rpr_context_info infoType;
                const char* name;
                GetInfoFnc* getInfo;

                ListDescriptor(rpr_context_info infoType, const char* name, void* getInfoFnc)
                    : infoType(infoType), name(name)
                    , getInfo(reinterpret_cast<GetInfoFnc*>(getInfoFnc)) {
                }
            };
            std::vector<ListDescriptor> lists = {
                {RPR_CONTEXT_LIST_CREATED_CAMERAS, "cameras", (void*)rprCameraGetInfo},
                {RPR_CONTEXT_LIST_CREATED_MATERIALNODES, "materialnodes", (void*)rprMaterialNodeGetInfo},
                {RPR_CONTEXT_LIST_CREATED_LIGHTS, "lights", (void*)rprLightGetInfo},
                {RPR_CONTEXT_LIST_CREATED_SHAPES, "shapes", (void*)rprShapeGetInfo},
                {RPR_CONTEXT_LIST_CREATED_HETEROVOLUMES, "heterovolumes", (void*)rprHeteroVolumeGetInfo},
                {RPR_CONTEXT_LIST_CREATED_GRIDS, "grids", (void*)rprGridGetInfo},
                {RPR_CONTEXT_LIST_CREATED_BUFFERS, "buffers", (void*)rprBufferGetInfo},
                {RPR_CONTEXT_LIST_CREATED_IMAGES, "images", (void*)rprImageGetInfo},
                {RPR_CONTEXT_LIST_CREATED_FRAMEBUFFERS, "framebuffers", (void*)rprFrameBufferGetInfo},
                {RPR_CONTEXT_LIST_CREATED_SCENES, "scenes", (void*)rprSceneGetInfo},
                {RPR_CONTEXT_LIST_CREATED_CURVES, "curves", (void*)rprCurveGetInfo},
            };

            bool hasLeaks = false;
            std::vector<char> nameBuffer;

            for (auto& list : lists) {
                size_t sizeParam = 0;
                if (!RPR_ERROR_CHECK(ctx->GetInfo(list.infoType, 0, nullptr, &sizeParam), "Failed to get context info", ctx)) {
                    size_t numObjects = sizeParam / sizeof(void*);

                    if (numObjects > 0) {
                        if (!hasLeaks) {
                            hasLeaks = true;
                            fprintf(stderr, "hdRpr - rpr::Context leaks detected\n");
                        }

                        fprintf(stderr, "Leaked %zu %s: ", numObjects, list.name);

                        std::vector<void*> objectPointers(numObjects, nullptr);
                        if (!RPR_ERROR_CHECK(ctx->GetInfo(list.infoType, sizeParam, objectPointers.data(), nullptr), "Failed to get context info", ctx)) {
                            fprintf(stderr, "{");
                            for (size_t i = 0; i < numObjects; ++i) {
                                size_t size;
                                if (!RPR_ERROR_CHECK(list.getInfo(objectPointers[i], RPR_OBJECT_NAME, 0, nullptr, &size), "Failed to get object name size") && size > 0) {
                                    if (size > nameBuffer.size()) {
                                        nameBuffer.reserve(size);
                                    }

                                    if (!RPR_ERROR_CHECK(list.getInfo(objectPointers[i], RPR_OBJECT_NAME, size, nameBuffer.data(), &size), "Failed to get object name")) {
                                        fprintf(stderr, "\"%.*s\"", int(size), nameBuffer.data());
                                        if (i + 1 != numObjects) fprintf(stderr, ",");
                                        continue;
                                    }
                                }

                                fprintf(stderr, "0x%p", objectPointers[i]);
                                if (i + 1 != numObjects) fprintf(stderr, ",");
                            }
                            fprintf(stderr, "}\n");
                        } else {
                            fprintf(stderr, "failed to get object list\n");
                        }
                    }
                }
            }
        }
        delete ctx;
    }

    void InitRpr() {
        bool flipRequestedByRenderSetting = false;

        {
            HdRprConfig* config;
            auto configInstanceLock = m_delegate->LockConfigInstance(&config);
            // Force sync to catch up the latest render quality and render device
            config->Sync(m_delegate);

            m_currentRenderQuality = GetRenderQuality(*config);
            flipRequestedByRenderSetting = config->GetCoreFlipVertical();

            m_rprContextMetadata.additionalIntProperties[RPR_CONTEXT_CREATEPROP_HYBRID_ACC_MEMORY_SIZE] = 
                static_cast<std::uint32_t>(config->GetHybridAccelerationMemorySizeMb()) << 20;
            m_rprContextMetadata.additionalIntProperties[RPR_CONTEXT_CREATEPROP_HYBRID_MESH_MEMORY_SIZE] = 
                static_cast<std::uint32_t>(config->GetHybridMeshMemorySizeMb()) << 20;
            m_rprContextMetadata.additionalIntProperties[RPR_CONTEXT_CREATEPROP_HYBRID_STAGING_MEMORY_SIZE] = 
                static_cast<std::uint32_t>(config->GetHybridStagingMemorySizeMb()) << 20;
            m_rprContextMetadata.additionalIntProperties[RPR_CONTEXT_CREATEPROP_HYBRID_SCRATCH_MEMORY_SIZE] = 
                static_cast<std::uint32_t>(config->GetHybridScratchMemorySizeMb()) << 20;
        }

        m_rprContextMetadata.pluginType = GetPluginType(m_currentRenderQuality);
        m_rprContext = RprContextPtr(RprUsdCreateContext(&m_rprContextMetadata), RprContextDeleter);
        if (!m_rprContext) {
            RPR_THROW_ERROR_MSG("Failed to create RPR context");
        }

        auto contextApiHandle = rpr::GetRprObject(m_rprContext.get());
        for (std::string materialXSearchPath : TfStringSplit(TfGetenv("MATERIALX_SEARCH_PATH"), ARCH_PATH_LIST_SEP)) {
            if (!materialXSearchPath.empty()) {
                RPR_ERROR_CHECK(rprMaterialXAddResourceFolder(contextApiHandle, materialXSearchPath.c_str()), "Failed to add mtlx resource folder");
            }
        }

        // TODO: verify
        if (RprUsdIsHybrid(m_rprContextMetadata.pluginType)) {
            RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_ENABLE_RELAXED_MATERIAL_CHECKS), 1u), "Failed to enable relaxed material checks");
        }

        // TODO: Re-check
        RPR_ERROR_CHECK(m_rprContext->SetParameter(rpr::ContextInfo(RPR_CONTEXT_USE_GMON), 1u), "Failed to enable GMON (By Roman Vasilyev)");

        uint32_t requiredYFlip = 0;
        bool flipRequestedByInteropHybrid = RprUsdIsHybrid(m_rprContextMetadata.pluginType) && m_rprContextMetadata.interopInfo;
        if (flipRequestedByInteropHybrid || flipRequestedByRenderSetting) {
            RPR_ERROR_CHECK_THROW(m_rprContext->GetFunctionPtr(
                RPR_CONTEXT_FLUSH_FRAMEBUFFERS_FUNC_NAME, 
                (void**)(&m_rprContextFlushFrameBuffers)
            ), "Fail to get rprContextFlushFramebuffers function");
            requiredYFlip = 1;
        }

        m_isOutputFlipped = RprUsdGetInfo<uint32_t>(m_rprContext.get(), RPR_CONTEXT_Y_FLIP) != requiredYFlip;
        RPR_ERROR_CHECK_THROW(m_rprContext->SetParameter(RPR_CONTEXT_Y_FLIP, requiredYFlip), "Failed to set context Y FLIP parameter");

        m_isRenderUpdateCallbackEnabled = false;
        m_imageCache.reset(new RprUsdImageCache(m_rprContext.get()));
        m_isAbortingEnabled.store(false);
    }

    bool ValidateRifModels(std::string const& modelsPath) {
        // To ensure that current RIF implementation will use correct models we check for the file that points to models version
        std::ifstream versionFile(modelsPath + "/rif_models.version");
        if (versionFile.is_open()) {
            std::stringstream buffer;
            buffer << versionFile.rdbuf();
            auto rifVersionString = std::to_string(RIF_VERSION_MAJOR) + "." + std::to_string(RIF_VERSION_MINOR) + "." + std::to_string(RIF_VERSION_REVISION);
            return rifVersionString == buffer.str();
        }

        return false;
    }

    void InitRif() {
        PlugPluginPtr plugin = PLUG_THIS_PLUGIN;
        auto modelsPath = PlugFindPluginResource(plugin, "rif_models", false);
        if (modelsPath.empty()) {
            TF_RUNTIME_ERROR("Failed to find RIF models in plugin package");
        } else if (!ValidateRifModels(modelsPath)) {
            modelsPath = "";
            TF_RUNTIME_ERROR("RIF version and AI models version mismatch");
        }

        m_rifContext = rif::Context::Create(m_rprContext.get(), m_rprContextMetadata, modelsPath);
    }

    void InitAovs() {
        m_colorAov = std::static_pointer_cast<HdRprApiColorAov>(CreateAov(HdAovTokens->color));

        m_lpeAovPool.clear();
        m_lpeAovPool.insert(m_lpeAovPool.begin(), {
            HdRprAovTokens->lpe0, HdRprAovTokens->lpe1, HdRprAovTokens->lpe2,
            HdRprAovTokens->lpe3, HdRprAovTokens->lpe4, HdRprAovTokens->lpe5,
            HdRprAovTokens->lpe6, HdRprAovTokens->lpe7, HdRprAovTokens->lpe8});
    }

    void InitScene() {
        rpr::Status status;
        m_scene.reset(m_rprContext->CreateScene(&status));
        if (!m_scene) {
            RPR_ERROR_CHECK_THROW(status, "Failed to create scene", m_rprContext.get());
        }

        RPR_ERROR_CHECK_THROW(m_rprContext->SetScene(m_scene.get()), "Failed to set context scene");
    }

    void InitCamera() {
        rpr::Status status;
        m_camera.reset(m_rprContext->CreateCamera(&status));
        if (!m_camera) {
            RPR_ERROR_CHECK_THROW(status, "Failed to create camera", m_rprContext.get());
        }

        RPR_ERROR_CHECK_THROW(m_scene->SetCamera(m_camera.get()), "Failed to to set scene camera");
    }

    void AddDefaultLight() {
        if (!m_defaultLightObject) {
            const GfVec3f k_defaultLightColor(0.5f, 0.5f, 0.5f);
            PlugPluginPtr plugin = PLUG_THIS_PLUGIN;
            auto imagesPath = PlugFindPluginResource(plugin, "images", false);
            auto path = imagesPath + "/Default.hdr";
            m_defaultLightObject = CreateEnvironmentLight(path, 1.f, HdRprApi::BackgroundOverride{true, {0.003f, 0.003f, 0.003f}});
            SetTransform(
                m_defaultLightObject->light.get(), 
                GfMatrix4f(1.0).SetRotate(GfRotation({1.0f, 0.0f, 0.0f}, 90.0)) * GfMatrix4f(1.0).SetRotate(GfRotation({0.0f, 0.0f, 1.0f}, 180.0)));

            if (RprUsdIsLeakCheckEnabled()) {
                m_defaultLightObject->light->SetName("defaultLight");
                m_defaultLightObject->image->SetName("defaultLight");
            }

            // Do not count default light object
            m_numLights--;
        }
    }

    void RemoveDefaultLight() {
        if (m_defaultLightObject) {
            ReleaseImpl(m_defaultLightObject);
            m_defaultLightObject = nullptr;

            // Do not count default light object
            m_numLights++;
        }
    }

    std::unique_ptr<RprUsdCoreImage> CreateConstantColorImage(float const* color) {
        std::array<float, 3> colorArray = {color[0], color[1], color[2]};
        rpr_image_format format = {3, RPR_COMPONENT_TYPE_FLOAT32};
        rpr_uint imageSize = RprUsdIsHybrid(m_rprContextMetadata.pluginType) ? 64 : 1;
        std::vector<std::array<float, 3>> imageData(imageSize * imageSize, colorArray);

        rpr::Status status;
        auto image = std::unique_ptr<RprUsdCoreImage>(RprUsdCoreImage::Create(m_rprContext.get(), imageSize, imageSize, format, imageData.data(), &status));
        if (!image) {
            RPR_ERROR_CHECK(status, "Failed to create const color image", m_rprContext.get());
            return nullptr;
        }

        return image;
    }

    void SplitPolygons(const VtIntArray& indexes, const VtIntArray& vpf, VtIntArray& out_newIndexes, VtIntArray& out_newVpf) {
        out_newIndexes.clear();
        out_newVpf.clear();

        out_newIndexes.reserve(indexes.size());
        out_newVpf.reserve(vpf.size());

        VtIntArray::const_iterator idxIt = indexes.begin();
        for (const int vCount : vpf) {
            if (vCount == 3 || vCount == 4) {
                for (int i = 0; i < vCount; ++i) {
                    out_newIndexes.push_back(*idxIt);
                    ++idxIt;
                }
                out_newVpf.push_back(vCount);
            } else {
                const int commonVertex = *idxIt;
                constexpr int triangleVertexCount = 3;
                for (int i = 1; i < vCount - 1; ++i) {
                    out_newIndexes.push_back(commonVertex);
                    out_newIndexes.push_back(*(idxIt + i + 0));
                    out_newIndexes.push_back(*(idxIt + i + 1));
                    out_newVpf.push_back(triangleVertexCount);
                }
                idxIt += vCount;
            }
        }
    }

    void SplitPolygons(const VtIntArray& indexes, const VtIntArray& vpf, VtIntArray& out_newIndexes) {
        out_newIndexes.clear();
        out_newIndexes.reserve(indexes.size());

        VtIntArray::const_iterator idxIt = indexes.begin();
        for (const int vCount : vpf) {
            if (vCount == 3 || vCount == 4) {
                for (int i = 0; i < vCount; ++i) {
                    out_newIndexes.push_back(*idxIt);
                    ++idxIt;
                }
            } else {
                const int commonVertex = *idxIt;
                for (int i = 1; i < vCount - 1; ++i) {
                    out_newIndexes.push_back(commonVertex);
                    out_newIndexes.push_back(*(idxIt + i + 0));
                    out_newIndexes.push_back(*(idxIt + i + 1));
                }
                idxIt += vCount;
            }
        }
    }

    void ConvertIndices(VtIntArray* indices, VtIntArray const& vpf, TfToken const& windingOrder) {
        if (windingOrder == HdTokens->rightHanded) {
            return;
        }

        // XXX: RPR does not allow to select which winding order we want to use and it's by default right handed
        size_t indicesOffset = 0;
        for (int iFace = 0; iFace < vpf.size(); ++iFace) {
            auto faceIndices = indices->data() + indicesOffset;
            std::swap(faceIndices[0], faceIndices[2]);
            indicesOffset += vpf[iFace];
        }
    }

    rpr::Shape* CreateCubeMesh(float width, float height, float depth) {
        constexpr const size_t cubeVertexCount = 24;
        constexpr const size_t cubeNormalCount = 24;
        constexpr const size_t cubeVpfCount = 12;

        const float halfWidth = width * 0.5f;
        const float halfHeight = height * 0.5f;
        const float halfDepth = depth * 0.5f;

        VtVec3fArray position(cubeVertexCount);
        position[0] = GfVec3f(-halfWidth, halfHeight, -halfDepth);
        position[1] = GfVec3f(halfWidth, halfHeight, -halfDepth);
        position[2] = GfVec3f(halfWidth, halfHeight, halfDepth);
        position[3] = GfVec3f(-halfWidth, halfHeight, halfDepth);

        position[4] = GfVec3f(-halfWidth, -halfHeight, -halfDepth);
        position[5] = GfVec3f(halfWidth, -halfHeight, -halfDepth);
        position[6] = GfVec3f(halfWidth, -halfHeight, halfDepth);
        position[7] = GfVec3f(-halfWidth, -halfHeight, halfDepth);

        position[8] = GfVec3f(-halfWidth, -halfHeight, halfDepth);
        position[9] = GfVec3f(-halfWidth, -halfHeight, -halfDepth);
        position[10] = GfVec3f(-halfWidth, halfHeight, -halfDepth);
        position[11] = GfVec3f(-halfWidth, halfHeight, halfDepth);

        position[12] = GfVec3f(halfWidth, -halfHeight, halfDepth);
        position[13] = GfVec3f(halfWidth, -halfHeight, -halfDepth);
        position[14] = GfVec3f(halfWidth, halfHeight, -halfDepth);
        position[15] = GfVec3f(halfWidth, halfHeight, halfDepth);

        position[16] = GfVec3f(-halfWidth, -halfHeight, -halfDepth);
        position[17] = GfVec3f(halfWidth, -halfHeight, -halfDepth);
        position[18] = GfVec3f(halfWidth, halfHeight, -halfDepth);
        position[19] = GfVec3f(-halfWidth, halfHeight, -halfDepth);

        position[20] = GfVec3f(-halfWidth, -halfHeight, halfDepth);
        position[21] = GfVec3f(halfWidth, -halfHeight, halfDepth);
        position[22] = GfVec3f(halfWidth, halfHeight, halfDepth);
        position[23] = GfVec3f(-halfWidth, halfHeight, halfDepth);

        VtVec3fArray normals(cubeNormalCount);
        normals[0] = GfVec3f(0.f, 1.f, 0.f);
        normals[1] = GfVec3f(0.f, 1.f, 0.f);
        normals[2] = GfVec3f(0.f, 1.f, 0.f);
        normals[3] = GfVec3f(0.f, 1.f, 0.f);

        normals[4] = GfVec3f(0.f, -1.f, 0.f);
        normals[5] = GfVec3f(0.f, -1.f, 0.f);
        normals[6] = GfVec3f(0.f, -1.f, 0.f);
        normals[7] = GfVec3f(0.f, -1.f, 0.f);

        normals[8] = GfVec3f(-1.f, 0.f, 0.f);
        normals[9] = GfVec3f(-1.f, 0.f, 0.f);
        normals[10] = GfVec3f(-1.f, 0.f, 0.f);
        normals[11] = GfVec3f(-1.f, 0.f, 0.f);

        normals[12] = GfVec3f(1.f, 0.f, 0.f);
        normals[13] = GfVec3f(1.f, 0.f, 0.f);
        normals[14] = GfVec3f(1.f, 0.f, 0.f);
        normals[15] = GfVec3f(1.f, 0.f, 0.f);

        normals[16] = GfVec3f(0.f, 0.f, -1.f);
        normals[17] = GfVec3f(0.f, 0.f, -1.f);
        normals[18] = GfVec3f(0.f, 0.f, -1.f);
        normals[19] = GfVec3f(0.f, 0.f, -1.f);

        normals[20] = GfVec3f(0.f, 0.f, 1.f);
        normals[21] = GfVec3f(0.f, 0.f, 1.f);
        normals[22] = GfVec3f(0.f, 0.f, 1.f);
        normals[23] = GfVec3f(0.f, 0.f, 1.f);

        VtIntArray indexes = {
            3,1,0,
            2,1,3,

            6,4,5,
            7,4,6,

            11,9,8,
            10,9,11,

            14,12,13,
            15,12,14,

            19,17,16,
            18,17,19,

            22,20,21,
            23,20,22
        };

        VtIntArray vpf(cubeVpfCount, 3);

        return CreateMesh(position, indexes, normals, VtIntArray(), VtVec2fArray(), VtIntArray(), vpf);
    }

    void UpdateColorAlpha(HdRprApiColorAov* colorAov) {
        // Force disable alpha for some render modes when we render with Northstar
        if (m_rprContextMetadata.pluginType == kPluginNorthstar) {
            // Contour rendering should not have an alpha,
            // it might cause missed contours (just for the user, not in actual data)
            if (m_contourAovs) {
                m_isAlphaEnabled = false;
            } else {
                auto currentRenderMode = RprUsdGetInfo<rpr_render_mode>(m_rprContext.get(), RPR_CONTEXT_RENDER_MODE);

                // XXX (RPRNEXT-343): opacity is always zero when such render modes are active:
                if (currentRenderMode == RPR_RENDER_MODE_NORMAL ||
                    currentRenderMode == RPR_RENDER_MODE_POSITION ||
                    currentRenderMode == RPR_RENDER_MODE_TEXCOORD ||
                    currentRenderMode == RPR_RENDER_MODE_WIREFRAME ||
                    currentRenderMode == RPR_RENDER_MODE_MATERIAL_INDEX) {
                    m_isAlphaEnabled = false;
                }
            }
        }

        if (m_isAlphaEnabled) {
            auto opacityAov = GetAov(HdRprAovTokens->opacity, m_viewportSize[0], m_viewportSize[1], HdFormatFloat32Vec4);
            if (opacityAov) {
                colorAov->SetOpacityAov(opacityAov);
            } else {
                TF_WARN("Cannot enable alpha");
            }
        } else {
            colorAov->SetOpacityAov(nullptr);
        }
    }

    std::shared_ptr<HdRprApiAov> GetAov(TfToken const& aovName, int width, int height, HdFormat format) {
        std::shared_ptr<HdRprApiAov> aov;
        auto aovIter = m_aovRegistry.find(aovName);
        if (aovIter == m_aovRegistry.end() ||
            !(aov = aovIter->second.lock())) {

            aov = CreateAov(aovName, width, height, format);
        }
        return aov;
    }

    std::shared_ptr<HdRprApiAov> CreateAov(TfToken const& aovName, int width, int height, HdFormat format) {
        if ((aovName.GetString() == "depth") || (aovName.GetString() == "worldCoordinate")) return nullptr;
        if (!m_rprContext ||
            width < 0 || height < 0 ||
            format == HdFormatInvalid || HdDataSizeOfFormat(format) == 0) {
            return nullptr;
        }

        auto& aovDesc = HdRprAovRegistry::GetInstance().GetAovDesc(aovName);
        if (aovDesc.id == kAovNone ||
            aovDesc.format == HdFormatInvalid) {
            TF_WARN("Unsupported aov type: %s", aovName.GetText());
            return nullptr;
        }

        std::shared_ptr<HdRprApiAov> aov;

        auto iter = m_aovRegistry.find(aovName);
        if (iter != m_aovRegistry.end()) {
            aov = iter->second.lock();
        }

        try {
            if (!aov) {
                HdRprApiAov* newAov = nullptr;
                std::function<void(HdRprApiAov*)> aovCustomDestructor;

                if (aovName == HdAovTokens->color) {
                    auto rawColorAov = GetAov(HdRprAovTokens->rawColor, width, height, HdFormatFloat32Vec4);
                    if (!rawColorAov) {
                        TF_RUNTIME_ERROR("Failed to create color AOV: can't create rawColor AOV");
                        return nullptr;
                    }

                    auto colorAov = new HdRprApiColorAov(format, std::move(rawColorAov), m_rprContext.get(), m_rprContextMetadata);
                    UpdateColorAlpha(colorAov);

                    newAov = colorAov;
                } else if (aovName == HdAovTokens->normal) {
                    newAov = new HdRprApiNormalAov(width, height, format, m_rprContext.get(), m_rprContextMetadata, m_rifContext.get());
                } else if (aovName == HdAovTokens->depth) {
                    auto worldCoordinateAov = GetAov(HdRprAovTokens->worldCoordinate, width, height, HdFormatFloat32Vec4);
                    if (!worldCoordinateAov) {
                        TF_RUNTIME_ERROR("Failed to create depth AOV: can't create worldCoordinate AOV");
                        return nullptr;
                    }

                    newAov = new HdRprApiDepthAov(width, height, format, std::move(worldCoordinateAov), m_rprContext.get(), m_rprContextMetadata, m_rifContext.get());
                } else if (TfStringStartsWith(aovName.GetString(), "lpe")) {
                    newAov = new HdRprApiAov(rpr::Aov(aovDesc.id), width, height, format, m_rprContext.get(), m_rprContextMetadata, m_rifContext.get());
                    aovCustomDestructor = [this](HdRprApiAov* aov) {
                        // Each LPE AOV reserves RPR's LPE AOV id (RPR_AOV_LPE_0, ...)
                        // As soon as LPE AOV is released we want to return reserved id to the pool
                        m_lpeAovPool.push_back(GetRprLpeAovName(aov->GetAovFb()->GetAovId()));
                        m_dirtyFlags |= ChangeTracker::DirtyAOVRegistry;
                        delete aov;
                    };
                } else if (aovName == HdRprAovTokens->materialIdMask ||
                           aovName == HdRprAovTokens->objectIdMask ||
                           aovName == HdRprAovTokens->objectGroupIdMask) {
                    TfToken baseAovName;
                    if (aovName == HdRprAovTokens->materialIdMask) {
                        baseAovName = HdRprAovTokens->materialId;
                    } else if (aovName == HdRprAovTokens->objectIdMask) {
                        baseAovName = HdAovTokens->primId;
                    } else if (aovName == HdRprAovTokens->objectGroupIdMask) {
                        baseAovName = HdRprAovTokens->objectGroupId;
                    }

                    auto baseAov = GetAov(baseAovName, width, height, (baseAovName == HdAovTokens->primId) ? HdFormatInt32Vec4 : HdFormatInt32);
                    if (!baseAov) {
                        TF_RUNTIME_ERROR("Failed to create %s AOV: cant create %s AOV", aovName.GetText(), baseAovName.GetText());
                        return nullptr;
                    }

                    newAov = new HdRprApiIdMaskAov(aovDesc, baseAov, width, height, format, m_rprContext.get(), m_rprContextMetadata, m_rifContext.get());
                } else {
                    if (!aovDesc.computed) {
                        newAov = new HdRprApiAov(rpr::Aov(aovDesc.id), width, height, format, m_rprContext.get(), m_rprContextMetadata, m_rifContext.get());
                    } else {
                        TF_CODING_ERROR("Failed to create %s AOV: unprocessed computed AOV", aovName.GetText());
                    }
                }

                if (!newAov) {
                    return nullptr;
                }

                if (!aovCustomDestructor) {
                    aovCustomDestructor = [this](HdRprApiAov* aov) {
                        m_dirtyFlags |= ChangeTracker::DirtyAOVRegistry;
                        delete aov;
                    };
                }

                aov = std::shared_ptr<HdRprApiAov>(newAov, std::move(aovCustomDestructor));
                m_aovRegistry[aovName] = aov;
                m_dirtyFlags |= ChangeTracker::DirtyAOVRegistry;
            } else {
                aov->Resize(width, height, format);
            }
        } catch (std::runtime_error const& e) {
            TF_RUNTIME_ERROR("Failed to create %s AOV: %s", aovName.GetText(), e.what());
        }

        return aov;
    }

    std::shared_ptr<HdRprApiAov> CreateAov(TfToken const& aovName) {
        auto iter = m_aovRegistry.find(aovName);
        if (iter != m_aovRegistry.end()) {
            if (auto aov = iter->second.lock()) {
                return aov;
            }
        }

        return CreateAov(aovName, m_viewportSize[0], m_viewportSize[1], HdFormatFloat32Vec4);
    }

    struct OutputRenderBuffer;

    OutputRenderBuffer* GetOutputRenderBuffer(TfToken const& aovName) {
        auto it = std::find_if(m_outputRenderBuffers.begin(), m_outputRenderBuffers.end(), [&](OutputRenderBuffer const& outRb) {
            return outRb.aovName == aovName;
        });
        if (it == m_outputRenderBuffers.end()) {
            return nullptr;
        }
        return &(*it);
    }

    bool RenderImage(std::string const& path) {
        if (!m_rifContext) {
            return false;
        }

        auto colorOutputRb = GetOutputRenderBuffer(HdAovTokens->color);
        if (!colorOutputRb) {
            return false;
        }

        auto textureData = RprUsdTextureData::New(path);
        if (textureData) {
            rif_image_desc imageDesc = {};
            imageDesc.image_width = textureData->GetWidth();
            imageDesc.image_height = textureData->GetHeight();
            imageDesc.image_depth = 1;
            imageDesc.image_row_pitch = 0;
            imageDesc.image_slice_pitch = 0;

            auto textureMetadata = textureData->GetGLMetadata();

            uint8_t bytesPerComponent;
            if (textureMetadata.glType == GL_UNSIGNED_BYTE) {
                imageDesc.type = RIF_COMPONENT_TYPE_UINT8;
                bytesPerComponent = 1;
            } else if (textureMetadata.glType == GL_HALF_FLOAT) {
                imageDesc.type = RIF_COMPONENT_TYPE_FLOAT16;
                bytesPerComponent = 2;
            } else if (textureMetadata.glType == GL_FLOAT) {
                imageDesc.type = RIF_COMPONENT_TYPE_FLOAT32;
                bytesPerComponent = 2;
            } else {
                TF_RUNTIME_ERROR("\"%s\" image has unsupported pixel channel type: %#x", path.c_str(), textureMetadata.glType);
                return false;
            }

            if (textureMetadata.glFormat == GL_RGBA) {
                imageDesc.num_components = 4;
            } else if (textureMetadata.glFormat == GL_RGB) {
                imageDesc.num_components = 3;
            } else if (textureMetadata.glFormat == GL_RED) {
                imageDesc.num_components = 1;
            } else {
                TF_RUNTIME_ERROR("\"%s\" image has unsupported pixel format: %#x", path.c_str(), textureMetadata.glFormat);
                return false;
            }

            auto rifImage = m_rifContext->CreateImage(imageDesc);

            void* mappedData;
            if (RIF_ERROR_CHECK(rifImageMap(rifImage->GetHandle(), RIF_IMAGE_MAP_WRITE, &mappedData), "Failed to map rif image") || !mappedData) {
                return false;
            }
            size_t imageSize = bytesPerComponent * imageDesc.num_components * imageDesc.image_width * imageDesc.image_height;
            std::memcpy(mappedData, textureData->GetData(), imageSize);
            RIF_ERROR_CHECK(rifImageUnmap(rifImage->GetHandle(), mappedData), "Failed to unmap rif image");

            auto colorRb = static_cast<HdRprRenderBuffer*>(colorOutputRb->aovBinding->renderBuffer);

            try {
                auto blitFilter = rif::Filter::CreateCustom(RIF_IMAGE_FILTER_USER_DEFINED, m_rifContext.get());
                auto blitKernelCode = std::string(R"(
                    const int2 outSize = GET_BUFFER_SIZE(outputImage);

                    int2 coord;
                    GET_COORD_OR_RETURN(coord, outSize);

                    vec2 uv = (convert_vec2(coord) + 0.5f)/convert_vec2(outSize);
                    float aspectRatio = (float)(outSize.x)/outSize.y;

                    vec2 srcUv;
                    if (aspectRatio > 1.0f) {
                        float scale = 1.0f/aspectRatio;
                        srcUv = make_vec2((uv.x - (1.0f - scale)*0.5f)/scale, uv.y);
                    } else {
                        srcUv = make_vec2(uv.x, (uv.y - (1.0f - aspectRatio)*0.5f)/aspectRatio);
                    }

                    const int2 inSize = GET_BUFFER_SIZE(srcImage);
                    int2 srcCoord = convert_int2(srcUv*convert_vec2(inSize));
                    srcCoord = clamp(srcCoord, make_int2(0, 0), inSize - 1);
                    vec4 color = ReadPixelTyped(srcImage, srcCoord.x, srcCoord.y);

                    WritePixelTyped(outputImage, coord.x, coord.y, color);
                )");
                blitFilter->SetInput("srcImage", rifImage->GetHandle());
                blitFilter->SetParam("code", blitKernelCode);
                blitFilter->SetOutput(rif::Image::GetDesc(colorRb->GetWidth(), colorRb->GetHeight(), colorRb->GetFormat()));
                blitFilter->SetInput(rif::Color, blitFilter->GetOutput());
                blitFilter->Update();

                m_rifContext->ExecuteCommandQueue();

                if (RIF_ERROR_CHECK(rifImageMap(blitFilter->GetOutput(), RIF_IMAGE_MAP_READ, &mappedData), "Failed to map rif image") || !mappedData) {
                    return false;
                }
                size_t size = HdDataSizeOfFormat(colorRb->GetFormat()) * colorRb->GetWidth() * colorRb->GetHeight();
                if (auto colorRbData = colorRb->GetPointerForWriting()) {
                    std::memcpy(colorRbData, mappedData, size);
                }

                RIF_ERROR_CHECK(rifImageUnmap(blitFilter->GetOutput(), mappedData), "Failed to unmap rif image");
                return true;
            } catch (rif::Error const& e) {
                TF_RUNTIME_ERROR("Failed to blit image: %s", e.what());
                return false;
            }
        } else {
            TF_RUNTIME_ERROR("Failed to load \"%s\" image", path.c_str());
            return false;
        }
    }

    void ApplyAspectRatioPolicy(GfVec2i viewportSize, TfToken const& policy, GfVec2f& size) {
        float viewportAspectRatio = float(viewportSize[0]) / float(viewportSize[1]);
        if (viewportAspectRatio <= 0.0) {
            return;
        }
        float apertureAspectRatio = size[0] / size[1];
        enum { Width, Height, None } adjust = None;
        if (policy == UsdRenderTokens->adjustPixelAspectRatio) {
            // XXX (RPR): Not supported by RPR API. How can we emulate it?
            // pixelAspectRatio = apertureAspectRatio / viewportAspectRatio;
        } else if (policy == UsdRenderTokens->adjustApertureHeight) {
            adjust = Height;
        } else if (policy == UsdRenderTokens->adjustApertureWidth) {
            adjust = Width;
        } else if (policy == UsdRenderTokens->expandAperture) {
            adjust = (apertureAspectRatio > viewportAspectRatio) ? Height : Width;
        } else if (policy == UsdRenderTokens->cropAperture) {
            adjust = (apertureAspectRatio > viewportAspectRatio) ? Width : Height;
        }
        // Adjust aperture so that size[0] / size[1] == viewportAspectRatio.
        if (adjust == Width) {
            size[0] = size[1] * viewportAspectRatio;
        } else if (adjust == Height) {
            size[1] = size[0] / viewportAspectRatio;
        }
    }

private:
    HdRprDelegate* m_delegate;

    enum ChangeTracker : uint32_t {
        Clean = 0,
        AllDirty = ~0u,
        DirtyScene = 1 << 0,
        DirtyAOVRegistry = 1 << 1,
        DirtyHdCamera = 1 << 2,
        DirtyViewport = 1 << 3,
        DirtyAOVBindings = 1 << 4,
    };
    uint32_t m_dirtyFlags = ChangeTracker::AllDirty;

    using RprContextPtr = std::unique_ptr<rpr::Context, decltype(&RprContextDeleter)>;
    RprContextPtr m_rprContext{nullptr, RprContextDeleter};
    RprUsdContextMetadata m_rprContextMetadata;
    bool m_isOutputFlipped;

    std::unique_ptr<rif::Context> m_rifContext;

    std::unique_ptr<rpr::Scene> m_scene;
    std::unique_ptr<rpr::Camera> m_camera;
    std::unique_ptr<RprUsdImageCache> m_imageCache;

    std::shared_ptr<HdRprApiColorAov> m_colorAov;
    std::map<TfToken, std::weak_ptr<HdRprApiAov>> m_aovRegistry;
    std::map<TfToken, std::shared_ptr<HdRprApiAov>> m_internalAovs;
    HdRenderPassAovBindingVector m_aovBindings;
    std::vector<TfToken> m_lpeAovPool;

    struct ContourRenderModeAovs {
        std::shared_ptr<HdRprApiAov> normal;
        std::shared_ptr<HdRprApiAov> primId;
        std::shared_ptr<HdRprApiAov> materialId;
        std::shared_ptr<HdRprApiAov> uv;
    };
    std::unique_ptr<ContourRenderModeAovs> m_contourAovs;

    struct OutputRenderBuffer {
        HdRenderPassAovBinding const* aovBinding;
        TfToken aovName;
        std::string lpe;

        std::shared_ptr<HdRprApiAov> rprAov;

        bool isMultiSampled;
        void* mappedData;
        size_t mappedDataSize;
    };
    std::vector<OutputRenderBuffer> m_outputRenderBuffers;

    class BatchRenderEventManager {
    public:
        void WaitForResolve(SdfPath const& aovId) {
            if (m_signalingAovId.IsEmpty()) {
                m_signalingAovId = aovId;
            } else if (m_signalingAovId != aovId) {
                // When there is more than one bound aov,
                // we want to avoid blocking resolve for each of them separately.
                // Instead, block only on the first one only.
                return;
            }

            m_isResolveRequested.store(true);
            std::unique_lock<std::mutex> lock(m_renderEventMutex);
            m_renderEventCV.wait(lock, [this]() -> bool { return !m_isRenderInProgress || !m_isResolveRequested; });
        }

        class RenderScopeGuard {
        public:
            RenderScopeGuard(BatchRenderEventManager* rem) : m_rem(rem) {
                m_rem->m_signalingAovId = SdfPath::EmptyPath();
                m_rem->m_isRenderInProgress.store(true);
                m_rem->m_renderEventCV.notify_one();
            }
            ~RenderScopeGuard() {
                m_rem->m_isRenderInProgress.store(false);
                m_rem->m_renderEventCV.notify_one();
            }

        private:
            BatchRenderEventManager* m_rem;
        };
        RenderScopeGuard EnterRenderScope() { return RenderScopeGuard(this); }

        bool IsResolveRequested() const {
            return m_isResolveRequested;
        }

        void OnResolve() {
            m_isResolveRequested.store(false);
            m_renderEventCV.notify_one();
        }

    private:
        std::mutex m_renderEventMutex;
        std::condition_variable m_renderEventCV;
        std::atomic<bool> m_isRenderInProgress{false};
        std::atomic<bool> m_isResolveRequested{false};
        SdfPath m_signalingAovId;
    };
    std::unique_ptr<BatchRenderEventManager> m_batchREM;
    std::atomic<bool> m_isBatch{false};
    bool m_isProgressive;

    struct ResolveData {
        struct AovEntry {
            HdRprApiAov* aov;
            bool isMultiSampled;
        };
        std::vector<AovEntry> rawAovs;
        std::vector<AovEntry> computedAovs;

        template <typename F>
        void ForAllAovs(F&& f) {
            for (auto& aov : rawAovs) { f(aov); }
            for (auto& aov : computedAovs) { f(aov); }
        }
    };
    ResolveData m_resolveData;

    enum {
        kResolveAfterRender,
        kResolveInRenderUpdateCallback,
    } m_resolveMode = kResolveAfterRender;
    bool m_isFirstSample = true;

    bool m_isDenoiseEnabled = false;
    int m_denoiseMinIter;
    int m_denoiseIterStep;

    GfVec2i m_viewportSize = GfVec2i(0);
    GfMatrix4d m_cameraProjectionMatrix = GfMatrix4d(1.f);
    HdRprCamera const* m_hdCamera = nullptr;
    bool m_isAlphaEnabled;

    std::atomic<int> m_numLights{0};
    HdRprApiEnvironmentLight* m_defaultLightObject = nullptr;

    bool m_isUniformSeed = true;
    uint32_t m_frameCount = 0;

    bool m_isInteractive = false;
    int m_numSamples = 0;
    int m_numSamplesPerIter = 0;
    int m_activePixels = -1;
    int m_maxSamples = 0;
    int m_minSamples = 0;
    float m_varianceThreshold = 0.0f;
    TfToken m_currentRenderQuality;

    using Duration = std::chrono::high_resolution_clock::duration;
    Duration m_frameRenderTotalTime;
    Duration m_frameResolveTotalTime;

    struct RenderUpdateCallbackData {
        HdRprApiImpl* rprApi = nullptr;
        float previousProgress = 0.0f;
    };
    RenderUpdateCallbackData m_rucData;
    bool m_isRenderUpdateCallbackEnabled;

    enum State {
        kStateUninitialized,
        kStateRender,
        kStateRestartRequired,
        kStateInvalid
    };
    State m_state = kStateUninitialized;

    std::atomic<bool> m_isAbortingEnabled;
    std::atomic<bool> m_abortRender;

    std::string m_rprSceneExportPath;
    bool m_rprExportAsSingleFile;
    bool m_rprExportUseImageCache;

    std::string m_cryptomatteOutputPath;
    bool m_cryptomattePreviewLayer;
    struct CryptomatteAov {
        std::shared_ptr<HdRprApiAov> aov[3];
    };
    struct CryptomatteAovs {
        CryptomatteAov mat;
        CryptomatteAov obj;
    };
    std::unique_ptr<CryptomatteAovs> m_cryptomatteAovs;

    rprContextFlushFrameBuffers_func m_rprContextFlushFrameBuffers = nullptr;
#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
    bool m_vulkanInteropBufferReady = false;
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT

    GfMatrix4d m_unitSizeTransform = GfMatrix4d(1.0);
};

HdRprApi::HdRprApi(HdRprDelegate* delegate) : m_impl(new HdRprApiImpl(delegate)) {

}

HdRprApi::~HdRprApi() {
    delete m_impl;
}

rpr::Shape* HdRprApi::CreateMesh(VtArray<VtVec3fArray> const& pointSamples, VtIntArray const& pointIndexes, VtArray<VtVec3fArray> const& normalSamples, VtIntArray const& normalIndexes, VtArray<VtVec2fArray> const& uvSamples, VtIntArray const& uvIndexes, VtIntArray const& vpf, TfToken const& polygonWinding) {
    m_impl->InitIfNeeded();
    return m_impl->CreateMesh(pointSamples, pointIndexes, normalSamples, normalIndexes, uvSamples, uvIndexes, vpf, polygonWinding);
}

rpr::Shape* HdRprApi::CreateMesh(VtVec3fArray const& points, VtIntArray const& pointIndexes, VtVec3fArray const& normals, VtIntArray const& normalIndexes, VtVec2fArray const& uvs, VtIntArray const& uvIndexes, VtIntArray const& vpf, TfToken const& polygonWinding) {
    m_impl->InitIfNeeded();
    return m_impl->CreateMesh(points, pointIndexes, normals, normalIndexes, uvs, uvIndexes, vpf, polygonWinding);
}

rpr::Curve* HdRprApi::CreateCurve(VtVec3fArray const& points, VtIntArray const& indices, VtFloatArray const& radiuses, VtVec2fArray const& uvs, VtIntArray const& segmentPerCurve) {
    m_impl->InitIfNeeded();
    return m_impl->CreateCurve(points, indices, radiuses, uvs, segmentPerCurve);
}

rpr::Shape* HdRprApi::CreateMeshInstance(rpr::Shape* prototypeMesh) {
    return m_impl->CreateMeshInstance(prototypeMesh);
}

HdRprApiEnvironmentLight* HdRprApi::CreateEnvironmentLight(GfVec3f color, float intensity, BackgroundOverride const& backgroundOverride) {
    m_impl->InitIfNeeded();
    return m_impl->CreateEnvironmentLight(color, intensity, backgroundOverride);
}

HdRprApiEnvironmentLight* HdRprApi::CreateEnvironmentLight(const std::string& prthTotexture, float intensity, BackgroundOverride const& backgroundOverride) {
    m_impl->InitIfNeeded();
    return m_impl->CreateEnvironmentLight(prthTotexture, intensity, backgroundOverride);
}

void HdRprApi::SetTransform(HdRprApiEnvironmentLight* envLight, GfMatrix4f const& transform) {
    m_impl->SetTransform(envLight->light.get(), transform);
}

void HdRprApi::SetTransform(rpr::SceneObject* object, GfMatrix4f const& transform) {
    m_impl->SetTransform(object, transform);
}

void HdRprApi::SetTransform(rpr::Shape* shape, size_t numSamples, float* timeSamples, GfMatrix4d* transformSamples) {
    m_impl->SetTransform(shape, numSamples, timeSamples, transformSamples);
}

void HdRprApi::SetTransform(HdRprApiVolume* volume, GfMatrix4f const& transform) {
    m_impl->SetTransform(volume, transform);
}

rpr::DirectionalLight* HdRprApi::CreateDirectionalLight() {
    m_impl->InitIfNeeded();
    return m_impl->CreateDirectionalLight();
}

rpr::SpotLight* HdRprApi::CreateSpotLight(float angle, float softness) {
    m_impl->InitIfNeeded();
    return m_impl->CreateSpotLight(angle, softness);
}

rpr::PointLight* HdRprApi::CreatePointLight() {
    m_impl->InitIfNeeded();
    return m_impl->CreatePointLight();
}

rpr::DiskLight* HdRprApi::CreateDiskLight() {
    m_impl->InitIfNeeded();
    return m_impl->CreateDiskLight();
}

rpr::SphereLight* HdRprApi::CreateSphereLight() {
    m_impl->InitIfNeeded();
    return m_impl->CreateSphereLight();
}

rpr::IESLight* HdRprApi::CreateIESLight(std::string const& iesFilepath) {
    m_impl->InitIfNeeded();
    return m_impl->CreateIESLight(iesFilepath);
}

void HdRprApi::SetDirectionalLightAttributes(rpr::DirectionalLight* directionalLight, GfVec3f const& color, float shadowSoftnessAngle) {
    m_impl->SetDirectionalLightAttributes(directionalLight, color, shadowSoftnessAngle);
}

void HdRprApi::SetLightRadius(rpr::SphereLight* light, float radius) {
    m_impl->SetLightRadius(light, radius);
}

void HdRprApi::SetLightRadius(rpr::DiskLight* light, float radius) {
    m_impl->SetLightRadius(light, radius);
}

void HdRprApi::SetLightAngle(rpr::DiskLight* light, float angle) {
    m_impl->SetLightAngle(light, angle);
}

void HdRprApi::SetLightColor(rpr::RadiantLight* light, GfVec3f const& color) {
    m_impl->SetLightColor(light, color);
}

RprUsdMaterial* HdRprApi::CreateGeometryLightMaterial(GfVec3f const& emissionColor) {
    return m_impl->CreateGeometryLightMaterial(emissionColor);
}

void HdRprApi::ReleaseGeometryLightMaterial(RprUsdMaterial* material) {
    return m_impl->ReleaseGeometryLightMaterial(material);
}

HdRprApiVolume* HdRprApi::CreateVolume(
    VtUIntArray const& densityCoords, VtFloatArray const& densityValues, VtVec3fArray const& densityLUT, float densityScale,
    VtUIntArray const& albedoCoords, VtFloatArray const& albedoValues, VtVec3fArray const& albedoLUT, float albedoScale,
    VtUIntArray const& emissionCoords, VtFloatArray const& emissionValues, VtVec3fArray const& emissionLUT, float emissionScale,
    const GfVec3i& gridSize, const GfVec3f& voxelSize, const GfVec3f& gridBBLow) {
    m_impl->InitIfNeeded();
    return m_impl->CreateVolume(
        densityCoords, densityValues, densityLUT, densityScale,
        albedoCoords, albedoValues, albedoLUT, albedoScale,
        emissionCoords, emissionValues, emissionLUT, emissionScale,
        gridSize, voxelSize, gridBBLow);
}

RprUsdMaterial* HdRprApi::CreateMaterial(SdfPath const& materialId, HdSceneDelegate* sceneDelegate, HdMaterialNetworkMap const& materialNetwork) {
    m_impl->InitIfNeeded();
    return m_impl->CreateMaterial(materialId, sceneDelegate, materialNetwork);
}

RprUsdMaterial* HdRprApi::CreatePointsMaterial(VtVec3fArray const& colors) {
    m_impl->InitIfNeeded();
    return m_impl->CreatePointsMaterial(colors);
}

RprUsdMaterial* HdRprApi::CreateDiffuseMaterial(GfVec3f const& color) {
    m_impl->InitIfNeeded();

    return m_impl->CreateRawMaterial(RPR_MATERIAL_NODE_UBERV2, {
        {RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR, ToVec4(color, 1.0f)}
    });
}

RprUsdMaterial* HdRprApi::CreatePrimvarColorLookupMaterial(){
    m_impl->InitIfNeeded();
    return m_impl->CreatePrimvarColorLookupMaterial();
}

void HdRprApi::SetMeshRefineLevel(rpr::Shape* mesh, int level) {
    m_impl->SetMeshRefineLevel(mesh, level);
}

void HdRprApi::SetMeshVertexInterpolationRule(rpr::Shape* mesh, TfToken boundaryInterpolation) {
    m_impl->SetMeshVertexInterpolationRule(mesh, boundaryInterpolation);
}

void HdRprApi::SetMeshMaterial(rpr::Shape* mesh, RprUsdMaterial const* material, bool displacementEnabled) {
    m_impl->SetMeshMaterial(mesh, material, displacementEnabled);
}

void HdRprApi::SetMeshVisibility(rpr::Shape* mesh, uint32_t visibilityMask) {
    m_impl->SetMeshVisibility(mesh, visibilityMask);
}

void HdRprApi::SetMeshId(rpr::Shape* mesh, uint32_t id) {
    m_impl->SetMeshId(mesh, id);
}

void HdRprApi::SetMeshIgnoreContour(rpr::Shape* mesh, bool ignoreContour) {
    m_impl->SetMeshIgnoreContour(mesh, ignoreContour);
}

bool HdRprApi::SetMeshVertexColor(rpr::Shape* mesh, VtArray<VtVec3fArray> const& primvarSamples, HdInterpolation interpolation) {
    return m_impl->SetMeshVertexColor(mesh, primvarSamples, interpolation);
}

void HdRprApi::SetCurveMaterial(rpr::Curve* curve, RprUsdMaterial const* material) {
    m_impl->SetCurveMaterial(curve, material);
}

void HdRprApi::SetCurveVisibility(rpr::Curve* curve, uint32_t visibilityMask) {
    m_impl->SetCurveVisibility(curve, visibilityMask);
}

void HdRprApi::Release(HdRprApiEnvironmentLight* envLight) {
    m_impl->Release(envLight);
}

void HdRprApi::Release(RprUsdMaterial* material) {
    m_impl->Release(material);
}

void HdRprApi::Release(HdRprApiVolume* volume) {
    m_impl->Release(volume);
}

void HdRprApi::Release(rpr::Light* light) {
    m_impl->Release(light);
}

void HdRprApi::Release(rpr::Shape* shape) {
    m_impl->Release(shape);
}

void HdRprApi::Release(rpr::Curve* curve) {
    m_impl->Release(curve);
}

void HdRprApi::SetName(rpr::ContextObject* object, const char* name) {
    m_impl->SetName(object, name);
}

void HdRprApi::SetName(RprUsdMaterial* object, const char* name) {
    m_impl->SetName(object, name);
}

void HdRprApi::SetName(HdRprApiEnvironmentLight* object, const char* name) {
    m_impl->SetName(object, name);
}

void HdRprApi::SetCamera(HdCamera const* camera) {
    m_impl->SetCamera(camera);
}

HdCamera const* HdRprApi::GetCamera() const {
    return m_impl->GetCamera();
}

GfMatrix4d HdRprApi::GetCameraViewMatrix() const {
    return m_impl->GetCameraViewMatrix();
}

const GfMatrix4d& HdRprApi::GetCameraProjectionMatrix() const {
    return m_impl->GetCameraProjectionMatrix();
}

GfVec2i HdRprApi::GetViewportSize() const {
    return m_impl->GetViewportSize();
}

void HdRprApi::SetViewportSize(GfVec2i const& size) {
    m_impl->SetViewportSize(size);
}

void HdRprApi::SetAovBindings(HdRenderPassAovBindingVector const& aovBindings) {
    m_impl->InitIfNeeded();
    m_impl->SetAovBindings(aovBindings);
}

HdRenderPassAovBindingVector HdRprApi::GetAovBindings() const {
    return m_impl->GetAovBindings();
}

void HdRprApi::CommitResources() {
    m_impl->CommitResources();
}

void HdRprApi::Resolve(SdfPath const& aovId) {
    m_impl->Resolve(aovId);
}

void HdRprApi::Render(HdRprRenderThread* renderThread) {
    m_impl->Render(renderThread);
}

void HdRprApi::AbortRender() {
    m_impl->AbortRender();
}

bool HdRprApi::IsChanged() const {
    return m_impl->IsChanged();
}

HdRprApi::RenderStats HdRprApi::GetRenderStats() const {
    return m_impl->GetRenderStats();
}

bool HdRprApi::IsGlInteropEnabled() const {
    return m_impl->IsGlInteropEnabled();
}

bool HdRprApi::IsVulkanInteropEnabled() const {
    return m_impl->IsVulkanInteropEnabled();
}

bool HdRprApi::IsArbitraryShapedLightSupported() const {
    m_impl->InitIfNeeded();
    return m_impl->IsArbitraryShapedLightSupported();
}

bool HdRprApi::IsSphereAndDiskLightSupported() const {
    m_impl->InitIfNeeded();
    return m_impl->IsSphereAndDiskLightSupported();
}

TfToken const& HdRprApi::GetCurrentRenderQuality() const {
    return m_impl->GetCurrentRenderQuality();
}

rpr::FrameBuffer* HdRprApi::GetRawColorFramebuffer() {
    return m_impl->GetRawColorFramebuffer();
}

rpr::FrameBuffer* HdRprApi::GetPrimIdFramebuffer() {
    return m_impl->GetPrimIdFramebuffer();
}

void HdRprApi::SetInteropInfo(void* interopInfo) {
    m_impl->SetInteropInfo(interopInfo);

    // Temporary should be force inited here, because otherwise has issues with GPU synchronization
    m_impl->InitIfNeeded();
}

#ifdef HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT
bool HdRprApi::GetInteropSemaphore(VkSemaphore& rInteropSemaphore, uint32_t& rInteropSemaphoreIndex) {
    return m_impl->GetInteropSemaphore(rInteropSemaphore, rInteropSemaphoreIndex);
}
#endif // HDRPR_ENABLE_VULKAN_INTEROP_SUPPORT

void HdRprApi::Restart()
{
    m_impl->Restart();
}

PXR_NAMESPACE_CLOSE_SCOPE
