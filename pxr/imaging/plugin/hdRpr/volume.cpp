#include "volume.h"

#include "openvdb/openvdb.h"
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
	(points)
);

namespace
{
	const float defaultDencity = 1.f;
	const float defaultAlbedo = 1.f;
}

/*
nmaespace
{
	std::map<int, GfVec3f> tempColors
	{
		{0,GfVec3f(0.000000f, 0.000000f, 0.000000f)},
		{1000,GfVec3f(1.000000f, 0.027490f, 0.000000f)}, //  1000 K
		{1500,GfVec3f(1.000000f, 0.149664f, 0.000000f)}, //  1500 K
		{2000,GfVec3f(1.000000f, 0.256644f, 0.008095f)}, //  2000 K
		{2500,GfVec3f(1.000000f, 0.372033f, 0.067450f)}, //  2500 K
		{3000,GfVec3f(1.000000f, 0.476725f, 0.153601f)}, //  3000 K
		{3500,GfVec3f(1.000000f, 0.570376f, 0.259196f)}, //  3500 K
		{4000,GfVec3f(1.000000f, 0.653480f, 0.377155f)}, //  4000 K
		{4500,GfVec3f(1.000000f, 0.726878f, 0.501606f)}, //  4500 K
		{5000,GfVec3f(1.000000f, 0.791543f, 0.628050f)}, //  5000 K
		{5500,GfVec3f(1.000000f, 0.848462f, 0.753228f)} , //  5500 K
		{6000,GfVec3f(1.000000f, 0.898581f, 0.874905f)}, //  6000 K
		{6500,GfVec3f(1.000000f, 0.942771f, 0.991642f)}, //  6500 K
		{7000,GfVec3f(0.906947f, 0.890456f, 1.000000f)}, //  7000 K
		{7500,GfVec3f(0.828247f, 0.841838f, 1.000000f)}, //  7500 K
		{8000,GfVec3f(0.765791f, 0.801896f, 1.000000f)}, //  8000 K
		{8500,GfVec3f(0.715255f, 0.768579f, 1.000000f)}, //  8500 K
		{9000,GfVec3f(0.673683f, 0.740423f, 1.000000f)}, //  9000 K
		{9500,GfVec3f(0.638992f, 0.716359f, 1.000000f)}, //  9500 K
		{10000,GfVec3f(0.609681f, 0.695588f, 1.000000f)}, // 10000 K
	};
}


GfVec3f colorLerp(const GfVec3f & color0, const GfVec3f & color1, const float & w)
{
	return color0 + (color1 - color0) * w;
}


GfVec3f temteratureToColor(const float & temperature)
{
	if (temperature <= tempColors.begin()->first)
	{
		return tempColors.begin()->second;
	}
	if (temperature >= tempColors.rbegin()->first)
	{
		tempColors.rbegin()->second;
	}
	
	auto tIt = tempColors.begin();
	float tLess = tIt->first;
	GfVec3f colorLess = tIt->second;

	for (++tIt; tIt != tempColors.end(); tIt++)
	{
		if ((int)temperature < tIt->first)
		{
			float tMore = tIt->first;
			GfVec3f colorMore = tIt->second;

			float waight = (temperature - tLess) / (tMore - tLess);

			return colorLerp(colorLess, colorMore, waight);
		}

		tLess = tIt->first;
		colorLess = tIt->second;
	}

	return GfVec3f();
}*/

size_t coordToIndex(const openvdb::Coord & coord, const openvdb::CoordBBox & activeBB, openvdb::Coord gridSize)
{
	const openvdb::Coord & v0 = activeBB.min();
	const openvdb::Coord & v1 = activeBB.max();

	float dx = ((float)(coord.x() - v0.x())) / (v1.x() - v0.x());
	float dy = ((float)(coord.y() - v0.y())) / (v1.y() - v0.y());
	float dz = ((float)(coord.z() - v0.z())) / (v1.z() - v0.z());


	size_t xn = static_cast<size_t>(roundf(dx * gridSize.x()));
	size_t yn = static_cast<size_t>(roundf(dy * gridSize.y()));
	size_t zn = static_cast<size_t>(roundf(dz * gridSize.z()));

	return (zn)*(gridSize.x()* gridSize.y()) + (yn)*(gridSize.x())+xn;
};


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
}

