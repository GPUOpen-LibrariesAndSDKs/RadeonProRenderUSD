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

#include "volume.h"
#include "field.h"
#include "rprApi.h"
#include "renderParam.h"

#include "RPRLibs/pluginUtils.hpp"

#include "houdini/openvdb.h"

#include "pxr/base/gf/range1f.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/usdLux/blackbody.h"
#include "pxr/usd/usdVol/tokens.h"

#include <openvdb/openvdb.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    HdRprVolumeTokens,
    (color)
    (density)
    (temperature)
    (scatteringColor)
    (transmissionColor)
    (emissionColor)
    (anisotropy)
    (multipleScattering)
    (normalize)
    (bias)
    (gain)
    (scale)
    (ramp)
    (blackbodyMode)
    (physical)
    (artistic)
);

/*

Volume Parameters:
- scatteringColor - vec3f - vec3(1) - scattering color.
- transmissionColor - vec3f - vec3(1) - transmission color.
- emissionColor - vec3f - vec3(1) - emissive color.
- anisotropy - float - 0.0 - forward or back scattering.
- multipleScattering - bool - false - whether to apply multiple scatter calculations.

Common Field Parameters:
- normalize - bool - false - whether fieldValue should be normalized
- scale - float - 1.0 - scale to be applied to value before lookup table. `fieldColor = LUT(scale * fieldValue)`
- gain - float - 1.0 - gain to be applied to LUT values. `LUT(x) = LUT(x) * gain + bias`
- bias - float - 0.0 - bias to be applied to LUT values. `LUT(x) = LUT(x) * gain + bias`
- ramp - vec3fArray - [vec3(0), vec3(1)] - value lookup table.

Temperature Field Parameters:
- blackbodyMode - TfToken - auto:
    * physical mode: temperature interpreted as pure physical values - the temperature in Kelvins. The temperature will be normalized by some max temperature and used as an emissive lookup. The emission lookup table will be filled with blackbody colors in the range [0, maxTemperature].
    * artistic: temperature is directly passed to an emission lookup table.
    * auto: select physical or artistic mode depending on file metadata

*/

namespace {

const int kLookupTableGranularityLevel = 64;
const float defaultDensity = 100.f; // RPR take density value of 100 as fully opaque
GfVec3f defaultColor = GfVec3f(0.18f);
GfVec3f defaultEmission = GfVec3f(0.0f); // Default to no emission

HdRprApi::VolumeMaterialParameters ParseVolumeMaterialParameters(HdSceneDelegate* sceneDelegate, SdfPath const& volumeId) {
    HdRprApi::VolumeMaterialParameters params;
    auto value = sceneDelegate->Get(volumeId, HdRprVolumeTokens->scatteringColor);
    auto t = value.GetTypeName();
    params.scatteringColor = sceneDelegate->Get(volumeId, HdRprVolumeTokens->scatteringColor).GetWithDefault(params.scatteringColor);
    params.transmissionColor = sceneDelegate->Get(volumeId, HdRprVolumeTokens->transmissionColor).GetWithDefault(params.transmissionColor);
    params.emissionColor = sceneDelegate->Get(volumeId, HdRprVolumeTokens->emissionColor).GetWithDefault(params.emissionColor);
    params.density = sceneDelegate->Get(volumeId, HdRprVolumeTokens->density).GetWithDefault(params.density);
    params.anisotropy = sceneDelegate->Get(volumeId, HdRprVolumeTokens->anisotropy).GetWithDefault(params.anisotropy);
    params.multipleScattering = sceneDelegate->Get(volumeId, HdRprVolumeTokens->multipleScattering).GetWithDefault(params.multipleScattering);
    return params;
}

struct GridParameters {
    bool normalize = false;
    float bias = 0.0f;
    float gain = 1.0f;
    float scale = 1.0f;
    VtVec3fArray ramp;

