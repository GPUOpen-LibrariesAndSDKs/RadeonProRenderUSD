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

#include "volume.h"
#include "rprApi.h"
#include "renderParam.h"

#include "houdini/openvdb.h"

#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/usdLux/blackbody.h"

#include <openvdb/openvdb.h>
#include <openvdb/points/PointDataGrid.h>
#include <openvdb/tools/Interpolation.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    HdRprVolumeTokens,
    (filePath) \
    (density) \
    (color) \
    (points) \
    (temperatureOffset) \
    (temperatureScale) \
    (emissive)
);

namespace {

template<typename GridType>
inline GridType const* openvdbGridCast(openvdb::GridBase const* grid) {
    if (grid && grid->type() == GridType::gridType()) {
        return static_cast<GridType const*>(grid);
    }
    return nullptr;
}

const float defaultDensity = 100.f;      // RPR take density value of 100 as fully opaque
GfVec3f defaultColor = GfVec3f(0.0f);    // Default color of black
GfVec3f defaultEmission = GfVec3f(0.0f); // Default to no emission

} // namespace anonymous

void ReadFloatGrid(openvdb::FloatGrid const* grid, const openvdb::Coord& coordOffset, float valueOffset, float valueScale, std::vector<uint32_t>& outDensityGridOnIndices, std::vector<float>& outDensityGridOnValueIndices) {
    for (auto iter = grid->beginValueOn(); iter; ++iter) {
        openvdb::Coord curCoord = iter.getCoord() + coordOffset;
        outDensityGridOnIndices.push_back(curCoord.x());
        outDensityGridOnIndices.push_back(curCoord.y());
        outDensityGridOnIndices.push_back(curCoord.z());

        float value = (float)(grid->getAccessor().getValue(iter.getCoord()));
        outDensityGridOnValueIndices.push_back((value + valueOffset) * valueScale);
    }
}

struct GridData {
    std::vector<float> values;
    std::vector<float> valueLUT;
    std::vector<uint32_t> indices;

