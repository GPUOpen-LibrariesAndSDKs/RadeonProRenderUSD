#include "volume.h"
#pragma warning(push)
#pragma warning(disable:4146)
#include "openvdb/openvdb.h"
#pragma warning(pop)

#include "openvdb/points/PointDataGrid.h"
#include <openvdb/tools/Interpolation.h>

#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/usdLux/blackbody.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
	HdRprVolumeTokens,
	(filePath)		\
	(density)		\
	(temperature)	\
	(color)			\
	(points)		\
	(temperatureOffset)\
	(temperatureScale)
);

namespace
{
	const float defaultDensity = 100.f;                      //RPR take density value of 100 as fully opaque
	GfVec3f defaultColor = GfVec3f(0.0f, 0.0f, 0.0f);        //Default color of black
	GfVec3f defaultEmission = GfVec3f(0.0f, 0.0f, 0.0f);     //Default to no emission
}

std::string findOpenVdbFilePath(HdSceneDelegate* sceneDelegate, const SdfPath & id)
{
	for (auto it : sceneDelegate->GetVolumeFieldDescriptors(id))
	{
		VtValue param = sceneDelegate->Get(it.fieldId, HdRprVolumeTokens->filePath);

		if (param.IsHolding<SdfAssetPath>())
		{
			return param.Get<SdfAssetPath>().GetAssetPath();
		}
	}

	return "";
}

void findNeededChannels(HdSceneDelegate* sceneDelegate, const SdfPath & id, bool &outNeedColor, bool &outNeedDensity, bool &outNeedEmissive)
{
	outNeedColor = false;
	outNeedDensity = false;
	outNeedEmissive = false;
	for (auto it : sceneDelegate->GetVolumeFieldDescriptors(id))
	{
		VtValue param = sceneDelegate->Get(it.fieldId, HdRprVolumeTokens->filePath);

		if (param.IsHolding<SdfAssetPath>())
		{
			if (it.fieldName.GetString() == "color")
				outNeedColor = true;
			if (it.fieldName.GetString() == "density")
				outNeedDensity = true;
			if (it.fieldName.GetString() == "emissive")
				outNeedEmissive = true;
		}
	}
}

void findTemperatureOffsetAndScale(HdSceneDelegate* sceneDelegate, const SdfPath & id, float &outOffset, float &outScale)
{
	for (auto it : sceneDelegate->GetVolumeFieldDescriptors(id))
	{
		VtValue param = sceneDelegate->Get(it.fieldId, HdRprVolumeTokens->temperatureOffset);
		if (param.IsHolding<float>())
			outOffset = param.Get<float>();

		param = sceneDelegate->Get(it.fieldId, HdRprVolumeTokens->temperatureScale);
		if (param.IsHolding<float>())
			outScale = param.Get<float>();
	}
}

void ReadFloatGrid(openvdb::FloatGrid::Ptr grid, const openvdb::Coord &coordOffset, float valueOffset, float valueScale, std::vector<uint32_t> &outDensityGridOnIndices, std::vector<float> &outDensityGridOnValueIndices)
{
	openvdb::CoordBBox gridOnBB = grid->evalActiveVoxelBoundingBox();

	for (openvdb::FloatGrid::ValueOnIter iter = grid->beginValueOn(); iter; ++iter) {
		openvdb::Coord curCoord = iter.getCoord() + coordOffset;
		outDensityGridOnIndices.push_back(curCoord.x());
		outDensityGridOnIndices.push_back(curCoord.y());
		outDensityGridOnIndices.push_back(curCoord.z());

		float value = (float)(grid->getAccessor().getValue(iter.getCoord()));
		outDensityGridOnValueIndices.push_back((value + valueOffset) * valueScale);
	}
}

HdRprVolume::HdRprVolume(SdfPath const& id, HdRprApiSharedPtr rprApi): HdVolume(id)
{
	m_rprApiWeakPtr = rprApi;
}

struct GridData {
	std::vector<float>    values;
	std::vector<uint32_t> indices;
	std::vector<float>    valueLUT;

	void DuplicateWithUniformValue(GridData &target, float valueChannel0, float valueChannel1, float valueChannel2) const
	{
		target.indices = indices;

		//color grid has one uniform color
		target.valueLUT.clear();
		target.valueLUT.push_back(valueChannel0);
		target.valueLUT.push_back(valueChannel1);
		target.valueLUT.push_back(valueChannel2);

		target.values.resize(values.size(), 0);
	}
};

HdRprVolume::~HdRprVolume()
{
}

