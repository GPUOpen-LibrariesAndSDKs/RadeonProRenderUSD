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

namespace
{
	float kelvin_table[] = {
		  0.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, // 0K
		  0.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, // 100K
		  0.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, // 200K
		  0.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, // 300K
		 55.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, // 400K
		100.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, // 500K
		155.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, // 600K
		205.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, // 700K
		255.0f / 255.0f,  16.0f / 255.0f,   0.0f / 255.0f, // 800K
		255.0f / 255.0f,  38.0f / 255.0f,   0.0f / 255.0f, // 900K
		255.0f / 255.0f,  56.0f / 255.0f,   0.0f / 255.0f, // 1000K
		255.0f / 255.0f,  71.0f / 255.0f,   0.0f / 255.0f, // 1100K
		255.0f / 255.0f,  83.0f / 255.0f,   0.0f / 255.0f, // 1200K
		255.0f / 255.0f,  93.0f / 255.0f,   0.0f / 255.0f, // 1300K
		255.0f / 255.0f, 101.0f / 255.0f,   0.0f / 255.0f, // 1400K
		255.0f / 255.0f, 109.0f / 255.0f,   0.0f / 255.0f, // 1500K
		255.0f / 255.0f, 115.0f / 255.0f,   0.0f / 255.0f, // 1600K
		255.0f / 255.0f, 121.0f / 255.0f,   0.0f / 255.0f, // 1700K
		255.0f / 255.0f, 126.0f / 255.0f,   0.0f / 255.0f, // 1800K
		255.0f / 255.0f, 131.0f / 255.0f,   0.0f / 255.0f, // 1900K
		255.0f / 255.0f, 138.0f / 255.0f,  18.0f / 255.0f, // 2000K
		255.0f / 255.0f, 142.0f / 255.0f,  33.0f / 255.0f, // 2100K
		255.0f / 255.0f, 147.0f / 255.0f,  44.0f / 255.0f, // 2200K
		255.0f / 255.0f, 152.0f / 255.0f,  54.0f / 255.0f, // 2300K
		255.0f / 255.0f, 157.0f / 255.0f,  63.0f / 255.0f, // 2400K
		255.0f / 255.0f, 161.0f / 255.0f,  72.0f / 255.0f, // 2500K
		255.0f / 255.0f, 165.0f / 255.0f,  79.0f / 255.0f, // 2600K
		255.0f / 255.0f, 169.0f / 255.0f,  87.0f / 255.0f, // 2700K
		255.0f / 255.0f, 173.0f / 255.0f,  94.0f / 255.0f, // 2800K
		255.0f / 255.0f, 177.0f / 255.0f, 101.0f / 255.0f, // 2900K
		255.0f / 255.0f, 180.0f / 255.0f, 107.0f / 255.0f, // 3000K
		255.0f / 255.0f, 184.0f / 255.0f, 114.0f / 255.0f, // 3100K
		255.0f / 255.0f, 187.0f / 255.0f, 120.0f / 255.0f, // 3200K
		255.0f / 255.0f, 190.0f / 255.0f, 126.0f / 255.0f, // 3300K
		255.0f / 255.0f, 193.0f / 255.0f, 132.0f / 255.0f, // 3400K
		255.0f / 255.0f, 196.0f / 255.0f, 137.0f / 255.0f, // 3500K
		255.0f / 255.0f, 199.0f / 255.0f, 143.0f / 255.0f, // 3600K
		255.0f / 255.0f, 201.0f / 255.0f, 148.0f / 255.0f, // 3700K
		255.0f / 255.0f, 204.0f / 255.0f, 153.0f / 255.0f, // 3800K
		255.0f / 255.0f, 206.0f / 255.0f, 159.0f / 255.0f, // 3900K
		255.0f / 255.0f, 209.0f / 255.0f, 163.0f / 255.0f, // 4000K
		255.0f / 255.0f, 211.0f / 255.0f, 168.0f / 255.0f, // 4100K
		255.0f / 255.0f, 213.0f / 255.0f, 173.0f / 255.0f, // 4200K
		255.0f / 255.0f, 215.0f / 255.0f, 177.0f / 255.0f, // 4300K
		255.0f / 255.0f, 217.0f / 255.0f, 182.0f / 255.0f, // 4400K
		255.0f / 255.0f, 219.0f / 255.0f, 186.0f / 255.0f, // 4500K
		255.0f / 255.0f, 221.0f / 255.0f, 190.0f / 255.0f, // 4600K
		255.0f / 255.0f, 223.0f / 255.0f, 194.0f / 255.0f, // 4700K
		255.0f / 255.0f, 225.0f / 255.0f, 198.0f / 255.0f, // 4800K
		255.0f / 255.0f, 227.0f / 255.0f, 202.0f / 255.0f, // 4900K
		255.0f / 255.0f, 228.0f / 255.0f, 206.0f / 255.0f, // 5000K
		255.0f / 255.0f, 230.0f / 255.0f, 210.0f / 255.0f, // 5100K
		255.0f / 255.0f, 232.0f / 255.0f, 213.0f / 255.0f, // 5200K
		255.0f / 255.0f, 233.0f / 255.0f, 217.0f / 255.0f, // 5300K
		255.0f / 255.0f, 235.0f / 255.0f, 220.0f / 255.0f, // 5400K
		255.0f / 255.0f, 236.0f / 255.0f, 224.0f / 255.0f, // 5500K
		255.0f / 255.0f, 238.0f / 255.0f, 227.0f / 255.0f, // 5600K
		255.0f / 255.0f, 239.0f / 255.0f, 230.0f / 255.0f, // 5700K
		255.0f / 255.0f, 240.0f / 255.0f, 233.0f / 255.0f, // 5800K
		255.0f / 255.0f, 242.0f / 255.0f, 236.0f / 255.0f, // 5900K
		255.0f / 255.0f, 243.0f / 255.0f, 239.0f / 255.0f, // 6000K
		255.0f / 255.0f, 244.0f / 255.0f, 242.0f / 255.0f, // 6100K
		255.0f / 255.0f, 245.0f / 255.0f, 245.0f / 255.0f, // 6200K
		255.0f / 255.0f, 246.0f / 255.0f, 247.0f / 255.0f, // 6300K
		255.0f / 255.0f, 248.0f / 255.0f, 251.0f / 255.0f, // 6400K
		255.0f / 255.0f, 249.0f / 255.0f, 253.0f / 255.0f, // 6500K
		254.0f / 255.0f, 249.0f / 255.0f, 255.0f / 255.0f, // 6600K
		252.0f / 255.0f, 247.0f / 255.0f, 255.0f / 255.0f, // 6700K
		249.0f / 255.0f, 246.0f / 255.0f, 255.0f / 255.0f, // 6800K
		247.0f / 255.0f, 245.0f / 255.0f, 255.0f / 255.0f, // 6900K
		245.0f / 255.0f, 243.0f / 255.0f, 255.0f / 255.0f, // 7000K
		243.0f / 255.0f, 242.0f / 255.0f, 255.0f / 255.0f, // 7100K
		240.0f / 255.0f, 241.0f / 255.0f, 255.0f / 255.0f, // 7200K
		239.0f / 255.0f, 240.0f / 255.0f, 255.0f / 255.0f, // 7300K
		237.0f / 255.0f, 239.0f / 255.0f, 255.0f / 255.0f, // 7400K
		235.0f / 255.0f, 238.0f / 255.0f, 255.0f / 255.0f, // 7500K
		233.0f / 255.0f, 237.0f / 255.0f, 255.0f / 255.0f, // 7600K
		231.0f / 255.0f, 236.0f / 255.0f, 255.0f / 255.0f, // 7700K
		230.0f / 255.0f, 235.0f / 255.0f, 255.0f / 255.0f, // 7800K
		228.0f / 255.0f, 234.0f / 255.0f, 255.0f / 255.0f, // 7900K
		227.0f / 255.0f, 233.0f / 255.0f, 255.0f / 255.0f, // 8000K
		225.0f / 255.0f, 232.0f / 255.0f, 255.0f / 255.0f, // 8100K
		224.0f / 255.0f, 231.0f / 255.0f, 255.0f / 255.0f, // 8200K
		222.0f / 255.0f, 230.0f / 255.0f, 255.0f / 255.0f, // 8300K
		221.0f / 255.0f, 230.0f / 255.0f, 255.0f / 255.0f, // 8400K
		220.0f / 255.0f, 229.0f / 255.0f, 255.0f / 255.0f, // 8500K
		218.0f / 255.0f, 229.0f / 255.0f, 255.0f / 255.0f, // 8600K
		217.0f / 255.0f, 227.0f / 255.0f, 255.0f / 255.0f, // 8700K
		216.0f / 255.0f, 227.0f / 255.0f, 255.0f / 255.0f, // 8800K
		215.0f / 255.0f, 226.0f / 255.0f, 255.0f / 255.0f, // 8900K
		214.0f / 255.0f, 225.0f / 255.0f, 255.0f / 255.0f, // 9000K
		212.0f / 255.0f, 225.0f / 255.0f, 255.0f / 255.0f, // 9100K
		211.0f / 255.0f, 224.0f / 255.0f, 255.0f / 255.0f, // 9200K
		210.0f / 255.0f, 223.0f / 255.0f, 255.0f / 255.0f, // 9300K
		209.0f / 255.0f, 223.0f / 255.0f, 255.0f / 255.0f, // 9400K
		208.0f / 255.0f, 222.0f / 255.0f, 255.0f / 255.0f, // 9500K
		207.0f / 255.0f, 221.0f / 255.0f, 255.0f / 255.0f, // 9600K
		207.0f / 255.0f, 221.0f / 255.0f, 255.0f / 255.0f, // 9700K
		206.0f / 255.0f, 220.0f / 255.0f, 255.0f / 255.0f, // 9800K
		205.0f / 255.0f, 220.0f / 255.0f, 255.0f / 255.0f, // 9900K
		207.0f / 255.0f, 218.0f / 255.0f, 255.0f / 255.0f, //10000K
		207.0f / 255.0f, 218.0f / 255.0f, 255.0f / 255.0f, //10100K
		206.0f / 255.0f, 217.0f / 255.0f, 255.0f / 255.0f, //10200K
		205.0f / 255.0f, 217.0f / 255.0f, 255.0f / 255.0f, //10300K
		204.0f / 255.0f, 216.0f / 255.0f, 255.0f / 255.0f, //10400K
		204.0f / 255.0f, 216.0f / 255.0f, 255.0f / 255.0f, //10500K
		203.0f / 255.0f, 215.0f / 255.0f, 255.0f / 255.0f, //10600K
		202.0f / 255.0f, 215.0f / 255.0f, 255.0f / 255.0f, //10700K
		202.0f / 255.0f, 214.0f / 255.0f, 255.0f / 255.0f, //10800K
		201.0f / 255.0f, 214.0f / 255.0f, 255.0f / 255.0f, //10900K
		200.0f / 255.0f, 213.0f / 255.0f, 255.0f / 255.0f, //11000K
		200.0f / 255.0f, 213.0f / 255.0f, 255.0f / 255.0f, //11100K
		199.0f / 255.0f, 212.0f / 255.0f, 255.0f / 255.0f, //11200K
		198.0f / 255.0f, 212.0f / 255.0f, 255.0f / 255.0f, //11300K
		198.0f / 255.0f, 212.0f / 255.0f, 255.0f / 255.0f, //11400K
		197.0f / 255.0f, 211.0f / 255.0f, 255.0f / 255.0f, //11500K
		197.0f / 255.0f, 211.0f / 255.0f, 255.0f / 255.0f, //11600K
		197.0f / 255.0f, 210.0f / 255.0f, 255.0f / 255.0f, //11700K
		196.0f / 255.0f, 210.0f / 255.0f, 255.0f / 255.0f, //11800K
		195.0f / 255.0f, 210.0f / 255.0f, 255.0f / 255.0f, //11900K
		195.0f / 255.0f, 209.0f / 255.0f, 255.0f / 255.0f  //12000K
	};
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
			for (int i = 0; i < sizeof(kelvin_table) / sizeof(kelvin_table[0]); i++)
				srcTemperatureGridData.valueLUT.push_back(kelvin_table[i]);

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