    enum {
        kNormalizeAuthored = 1 << 0,
        kBiasAuthored = 1 << 1,
        kGainAuthored = 1 << 2,
        kScaleAuthored = 1 << 3,
        kRampAuthored = 1 << 4,
    };
    uint32_t authoredParamsMask = 0;
};

template <typename T>
bool ParseGridParameter(TfToken const& name, HdSceneDelegate* sceneDelegate, SdfPath const& fieldId, T* param) {
    auto value = sceneDelegate->Get(fieldId, name);
    if (value.IsHolding<T>()) {
        *param = value.UncheckedGet<T>();
        return true;
    }
    return false;
}

GridParameters ParseGridParameters(HdSceneDelegate* sceneDelegate, SdfPath const& fieldId) {
    GridParameters params;
    if (ParseGridParameter(HdRprVolumeTokens->normalize, sceneDelegate, fieldId, &params.normalize)) params.authoredParamsMask |= GridParameters::kNormalizeAuthored;
    if (ParseGridParameter(HdRprVolumeTokens->bias, sceneDelegate, fieldId, &params.bias)) params.authoredParamsMask |= GridParameters::kBiasAuthored;
    if (ParseGridParameter(HdRprVolumeTokens->gain, sceneDelegate, fieldId, &params.gain)) params.authoredParamsMask |= GridParameters::kGainAuthored;
    if (ParseGridParameter(HdRprVolumeTokens->scale, sceneDelegate, fieldId, &params.scale)) params.authoredParamsMask |= GridParameters::kScaleAuthored;
    if (ParseGridParameter(HdRprVolumeTokens->ramp, sceneDelegate, fieldId, &params.ramp)) params.authoredParamsMask |= GridParameters::kRampAuthored;
    return params;
}

enum class BlackbodyMode {
    kAuto,
    kPhysical,
    kArtistic,
};

BlackbodyMode ParseGridBlackbodyMode(HdSceneDelegate* sceneDelegate, SdfPath const& fieldId) {
    auto modeValue = sceneDelegate->Get(fieldId, HdRprVolumeTokens->blackbodyMode);
    if (modeValue.IsHolding<TfToken>()) {
        auto& modeToken = modeValue.UncheckedGet<TfToken>();
        if (modeToken == HdRprVolumeTokens->physical) {
            return BlackbodyMode::kPhysical;
        } else if (modeToken == HdRprVolumeTokens->artistic) {
            return BlackbodyMode::kArtistic;
        }
    }

    return BlackbodyMode::kAuto;
}

struct GridInfo {
    std::string filepath;
    openvdb::FloatGrid const* vdbGrid = nullptr;
    HdVolumeFieldDescriptor const* desc;
    GridParameters params;
};

void ParseOpenvdbMetadata(GridInfo* grid) {
    auto isAllParametersParsed = [](GridInfo* grid) {
        // We parse only these parameters from .vdb file metadata
        static constexpr auto metadataParameters = GridParameters::kRampAuthored | GridParameters::kScaleAuthored;
        return (grid->params.authoredParamsMask & metadataParameters) == metadataParameters;
    };
    if (isAllParametersParsed(grid)) {
        return;
    }

    std::string metadataNamePrefix;
    if (grid->desc->fieldName == HdRprVolumeTokens->temperature) {
        metadataNamePrefix = "volvis_emit";
    } else if (grid->desc->fieldName == HdRprVolumeTokens->density) {
        metadataNamePrefix = "volvis_density";
    }

    auto cdrampMd = metadataNamePrefix + "cdramp";
    auto scaleMd = metadataNamePrefix + "scale";

    try {
        openvdb::io::File file(grid->filepath);
        file.open();

        auto metadata = file.getMetadata();
        for (auto it = metadata->beginMeta(); it != metadata->endMeta() && !isAllParametersParsed(grid); ++it) {
            if (it->first == cdrampMd) {
                if (grid->params.authoredParamsMask & GridParameters::kRampAuthored) {
                    continue;
                }

                try {
                    auto root = json::parse(it->second->str());
                    if (root["colortype"] == "RGB") {
                        auto points = root["points"];
                        auto pointsIt = points.begin();

                        // First element is always number of points
                        int numPoints = pointsIt->get<int>();
                        if (numPoints <= 0) {
                            TF_RUNTIME_ERROR("Failed to parse openvdb metadata \"%s\": invalid %s - incorrect number of points %d", grid->filepath.c_str(), cdrampMd.c_str(), numPoints);
                            continue;
                        }
                        ++pointsIt;

                        std::vector<float> parameters;
                        std::vector<GfVec3f> colors;

                        parameters.reserve(std::min(64, numPoints));
                        colors.reserve(std::min(64, numPoints));

                        for (; pointsIt != points.end(); ++pointsIt) {
                            if (numPoints == 0) {
                                TF_RUNTIME_ERROR("Failed to parse openvdb metadata \"%s\": invalid %s - excessive number of points", grid->filepath.c_str(), cdrampMd.c_str());
                                continue;
                            }

                            auto& point = (*pointsIt);
                            parameters.push_back(point["t"].get<float>());

                            GfVec3f color;
                            auto rgba = point["rgba"];
                            for (int i = 0; i < 3; ++i) {
                                color[i] = rgba[i].get<float>();
                            }
                            colors.push_back(color);

                            numPoints--;
                        }

                        if (numPoints != 0) {
                            TF_RUNTIME_ERROR("Failed to parse openvdb metadata \"%s\": invalid %s - insufficient number of points", grid->filepath.c_str(), cdrampMd.c_str());
                            continue;
                        }

                        // RPR expects linearly interpolated ramp
                        // Houdini's ramp is defined as parameter-color pair (the parameter is in [0; 1] range)
                        // Here we convert arbitrarily distributed color ramp to a linear ramp
                        auto& ramp = grid->params.ramp;
                        ramp.reserve(kLookupTableGranularityLevel);
                        for (int i = 0; i < kLookupTableGranularityLevel; ++i) {
                            float t = static_cast<float>(i) / (kLookupTableGranularityLevel - 1);
                            ramp.push_back(HdResampleRawTimeSamples(t, parameters.size(), parameters.data(), colors.data()));
                        }
                        grid->params.authoredParamsMask |= GridParameters::kRampAuthored;
                    }
                } catch (json::exception& e) {
                    TF_RUNTIME_ERROR("Failed to parse openvdb metadata \"%s\": invalid %s - %s", grid->filepath.c_str(), cdrampMd.c_str(), e.what());
                }
            } else if (it->first == scaleMd) {
                if (grid->params.authoredParamsMask & GridParameters::kScaleAuthored) {
                    continue;
                }

                if (it->second->typeName() == "float") {
                    try {
                        grid->params.scale *= std::stof(it->second->str()) * 0.01f;
                        grid->params.authoredParamsMask |= GridParameters::kScaleAuthored;
                    } catch (std::exception& e) {
                        TF_RUNTIME_ERROR("Failed to parse openvdb metadata \"%s\": invalid %s - %s", grid->filepath.c_str(), scaleMd.c_str(), e.what());
                    }
                }
            }
        }
    } catch (openvdb::IoError const& e) {
        TF_RUNTIME_ERROR("Failed to parse openvdb metadata \"%s\": %s", grid->filepath.c_str(), e.what());
    }
}

VDBGrid<float> CopyGridTopology(VDBGrid<float> const& from) {
    static const float kFillValue = 0.0f;

    VDBGrid<float> newGrid;
    newGrid.gridSizeX = from.gridSizeX;
    newGrid.gridSizeY = from.gridSizeY;
    newGrid.gridSizeZ = from.gridSizeZ;
    newGrid.coords = from.coords;
    newGrid.values = VtFloatArray(from.values.size(), kFillValue);
    newGrid.LUT = from.LUT;
    newGrid.maxValue = kFillValue;
    newGrid.minValue = kFillValue;

    return newGrid;
}

void NormalizeGrid(VDBGrid<float>* grid) {
    if ((GfIsClose(grid->minValue, 0.0f, 1e-3f) && GfIsClose(grid->maxValue, 1.0f, 1e-3f)) ||
        GfIsClose(grid->minValue, grid->maxValue, 1e-6f)) {
        return;
    }

    float offset = -grid->minValue;
    float scale = 1.0f / (grid->maxValue - grid->minValue);
    for (auto& value : grid->values) {
        value = value * scale + offset;
    }

    grid->minValue = 0.0f;
    grid->maxValue = 1.0f;
}

bool IsInMemoryVdb(std::string const& filepath) {
    static std::string opPrefix("op:");
    return filepath.compare(0, opPrefix.size(), opPrefix) == 0;
}

} // namespace anonymous