void HdRprVolume::Sync(
	HdSceneDelegate* sceneDelegate,
	HdRenderParam*   renderParam,
	HdDirtyBits*     dirtyBits,
	TfToken const&   reprName
)
{
	if (*dirtyBits & HdChangeTracker::DirtyParams)
	{
        m_rprHeteroVolume = nullptr;

		std::string openVdbPath = findOpenVdbFilePath(sceneDelegate, GetId());

		if (openVdbPath.empty())
		{
			*dirtyBits = HdChangeTracker::Clean;
			fprintf(stderr, "[Node: %s]: vdb path empty\n", GetId().GetName().c_str());
			return;
		}

		bool bNeedColor = false;
		bool bNeedDensity = false;
		bool bNeedEmissive = false;
		findNeededChannels(sceneDelegate, GetId(), bNeedColor, bNeedDensity, bNeedEmissive);

		float temperatureOffset = 0.0f;
		float temperatureScale = 1.0f;
		findTemperatureOffsetAndScale(sceneDelegate, GetId(), temperatureOffset, temperatureScale);

		openvdb::initialize();

		openvdb::io::File file(openVdbPath);

		try {
			file.open();
		}
		catch (openvdb::IoError e)
		{
			TF_CODING_ERROR("%s", e.what());
			fprintf(stderr, "[Node: %s]: error opening vdb file %s\n", GetId().GetName().c_str(), openVdbPath.c_str());
			*dirtyBits = HdChangeTracker::Clean;
			return;
		}

		openvdb::GridPtrVecPtr grids = file.getGrids();

		std::set<std::string> gridNames;
		for (auto name = file.beginName(); name != file.endName(); ++name)
		{
			gridNames.insert(*name);
		}
		if (gridNames.empty())
		{
			*dirtyBits = HdChangeTracker::Clean;
			fprintf(stderr, "[Node: %s]: vdb file %s has no grids\n", GetId().GetName().c_str(), openVdbPath.c_str());
			return;
		}

		bool hasDensity = gridNames.find(HdRprVolumeTokens->density) != gridNames.end();
		bool hasTemperature = gridNames.find(HdRprVolumeTokens->temperature) != gridNames.end();

		openvdb::FloatGrid::Ptr densityGrid = (hasDensity) ? openvdb::gridPtrCast<openvdb::FloatGrid>(file.readGrid(HdRprVolumeTokens->density)) : nullptr;
		openvdb::FloatGrid::Ptr temperatureGrid = (hasTemperature) ? openvdb::gridPtrCast<openvdb::FloatGrid>(file.readGrid(HdRprVolumeTokens->temperature)) : nullptr;

		bool bNeedToReadDensityGrid = bNeedDensity && hasDensity;
		if (bNeedToReadDensityGrid && !densityGrid.get())
		{
			fprintf(stderr, "[Node: %s]: vdb file %s density grid doesn't have float type.\n", GetId().GetName().c_str(), openVdbPath.c_str());
			bNeedToReadDensityGrid = false;
		}

		bool bNeedToReadTemperatureGrid = (bNeedColor || bNeedEmissive) && hasTemperature;
		if (bNeedToReadTemperatureGrid && !temperatureGrid.get())
		{
			fprintf(stderr, "[Node: %s]: vdb file %s temperature grid doesn't have float type.\n", GetId().GetName().c_str(), openVdbPath.c_str());
			bNeedToReadTemperatureGrid = false;
		}

		if (!bNeedToReadDensityGrid && !bNeedToReadTemperatureGrid)
		{
			fprintf(stderr, "[Node: %s]: vdb file %s does not have the needed grids.\n", GetId().GetName().c_str(), openVdbPath.c_str());
			return;
		}

		//If we need to read from both grids, check compatibility
		if (bNeedToReadDensityGrid && bNeedToReadTemperatureGrid)
		{
			if (densityGrid->voxelSize() != temperatureGrid->voxelSize())
				fprintf(stderr, "[Node: %s]: vdb file %s has different voxel sizes for density grid and temperature grid. Taking voxel size of density grid\n", GetId().GetName().c_str(), openVdbPath.c_str());
			if (densityGrid->transform() != temperatureGrid->transform())
				fprintf(stderr, "[Node: %s]: vdb file %s has different transform for density grid and temperature grid. Taking transform of density grid\n", GetId().GetName().c_str(), openVdbPath.c_str());
		}

		openvdb::Vec3d voxelSize = bNeedToReadDensityGrid ? densityGrid->voxelSize() : temperatureGrid->voxelSize();
		openvdb::math::Transform gridTransform = bNeedToReadDensityGrid ? densityGrid->transform() : temperatureGrid->transform();
		openvdb::CoordBBox gridOnBB;
		if (bNeedToReadDensityGrid)
			gridOnBB.expand(densityGrid->evalActiveVoxelBoundingBox());
		if (bNeedToReadTemperatureGrid)
			gridOnBB.expand(temperatureGrid->evalActiveVoxelBoundingBox());
		openvdb::Coord gridOnBBSize = gridOnBB.extents();

		GridData srcDensityGridData;
		GridData srcTemperatureGridData;
		GridData defaultEmissionGridData;
		GridData defaultColorGridData;
		GridData defaultDensityGridData;

		GridData *pDensityGridData = nullptr;
		GridData *pColorGridData = nullptr;
		GridData *pEmissiveGridData = nullptr;

		if (bNeedToReadDensityGrid)
		{
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

			if (bNeedDensity)
				pDensityGridData = &srcDensityGridData;
		}
		if (bNeedToReadTemperatureGrid)
		{
			ReadFloatGrid(temperatureGrid, -gridOnBB.min(), temperatureOffset, temperatureScale / 12000.0f, srcTemperatureGridData.indices, srcTemperatureGridData.values);
			for (int i = 0; i <= 12000; i += 100)
			{
				GfVec3f color = UsdLuxBlackbodyTemperatureAsRgb((float)i);
				if (i <= 1000)
				{
					color *= (float)i / 1000.0f;
					color *= (float)i / 1000.0f;
				}
				srcTemperatureGridData.valueLUT.push_back(color.data()[0]);
				srcTemperatureGridData.valueLUT.push_back(color.data()[1]);
				srcTemperatureGridData.valueLUT.push_back(color.data()[2]);
			}

			if (bNeedColor)
				pColorGridData = &srcTemperatureGridData;
			if (bNeedEmissive)
				pEmissiveGridData = &srcTemperatureGridData;
		}

		if (!pDensityGridData)
		{
			srcTemperatureGridData.DuplicateWithUniformValue(defaultDensityGridData, defaultDensity, defaultDensity, defaultDensity);
			pDensityGridData = &defaultDensityGridData;
		}
		if (!pEmissiveGridData)
		{
			srcDensityGridData.DuplicateWithUniformValue(defaultEmissionGridData, defaultEmission.data()[0], defaultEmission.data()[1], defaultEmission.data()[2]);
			pEmissiveGridData = &defaultEmissionGridData;
		}
		if (!pColorGridData)
		{
			srcDensityGridData.DuplicateWithUniformValue(defaultColorGridData, defaultColor.data()[0], defaultColor.data()[1], defaultColor.data()[2]);
			pColorGridData = &defaultColorGridData;
		}


		file.close();

		HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
		if (!rprApi)
		{
			TF_CODING_ERROR("RprApi is expired");
			*dirtyBits = HdChangeTracker::Clean;
			return;
		}

		openvdb::Vec3d gridMin = gridTransform.indexToWorld(gridOnBB.min());
		GfVec3f gridBBLow = GfVec3f((float)(gridMin.x() - voxelSize[0] / 2), (float)(gridMin.y() - voxelSize[1] / 2), (float)(gridMin.z() - voxelSize[2] / 2));

		m_rprHeteroVolume = rprApi->CreateVolume(pDensityGridData->indices, pDensityGridData->values, pDensityGridData->valueLUT,
			pColorGridData->indices, pColorGridData->values, pColorGridData->valueLUT, pEmissiveGridData->indices, pEmissiveGridData->values, pEmissiveGridData->valueLUT,
			GfVec3i(gridOnBBSize.x(), gridOnBBSize.y(), gridOnBBSize.z()), GfVec3f((float)voxelSize[0], (float)voxelSize[1], (float)voxelSize[2]), gridBBLow);
	}

	*dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits
HdRprVolume::GetInitialDirtyBitsMask() const
{
	int mask = HdChangeTracker::Clean
		| HdChangeTracker::DirtyPrimvar
		| HdChangeTracker::AllDirty
		;

	return (HdDirtyBits)mask;
}

HdDirtyBits
HdRprVolume::_PropagateDirtyBits(HdDirtyBits bits) const
{
	return bits;
}

void
HdRprVolume::_InitRepr(TfToken const &reprName,
	HdDirtyBits *dirtyBits)
{
	TF_UNUSED(reprName);
	TF_UNUSED(dirtyBits);

	// No-op
}


PXR_NAMESPACE_CLOSE_SCOPE