template <class TPtr, class TValueOnIt>
openvdb::CoordBBox computeGrigOnBB(TPtr grid)
{
	if (grid->empty())
	{
		return openvdb::CoordBBox();
	}

	openvdb::Coord min = grid->beginValueOn().getCoord();
	openvdb::Coord max = grid->beginValueOn().getCoord();

	for (TValueOnIt iter = grid->beginValueOn(); iter; ++iter) {

		openvdb::Coord coord = iter.getCoord();

		min.x() = std::min(min.x(), coord.x());
		min.y() = std::min(min.y(), coord.y());
		min.z() = std::min(min.z(), coord.z());

		max.x() = std::max(max.x(), coord.x());
		max.y() = std::max(max.y(), coord.y());
		max.z() = std::max(max.z(), coord.z());
	}

	return openvdb::CoordBBox(min, max);
}


template <class TPtr, class TValueOnIt>
void fillGridOnWithDefaultColor(TPtr grid, VtVec4fArray & out_grid, VtArray<size_t> & out_idx, openvdb::Coord & out_gridOnBBSize)
{
	openvdb::CoordBBox gridOnBB = computeGrigOnBB< TPtr, TValueOnIt>(grid);
	out_gridOnBBSize = gridOnBB.max() - gridOnBB.min();

	for (TValueOnIt iter = grid->beginValueOn(); iter; ++iter) {
		out_grid.push_back(defaultColor);
		out_idx.push_back(coordToIndex(iter.getCoord(), gridOnBB, out_gridOnBBSize));
	}
}