    void DuplicateWithUniformValue(GridData& target, float valueChannel0, float valueChannel1, float valueChannel2) const {
        target.indices = indices;

        //color grid has one uniform color
        target.valueLUT.clear();
        target.valueLUT.push_back(valueChannel0);
        target.valueLUT.push_back(valueChannel1);
        target.valueLUT.push_back(valueChannel2);

        target.values.resize(values.size(), 0);
    }
};

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

    if (*dirtyBits & HdChangeTracker::DirtyTopology) {
        if (m_rprVolume) {
            rprApi->Release(m_rprVolume);
        }
        m_rprVolume = nullptr;

        openvdb::initialize();
        std::map<std::string, openvdb::GridBase::Ptr> openvdbFileGrids;

        auto getVdbGrid = [&id, &openvdbFileGrids]
            (SdfAssetPath const& assetPath, const char* name) -> openvdb::GridBase const* {
            if (assetPath.GetAssetPath().compare(0, sizeof("op:") - 1, "op:") == 0) {
                return HoudiniOpenvdbLoader::Instance().GetGrid(assetPath.GetAssetPath().c_str(), name);
            } else {
                std::string openvdbPath;
                if (assetPath.GetResolvedPath().empty()) {
                    openvdbPath = ArGetResolver().Resolve(assetPath.GetAssetPath());
                } else {
                    openvdbPath = assetPath.GetResolvedPath();
                }

                auto gridId = openvdbPath + name;
                auto gridIter = openvdbFileGrids.find(gridId);
                if (gridIter != openvdbFileGrids.end()) {
                    return gridIter->second.get();
                }

                try {
                    openvdb::io::File file(openvdbPath);
                    file.open();
                    auto grid = file.readGrid(name);
                    auto ret = grid.get();
                    openvdbFileGrids[gridId] = std::move(grid);
                    return ret;
                } catch (openvdb::IoError const& e) {
                    TF_RUNTIME_ERROR("[%s] Failed to open vdb file \"%s\": %s", id.GetName().c_str(), openvdbPath.c_str(), e.what());
                }
            }

            return nullptr;
        };

        std::map<TfToken, openvdb::GridBase const*> fieldGrids;
        float temperatureOffset = 0.0f;
        float temperatureScale = 1.0f;

        auto volumeFieldDescriptorVector = sceneDelegate->GetVolumeFieldDescriptors(GetId());
        for (auto const& desc : sceneDelegate->GetVolumeFieldDescriptors(GetId())) {
            auto param = sceneDelegate->Get(desc.fieldId, HdRprVolumeTokens->filePath);
            if (param.IsHolding<SdfAssetPath>()) {
                if (desc.fieldName == HdRprVolumeTokens->color) {
                    // XXX: Currently we only track color field presence
                    // to know if we need to generate color grid from temperature grid.
                    // Original minghao implementation.
                    // More testing assets required.
                    fieldGrids[desc.fieldName] = nullptr;
                } else {
                    fieldGrids[desc.fieldName] = getVdbGrid(param.UncheckedGet<SdfAssetPath>(), desc.fieldName.GetText());
                }
            }

            if (desc.fieldName == HdRprVolumeTokens->emissive) {
                temperatureOffset = sceneDelegate->Get(desc.fieldId, HdRprVolumeTokens->temperatureOffset).GetWithDefault(0.0f);
                temperatureScale = sceneDelegate->Get(desc.fieldId, HdRprVolumeTokens->temperatureScale).GetWithDefault(1.0f);
            }
        }

        if (fieldGrids.empty()) {
            *dirtyBits = HdChangeTracker::Clean;
            return;
        }

        auto colorGridIter = fieldGrids.find(HdRprVolumeTokens->color);
        auto densityGridIter = fieldGrids.find(HdRprVolumeTokens->density);
        auto temperatureGridIter = fieldGrids.find(HdRprVolumeTokens->emissive);

        bool hasColor = colorGridIter != fieldGrids.end();
        bool hasDensity = densityGridIter != fieldGrids.end();
        bool hasTemperature = temperatureGridIter != fieldGrids.end();

        auto densityGrid = hasDensity ? openvdbGridCast<openvdb::FloatGrid>(densityGridIter->second) : nullptr;
        auto temperatureGrid = hasTemperature ? openvdbGridCast<openvdb::FloatGrid>(temperatureGridIter->second) : nullptr;

        if (hasDensity && !densityGrid) {
            TF_RUNTIME_ERROR("[Node: %s]: vdb density grid doesn't have float type.", GetId().GetName().c_str());
            hasDensity = false;
        }

        if (hasTemperature && !temperatureGrid) {
            TF_RUNTIME_ERROR("[Node: %s]: vdb temperature grid doesn't have float type.", GetId().GetName().c_str());
            hasTemperature = false;
        }

        if (!hasDensity && !hasTemperature) {
            TF_RUNTIME_ERROR("[Node: %s]: does not have the needed grids.", GetId().GetName().c_str());
            *dirtyBits = HdChangeTracker::Clean;
            return;
        }

        //If we need to read from both grids, check compatibility
        if (hasDensity && hasTemperature) {
            if (densityGrid->voxelSize() != temperatureGrid->voxelSize())
                TF_RUNTIME_ERROR("[Node: %s]: density grid and temperature grid differs in voxel sizes. Taking voxel size of density grid", GetId().GetName().c_str());
            if (densityGrid->transform() != temperatureGrid->transform())
                TF_RUNTIME_ERROR("[Node: %s]: density grid and temperature grid have different transform. Taking transform of density grid", GetId().GetName().c_str());
        }

        openvdb::Vec3d voxelSize = hasDensity ? densityGrid->voxelSize() : temperatureGrid->voxelSize();
        openvdb::math::Transform gridTransform = hasDensity ? densityGrid->transform() : temperatureGrid->transform();
        openvdb::CoordBBox gridOnBB;
        if (hasDensity)
            gridOnBB.expand(densityGrid->evalActiveVoxelBoundingBox());
        if (hasTemperature)
            gridOnBB.expand(temperatureGrid->evalActiveVoxelBoundingBox());
        openvdb::Coord gridOnBBSize = gridOnBB.extents();

        GridData srcDensityGridData;
        GridData srcTemperatureGridData;
        GridData defaultEmissionGridData;
        GridData defaultColorGridData;
        GridData defaultDensityGridData;

        GridData* pDensityGridData = nullptr;
        GridData* pColorGridData = nullptr;
        GridData* pEmissiveGridData = nullptr;

        if (hasDensity) {
            float minVal, maxVal;
            densityGrid->evalMinMax(minVal, maxVal);
            float valueScale = (maxVal <= minVal) ? 1.0f : (1.0f / (maxVal - minVal));
            ReadFloatGrid(densityGrid, -gridOnBB.min(), -minVal, valueScale, srcDensityGridData.indices, srcDensityGridData.values);
            srcDensityGridData.valueLUT.push_back(minVal);
            srcDensityGridData.valueLUT.push_back(minVal);
            srcDensityGridData.valueLUT.push_back(minVal);
            srcDensityGridData.valueLUT.push_back(maxVal);
            srcDensityGridData.valueLUT.push_back(maxVal);
            srcDensityGridData.valueLUT.push_back(maxVal);

            pDensityGridData = &srcDensityGridData;
        }

        if (hasTemperature) {
            ReadFloatGrid(temperatureGrid, -gridOnBB.min(), temperatureOffset, temperatureScale / 12000.0f, srcTemperatureGridData.indices, srcTemperatureGridData.values);
            for (int i = 0; i <= 12000; i += 100) {
                GfVec3f color = UsdLuxBlackbodyTemperatureAsRgb((float)i);
                if (i <= 1000) {
                    color *= (float)i / 1000.0f;
                    color *= (float)i / 1000.0f;
                }
                srcTemperatureGridData.valueLUT.push_back(color.data()[0]);
                srcTemperatureGridData.valueLUT.push_back(color.data()[1]);
                srcTemperatureGridData.valueLUT.push_back(color.data()[2]);
            }
            pEmissiveGridData = &srcTemperatureGridData;

            if (hasColor)
                pColorGridData = &srcTemperatureGridData;
        }

        if (!pDensityGridData) {
            srcTemperatureGridData.DuplicateWithUniformValue(defaultDensityGridData, defaultDensity, defaultDensity, defaultDensity);
            pDensityGridData = &defaultDensityGridData;
        }
        if (!pEmissiveGridData) {
            srcDensityGridData.DuplicateWithUniformValue(defaultEmissionGridData, defaultEmission.data()[0], defaultEmission.data()[1], defaultEmission.data()[2]);
            pEmissiveGridData = &defaultEmissionGridData;
        }
        if (!pColorGridData) {
            srcDensityGridData.DuplicateWithUniformValue(defaultColorGridData, defaultColor.data()[0], defaultColor.data()[1], defaultColor.data()[2]);
            pColorGridData = &defaultColorGridData;
        }

        openvdb::Vec3d gridMin = gridTransform.indexToWorld(gridOnBB.min());
        GfVec3f gridBBLow = GfVec3f((float)(gridMin.x() - voxelSize[0] / 2), (float)(gridMin.y() - voxelSize[1] / 2), (float)(gridMin.z() - voxelSize[2] / 2));

        m_rprVolume = rprApi->CreateVolume(
            pDensityGridData->indices, pDensityGridData->values, pDensityGridData->valueLUT,
            pColorGridData->indices, pColorGridData->values, pColorGridData->valueLUT,
            pEmissiveGridData->indices, pEmissiveGridData->values, pEmissiveGridData->valueLUT,
            GfVec3i(gridOnBBSize.x(), gridOnBBSize.y(), gridOnBBSize.z()), GfVec3f((float)voxelSize[0], (float)voxelSize[1], (float)voxelSize[2]), gridBBLow);
    }

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdRprVolume::GetInitialDirtyBitsMask() const {
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyMaterialId;

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