HdRprVolume::HdRprVolume(SdfPath const& id)
    : HdVolume(id) {

}

void HdRprVolume::Sync(
    HdSceneDelegate* sceneDelegate,
    HdRenderParam* renderParam,
    HdDirtyBits* dirtyBits,
    TfToken const& reprName) {

    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    auto& id = GetId();

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        m_transform = GfMatrix4f(sceneDelegate->GetTransform(id));
    }

    bool newVolume = false;

    if (*dirtyBits & HdChangeTracker::DirtyTopology) {
        if (m_rprVolume) {
            rprApi->Release(m_rprVolume);
        }
        m_rprVolume = nullptr;

        openvdb::initialize();
        std::map<std::string, openvdb::GridBase::Ptr> retainedVDBGrids;

        auto getVdbGrid = [&](SdfPath const& fieldId, std::string const& openvdbPath) -> openvdb::FloatGrid const* {
            auto fieldName = sceneDelegate->Get(fieldId, UsdVolTokens->fieldName).GetWithDefault(TfToken());
            if (IsInMemoryVdb(openvdbPath)) {
                auto houdiniGrid = HoudiniOpenvdbLoader::Instance().GetGrid(openvdbPath.c_str(), fieldName.GetText());
                if (houdiniGrid->type() != openvdb::FloatGrid::gridType()) {
                    TF_RUNTIME_ERROR("[%s] Failed to read vdb grid \"%s\": RPR supports scalar fields only", id.GetName().c_str(), openvdbPath.c_str());
                    return nullptr;
                }
                return static_cast<openvdb::FloatGrid const*>(houdiniGrid);
            } else {
                auto gridId = openvdbPath + fieldName.GetString();
                auto gridIter = retainedVDBGrids.find(gridId);
                if (gridIter != retainedVDBGrids.end()) {
                    return static_cast<openvdb::FloatGrid const*>(gridIter->second.get());
                }

                try {
                    openvdb::io::File file(openvdbPath);
                    file.open();
                    auto grid = file.readGrid(fieldName.GetString());
                    if (grid->type() != openvdb::FloatGrid::gridType()) {
                        TF_RUNTIME_ERROR("[%s] Failed to read vdb grid from file \"%s\": RPR supports scalar fields only", id.GetName().c_str(), openvdbPath.c_str());
                        return nullptr;
                    }
                    auto ret = static_cast<openvdb::FloatGrid const*>(grid.get());
                    retainedVDBGrids[gridId] = std::move(grid);
                    return ret;
                } catch (openvdb::Exception const& e) {
                    TF_RUNTIME_ERROR("[%s] Failed to read vdb grid from file \"%s\": %s", id.GetName().c_str(), openvdbPath.c_str(), e.what());
                }
            }

            return nullptr;
        };

        GridInfo densityGridInfo;
        GridInfo emissionGridInfo;
        GridInfo albedoGridInfo;

        decltype(m_fieldSubscriptions) activeFieldSubscriptions;

        auto volumeFieldDescriptorVector = sceneDelegate->GetVolumeFieldDescriptors(GetId());
        for (auto const& desc : volumeFieldDescriptorVector) {
            GridInfo* targetInfo;
            if (desc.fieldName == HdRprVolumeTokens->density) {
                targetInfo = &densityGridInfo;
            } else if (desc.fieldName == HdRprVolumeTokens->temperature) {
                targetInfo = &emissionGridInfo;
            } else if (desc.fieldName == HdRprVolumeTokens->color) {
                targetInfo = &albedoGridInfo;
            } else {
                continue;
            }

            auto param = sceneDelegate->Get(desc.fieldId, UsdVolTokens->filePath);
            if (param.IsHolding<SdfAssetPath>()) {
                targetInfo->desc = &desc;

                auto& assetPath = param.UncheckedGet<SdfAssetPath>();
                if (!assetPath.GetResolvedPath().empty()) {
                    targetInfo->filepath = assetPath.GetResolvedPath();
                } else {
                    targetInfo->filepath = assetPath.GetAssetPath();
                }

                targetInfo->vdbGrid = getVdbGrid(desc.fieldId, targetInfo->filepath);
                if (targetInfo->vdbGrid) {
                    targetInfo->params = ParseGridParameters(sceneDelegate, desc.fieldId);
                    ParseOpenvdbMetadata(targetInfo);

                    // Subscribe for field updates, more info in renderParam.h
                    auto fieldSubscription = m_fieldSubscriptions.find(desc.fieldId);
                    if (fieldSubscription == m_fieldSubscriptions.end()) {
                        activeFieldSubscriptions.emplace(desc.fieldId, rprRenderParam->SubscribeVolumeForFieldUpdates(this, desc.fieldId));
                    } else {
                        // Reuse the old one
                        activeFieldSubscriptions.emplace(desc.fieldId, std::move(fieldSubscription->second));
                    }
                }
            }
        }

        m_fieldSubscriptions.clear();
        std::swap(m_fieldSubscriptions, activeFieldSubscriptions);

        auto densityGrid = densityGridInfo.vdbGrid;
        auto emissionGrid = emissionGridInfo.vdbGrid;
        auto albedoGrid = albedoGridInfo.vdbGrid;

        if (!densityGrid && !emissionGrid) {
            TF_RUNTIME_ERROR("[Node: %s]: does not have the needed grids.", GetId().GetName().c_str());
            *dirtyBits = HdChangeTracker::Clean;
            return;
        }

        // If we need to read from both grids, check compatibility
        if (densityGridInfo.vdbGrid && emissionGrid) {
            if (densityGridInfo.vdbGrid->voxelSize() != emissionGrid->voxelSize())
                TF_RUNTIME_ERROR("[Node: %s]: density grid and temperature grid differs in voxel sizes. Taking voxel size of density grid", GetId().GetName().c_str());
            if (densityGridInfo.vdbGrid->transform() != emissionGrid->transform())
                TF_RUNTIME_ERROR("[Node: %s]: density grid and temperature grid have different transform. Taking transform of density grid", GetId().GetName().c_str());
        }

        openvdb::Vec3d voxelSize = densityGrid ? densityGrid->voxelSize() : emissionGrid->voxelSize();
        openvdb::math::Transform gridTransform = densityGrid ? densityGrid->transform() : emissionGrid->transform();
        openvdb::CoordBBox activeVoxelsBB;
        if (densityGrid) activeVoxelsBB.expand(densityGrid->evalActiveVoxelBoundingBox());
        if (emissionGrid) activeVoxelsBB.expand(emissionGrid->evalActiveVoxelBoundingBox());
        if (albedoGrid) activeVoxelsBB.expand(albedoGrid->evalActiveVoxelBoundingBox());
        openvdb::Coord activeVoxelsBBSize = activeVoxelsBB.extents();

        VDBGrid<float> densityGridData;
        VDBGrid<float> emissionGridData;
        VDBGrid<float> albedoGridData;

        if (densityGrid) {
            ProcessVDBGrid(densityGridData, densityGridInfo.vdbGrid, activeVoxelsBB);

            if (densityGridInfo.params.ramp.empty()) {
                if ((densityGridInfo.params.authoredParamsMask & GridParameters::kNormalizeAuthored) == 0) {
                    densityGridInfo.params.normalize = true;
                }

                densityGridInfo.params.ramp.push_back(GfVec3f(densityGridData.minValue));
                densityGridInfo.params.ramp.push_back(GfVec3f(densityGridData.maxValue));
            }

            if (densityGridInfo.params.normalize) {
                NormalizeGrid(&densityGridData);
            }
        }

        if (emissionGrid) {
            auto blackbodyMode = ParseGridBlackbodyMode(sceneDelegate, emissionGridInfo.desc->fieldId);
            if (blackbodyMode == BlackbodyMode::kPhysical ||
                (blackbodyMode == BlackbodyMode::kAuto && (emissionGridInfo.params.authoredParamsMask & GridParameters::kRampAuthored) == 0)) {
                emissionGridInfo.params.ramp.clear();
                emissionGridInfo.params.ramp.reserve(kLookupTableGranularityLevel);
                for (int i = 0; i < kLookupTableGranularityLevel; ++i) {
                    float parameter = static_cast<float>(i) / (kLookupTableGranularityLevel - 1);

                    static constexpr int kMaxTemperature = 10000;
                    float temperature = parameter * kMaxTemperature;

                    GfVec3f color = UsdLuxBlackbodyTemperatureAsRgb(temperature);
                    if (temperature <= 1000) {
                        color *= temperature / 1000.0f;
                        color *= temperature / 1000.0f;
                    }
                    emissionGridInfo.params.ramp.push_back(color);
                }
            } else if (emissionGridInfo.params.ramp.empty()) {
                emissionGridInfo.params.ramp.push_back(GfVec3f(0.0f));
                emissionGridInfo.params.ramp.push_back(GfVec3f(1.0f));
            }

            ProcessVDBGrid(emissionGridData, emissionGridInfo.vdbGrid, activeVoxelsBB);

            if (emissionGridInfo.params.normalize) {
                NormalizeGrid(&emissionGridData);
            }
        }

        if (albedoGrid) {
            ProcessVDBGrid(albedoGridData, albedoGridInfo.vdbGrid, activeVoxelsBB);
            if (albedoGridInfo.params.normalize) {
                NormalizeGrid(&albedoGridData);
            }
            if (albedoGridInfo.params.ramp.empty()) {
                albedoGridInfo.params.ramp.push_back(defaultColor);
            }
        }

        if (densityGridData.coords.empty()) {
            densityGridData = CopyGridTopology(emissionGridData);
            densityGridInfo.params.ramp.push_back(GfVec3f(defaultDensity));
        }

        if (emissionGridData.coords.empty()) {
            emissionGridData = CopyGridTopology(densityGridData);
            emissionGridInfo.params.ramp.push_back(defaultEmission);
        }

        if (albedoGridData.coords.empty()) {
            albedoGridData = CopyGridTopology(densityGrid ? densityGridData : emissionGridData);
            albedoGridInfo.params.ramp.push_back(defaultColor);
        }

        for (auto& value : densityGridInfo.params.ramp) {
            value = value * densityGridInfo.params.gain + GfVec3f(densityGridInfo.params.bias);
        }
        for (auto& value : emissionGridInfo.params.ramp) {
            value = value * emissionGridInfo.params.gain + GfVec3f(emissionGridInfo.params.bias);
        }
        for (auto& value : albedoGridInfo.params.ramp) {
            value = value * albedoGridInfo.params.gain + GfVec3f(albedoGridInfo.params.bias);
        }

        openvdb::Vec3d gridMin = gridTransform.indexToWorld(activeVoxelsBB.min());
        GfVec3f gridBBLow((float)(gridMin.x() - voxelSize[0] / 2), (float)(gridMin.y() - voxelSize[1] / 2), (float)(gridMin.z() - voxelSize[2] / 2));
        GfVec3f voxelSizeGf(voxelSize.x(), voxelSize.y(), voxelSize.z());

        auto volumeMaterialParams = ParseVolumeMaterialParameters(sceneDelegate, id);
        m_rprVolume = rprApi->CreateVolume(
            densityGridData.coords, densityGridData.values, densityGridInfo.params.ramp, densityGridInfo.params.scale,
            albedoGridData.coords, albedoGridData.values, albedoGridInfo.params.ramp, albedoGridInfo.params.scale, 
            emissionGridData.coords, emissionGridData.values, emissionGridInfo.params.ramp, emissionGridInfo.params.scale,
            GfVec3i(activeVoxelsBBSize.asPointer()), voxelSizeGf, gridBBLow, volumeMaterialParams);
        newVolume = m_rprVolume != nullptr;
    }

    if (m_rprVolume) {
        if (newVolume || (*dirtyBits & HdChangeTracker::DirtyTransform)) {
            rprApi->SetTransform(m_rprVolume, m_transform);
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdRprVolume::GetInitialDirtyBitsMask() const {
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyMaterialId
        | HdChangeTracker::AllDirty;

    return (HdDirtyBits)mask;
}

HdDirtyBits HdRprVolume::_PropagateDirtyBits(HdDirtyBits bits) const {
    return bits;
}

void HdRprVolume::_InitRepr(TfToken const& reprName,
                       HdDirtyBits* dirtyBits) {
    TF_UNUSED(reprName);
    TF_UNUSED(dirtyBits);

    // No-op
}

void HdRprVolume::Finalize(HdRenderParam* renderParam) {
    static_cast<HdRprRenderParam*>(renderParam)->AcquireRprApiForEdit()->Release(m_rprVolume);
    m_rprVolume = nullptr;

    HdVolume::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