HdRprVolume::HdRprVolume(SdfPath const& id, HdRprApiSharedPtr rprApi): HdVolume(id)
{
	m_rprApiWeakPrt = rprApi;
}

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

		std::string openVdbPath = findOpenVdbFilePath(sceneDelegate, GetId());

		if (openVdbPath.empty())
		{
			*dirtyBits = HdChangeTracker::Clean;
			return;
		}

		openvdb::initialize();

		openvdb::io::File file(openVdbPath);
		
		try {
			file.open();
		}
		catch (openvdb::IoError e)
		{
			TF_CODING_ERROR("%s", e.what());
			*dirtyBits = HdChangeTracker::Clean;
			return;
		}
		

		openvdb::GridPtrVecPtr grids = file.getGrids();

		VtArray<float> gridDencity;
		VtArray<size_t> idxDencity;

		VtArray<float> gridAlbedo;
		VtArray<unsigned int> idxAlbedo;

		openvdb::Coord gridOnBBSize;
		openvdb::Vec3d voxelSize;

		std::set<std::string> gridNames;
		for (auto name = file.beginName(); name != file.endName(); ++name)
		{
			gridNames.insert(*name);
		}
		if (gridNames.empty())
		{
			*dirtyBits = HdChangeTracker::Clean;
			return;
		}

		bool isDencity = gridNames.find(HdRprVolumeTokens->density) != gridNames.end();
		bool isTemperature = gridNames.find(HdRprVolumeTokens->temperature) != gridNames.end();
		bool isColor = gridNames.find(HdRprVolumeTokens->color) != gridNames.end();


		if (isDencity || isTemperature || isColor)
		{
			openvdb::FloatGrid::Ptr dencityGrid = (isDencity) ? openvdb::gridPtrCast<openvdb::FloatGrid>(file.readGrid(HdRprVolumeTokens->density)) : nullptr;
			openvdb::FloatGrid::Ptr temperatureGrid = (isTemperature) ? openvdb::gridPtrCast<openvdb::FloatGrid>(file.readGrid(HdRprVolumeTokens->temperature)) : nullptr;


			openvdb::FloatGrid::Ptr fpGrig;

			if (isDencity)
			{
				fpGrig = dencityGrid;
			}
			else if (isTemperature)
			{
				fpGrig = temperatureGrid;
			}
			else
			{
				*dirtyBits = HdChangeTracker::Clean;
				return;
			}

			voxelSize = fpGrig->voxelSize();

			openvdb::CoordBBox gridOnBB = computeGrigOnBB<openvdb::FloatGrid::Ptr,openvdb::FloatGrid::ValueOnIter>(fpGrig);
			gridOnBBSize = gridOnBB.max() - gridOnBB.min();

			//

			for (openvdb::FloatGrid::ValueOnIter iter = fpGrig->beginValueOn(); iter; ++iter) {

				openvdb::Coord coord = iter.getCoord();
				float dencity = defaultDencity;


				if (dencityGrid)
				{
					openvdb::FloatGrid::ConstAccessor dencityAccesor = dencityGrid->getConstAccessor();
					dencity = dencityAccesor.getValue(coord);
				}



				if (isTemperature)
				{
					// Not implemented
				}
				else if (isColor)
				{
					GfVec4f voxelColor;

					openvdb::GridBase::Ptr colorGrid = file.readGrid(HdRprVolumeTokens->color);
					if(colorGrid->isType<openvdb::Vec3IGrid>())
					{
						openvdb::Vec3i color = openvdb::gridPtrCast<openvdb::Vec3IGrid>(colorGrid)->getAccessor().getValue(coord);
						voxelColor[0] = static_cast<float>(color[0]);
						voxelColor[1] = static_cast<float>(color[1]);
						voxelColor[2] = static_cast<float>(color[2]);
					}
					else if (colorGrid->isType<openvdb::Vec3SGrid>())
					{
						openvdb::Vec3f color = openvdb::gridPtrCast<openvdb::Vec3SGrid>(colorGrid)->getAccessor().getValue(coord);
						voxelColor[0] = color[0];
						voxelColor[1] = color[1];
						voxelColor[2] = color[2];
					}
					else if (colorGrid->isType<openvdb::Vec3DGrid>())
					{
						openvdb::Vec3d color = openvdb::gridPtrCast<openvdb::Vec3DGrid>(colorGrid)->getAccessor().getValue(coord);
						voxelColor[0] = static_cast<float>(color[0]);
						voxelColor[1] = static_cast<float>(color[1]);
						voxelColor[2] = static_cast<float>(color[2]);
					}

					gridAlbedo.push_back(voxelColor[0]);
					gridAlbedo.push_back(voxelColor[1]);
					gridAlbedo.push_back(voxelColor[2]);

					const unsigned int nextIdx = (idxAlbedo.empty()) ? 0 : idxAlbedo.back() + 1;

					idxAlbedo.push_back(nextIdx + 0);
					idxAlbedo.push_back(nextIdx + 1);
					idxAlbedo.push_back(nextIdx + 2);
				}
				else
				{
					gridAlbedo.push_back(defaultAlbedo);
					
					const unsigned int nextIdx = (idxAlbedo.empty()) ? 0 : idxAlbedo.back() + 1;

					idxAlbedo.push_back(nextIdx);
					idxAlbedo.push_back(nextIdx);
					idxAlbedo.push_back(nextIdx);
				}

				gridDencity.push_back((dencityGrid) ? dencityGrid->getConstAccessor().getValue(coord) : 0.f);
				idxDencity.push_back(coordToIndex(iter.getCoord(), gridOnBB, gridOnBBSize));
			}
		}
		else if (gridNames.find(HdRprVolumeTokens->points) != gridNames.end())
		{
			// Not implemented
		}
		else
		{
			openvdb::GridBase::Ptr baseGrid = file.readGrid( * file.beginName());
			if (baseGrid->empty())
			{
				*dirtyBits = HdChangeTracker::Clean;
				return;
			}


			/*
			voxelSize = baseGrid->voxelSize();

			if (baseGrid->isType<openvdb::BoolGrid>())			fillGridOnWithDefaultColor< openvdb::BoolGrid::Ptr, openvdb::BoolGrid::ValueOnIter >(openvdb::gridPtrCast<openvdb::BoolGrid>(baseGrid), grid, idx, gridOnBBSize);
			else if (baseGrid->isType<openvdb::FloatGrid>())	fillGridOnWithDefaultColor< openvdb::FloatGrid::Ptr, openvdb::FloatGrid::ValueOnIter >(openvdb::gridPtrCast<openvdb::FloatGrid>(baseGrid), grid, idx, gridOnBBSize);
			else if (baseGrid->isType<openvdb::DoubleGrid>())	fillGridOnWithDefaultColor< openvdb::DoubleGrid::Ptr, openvdb::DoubleGrid::ValueOnIter >(openvdb::gridPtrCast<openvdb::DoubleGrid>(baseGrid), grid, idx, gridOnBBSize);
			else if (baseGrid->isType<openvdb::Int32Grid>())	fillGridOnWithDefaultColor< openvdb::Int32Grid::Ptr, openvdb::Int32Grid::ValueOnIter >(openvdb::gridPtrCast<openvdb::Int32Grid>(baseGrid), grid, idx, gridOnBBSize);
			else if (baseGrid->isType<openvdb::Int64Grid>())	fillGridOnWithDefaultColor< openvdb::Int64Grid::Ptr, openvdb::Int64Grid::ValueOnIter >(openvdb::gridPtrCast<openvdb::Int64Grid>(baseGrid), grid, idx, gridOnBBSize);
			else if (baseGrid->isType<openvdb::Vec3IGrid>())	fillGridOnWithDefaultColor< openvdb::Vec3IGrid::Ptr, openvdb::Vec3IGrid::ValueOnIter >(openvdb::gridPtrCast<openvdb::Vec3IGrid>(baseGrid), grid, idx, gridOnBBSize);
			else if (baseGrid->isType<openvdb::Vec3SGrid>())	fillGridOnWithDefaultColor< openvdb::Vec3SGrid::Ptr, openvdb::Vec3SGrid::ValueOnIter >(openvdb::gridPtrCast<openvdb::Vec3SGrid>(baseGrid), grid, idx, gridOnBBSize);
			else if (baseGrid->isType<openvdb::Vec3DGrid>())	fillGridOnWithDefaultColor< openvdb::Vec3DGrid::Ptr, openvdb::Vec3DGrid::ValueOnIter >(openvdb::gridPtrCast<openvdb::Vec3DGrid>(baseGrid), grid, idx, gridOnBBSize);
			else if (baseGrid->isType<openvdb::StringGrid>())	fillGridOnWithDefaultColor< openvdb::StringGrid::Ptr, openvdb::StringGrid::ValueOnIter >(openvdb::gridPtrCast<openvdb::StringGrid>(baseGrid), grid, idx, gridOnBBSize);
			*/
		}

		file.close();

		HdRprApiSharedPtr rprApi = m_rprApiWeakPrt.lock();
		if (!rprApi)
		{
			TF_CODING_ERROR("RprApi is expired");
			*dirtyBits = HdChangeTracker::Clean;
			return;
		}

		rprApi->CreateVolume(gridDencity, idxDencity, gridAlbedo, idxAlbedo, GfVec3i(gridOnBBSize.x(), gridOnBBSize.y(), gridOnBBSize.z()), GfVec3f((float)voxelSize[0], voxelSize[1], voxelSize[2]), nullptr, nullptr);

	}

	* dirtyBits = HdChangeTracker::Clean;
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
