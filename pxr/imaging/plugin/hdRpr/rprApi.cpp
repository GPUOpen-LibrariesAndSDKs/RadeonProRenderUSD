#include "rprApi.h"

#include "RprSupport.h"
#include "RadeonProRender.h"
#include "RadeonProRender_CL.h"
#include "RadeonProRender_GL.h"

#include "../RprTools.h"
#include "../RprTools.cpp"

#include "material.h"
#include "materialFactory.h"
#include "materialAdapter.h"

#include <vector>

#include "pxr/imaging/pxOsd/tokens.h"

#ifdef USE_RIF
#include "ImageFilter.h"
#endif // USE_RIF

#ifdef WIN32
#include <shlobj_core.h>
#pragma comment(lib,"Shell32.lib")
#elif __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <dlfcn.h>
#else

#endif // WIN32

#define PRINTER(name) printer(#name, (name))

// we lock()/unlock() around rpr calls that might be called multithreaded.
#define SAFE_DELETE_RPR_OBJECT(x) if(x) {lock(); rprObjectDelete( x ); x = nullptr; unlock();}
#define INVALID_TEXTURE -1
#define INVALID_FRAMEBUFFER -1

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
#ifdef WIN32
	const char* k_TahoeLibName = "Tahoe64.dll";
#elif defined __linux__
	const char* k_TahoeLibName = "libTahoe64.so";
#elif defined __APPLE__
	const char* k_TahoeLibName = "libTahoe64.dylib";
    const char* k_RadeonProRenderLibName = "libRadeonProRender64.dylib";
#endif

	constexpr const rpr_uint k_defaultFbWidth = 800;
	constexpr const rpr_uint k_defaultFbHeight = 600;

	const GfVec3f k_defaultLightColor(0.5f, 0.5f, 0.5f);

	const uint32_t k_diskVertexCount = 32;

	constexpr const char * k_pathToRprPreference = "hdRprPreferences.dat";
}


inline bool rprIsErrorCheck(const TfCallContext &context, const rpr_status status, const std::string & messageOnFail)
{

	if (RPR_SUCCESS == status)
	{
		return false;
	}

	const char * rprErrorString = [](const rpr_status s)-> const char * {
		switch (s)
		{
		case RPR_ERROR_INVALID_API_VERSION: return "invalide api version";
		case RPR_ERROR_INVALID_PARAMETER: return "invalide parameter";
		case RPR_ERROR_UNSUPPORTED: return "unsupported";
		default:
			break;
		}

		return "unknown error";
	}(status);

	const size_t maxBufferSize = 1024;
	char buffer[maxBufferSize];

	snprintf(buffer, maxBufferSize, "%s %s: %s", "[RPR ERROR] ", messageOnFail.c_str(), rprErrorString);
	Tf_PostErrorHelper(context, TF_DIAGNOSTIC_CODING_ERROR_TYPE, buffer);

	return true;
}

#define RPR_ERROR_CHECK(STATUS, MESSAGE_ON_FAIL) rprIsErrorCheck(TF_CALL_CONTEXT, STATUS, MESSAGE_ON_FAIL)

const char* HdRprApi::GetTmpDir() {
#ifdef WIN32
	char appDataPath[MAX_PATH];
	// Get path for each computer, non-user specific and non-roaming data.
	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, 0, appDataPath)))
	{
		static char path[MAX_PATH];
		snprintf(path, sizeof(path), "%s\\hdRPR\\", appDataPath);
		return path;
	}
#elif defined __linux__
	if (auto homeEnv = getenv("HOME")) {
		static char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/.config/hdRPR/", homeEnv);
		return path;
	}
#elif defined __APPLE__
	return "~/Library/Application Support/hdRPR/";
#else
#warning "Unknown platform"
#endif
	return "";
}

std::string GetRprSdkPath()
{
#ifdef __APPLE__
    uint32_t count = _dyld_image_count();
            std::string pathToRpr;
            for (uint32_t i = 0; i < count; ++i) {
                    const mach_header *header = _dyld_get_image_header(i);
                    if (!header) { break; }
                    char *code_ptr = NULL;
                    uint64_t size;
                    code_ptr = getsectdatafromheader_64((const mach_header_64 *)header, SEG_TEXT, SECT_TEXT, &size);
                    if (!code_ptr) { continue; }
                    const uintptr_t slide = _dyld_get_image_vmaddr_slide(i);
                    const uintptr_t start = (const uintptr_t)code_ptr + slide;
                    Dl_info info;
                    if (dladdr((const void *)start, &info)) {
                            std::string dlpath(info.dli_fname);
                            std::size_t found = dlpath.find(k_RadeonProRenderLibName);
                            if(found != std::string::npos)
                            {
                                return dlpath.substr(0, found);
                            }
                    }
            }

    TF_CODING_ERROR("Path to RPR SDK with %s not found", k_RadeonProRenderLibName);
#endif // __APPLE__

    return std::string();
}

rpr_creation_flags getAllCompatibleGpuFlags()
{
#ifdef __APPLE__
    return RPR_CREATION_FLAGS_ENABLE_METAL;
#else

    rpr_creation_flags flags = 0x0;
	const rpr_creation_flags allGpuFlags = RPR_CREATION_FLAGS_ENABLE_GPU0
		| RPR_CREATION_FLAGS_ENABLE_GPU1
		| RPR_CREATION_FLAGS_ENABLE_GPU2
		| RPR_CREATION_FLAGS_ENABLE_GPU3
		| RPR_CREATION_FLAGS_ENABLE_GPU4
		| RPR_CREATION_FLAGS_ENABLE_GPU5
		| RPR_CREATION_FLAGS_ENABLE_GPU6
		| RPR_CREATION_FLAGS_ENABLE_GPU7;

#ifdef WIN32
	RPR_TOOLS_OS rprToolOs = RPR_TOOLS_OS::RPRTOS_WINDOWS;
#else
	RPR_TOOLS_OS rprToolOs = RPR_TOOLS_OS::RPRTOS_LINUX;
#endif // WIN32


		rprAreDevicesCompatible(k_TahoeLibName, nullptr, false, allGpuFlags, &flags, rprToolOs);
		return flags;
#endif //__APPLE__
}


const rpr_creation_flags getRprCreationFlags(const HdRprRenderDevice renderDevice)
{
	rpr_creation_flags flags = 0x0;


	if (HdRprRenderDevice::CPU == renderDevice)
	{
#ifdef  USE_GL_INTEROP

		TF_CODING_WARNING("Do not support GL Interop with CPU device. Switched to GPU.");
		flags = getAllCompatibleGpuFlags();
#else
		flags = RPR_CREATION_FLAGS_ENABLE_CPU;
#endif
	}
	else if (HdRprRenderDevice::GPU == renderDevice)
	{
		flags = getAllCompatibleGpuFlags();
	}
	else
	{
		//TODO: log unknown HdRprRenderDevice
		return NULL;
	}


	if (flags == 0x0)
	{
		// TODO: log no compatible device
		return NULL;
	}

#ifdef  USE_GL_INTEROP
	flags |= RPR_CREATION_FLAGS_ENABLE_GL_INTEROP;
#endif

	return flags;
}

class HdRprPreferences
{
public:

	static HdRprPreferences & GetInstance() {
		static HdRprPreferences instance;
		return instance;
	}

	void SetAov(const HdRprAov & aov)
	{
		m_prefData.mAov = aov;
		Save();
		SetDitry(true);
	}

	const HdRprAov & GetAov() const
	{
		return m_prefData.mAov;
	}

	void SetRenderDevice(const HdRprRenderDevice & renderDevice)
	{
		m_prefData.mRenderDevice = renderDevice;
		Save();
		SetDitry(true);
	}

	HdRprRenderDevice GetRenderDevice() const
	{
		return m_prefData.mRenderDevice;
	}


	void SetFilterType(const FilterType & type)
	{
		m_prefData.mFilterType = type;
		SetFilterDitry(true);
	}

	FilterType GetFilterType() const
	{
		return m_prefData.mFilterType;
	}

	bool IsDirty() const
	{
		return m_isDirty;
	}


	bool IsFilterTypeDirty()
	{
		return m_isFilterDirty;
	}

	void SetDitry(bool isDirty)
	{
		m_isDirty = isDirty;
	}

	void SetFilterDitry(bool isDirty)
	{
		m_isFilterDirty = isDirty;
	}

private:
	HdRprPreferences()
	{
		if (!Load())
		{
			SetDefault();
		}

		SetDitry(true);
	}

	~HdRprPreferences()
	{
		Save();
	}

	bool Load()
	{
		std::string tmpDir = HdRprApi::GetTmpDir();
		std::string rprPreferencePath = (tmpDir.empty()) ? k_pathToRprPreference : tmpDir + k_pathToRprPreference;

		if (FILE * f = fopen(rprPreferencePath.c_str(), "rb"))
		{
			if (!fread(&m_prefData, sizeof(PrefData), 1, f))
			{
				TF_CODING_ERROR("Fail to read rpr preferences dat file");
			}
			fclose(f);
			return IsValid();
		}

		return false;
	}

	void Save()
	{
		std::string tmpDir = HdRprApi::GetTmpDir();
		std::string rprPreferencePath = (tmpDir.empty()) ? k_pathToRprPreference : tmpDir + k_pathToRprPreference;

		if (FILE * f = fopen(rprPreferencePath.c_str(), "wb"))
		{
			if (!fwrite(&m_prefData, sizeof(PrefData), 1, f))
			{
				TF_CODING_ERROR("Fail to write rpr preferences dat file");
			}
			fclose(f);
		}
	}

	bool IsValid()
	{
		return (m_prefData.mRenderDevice >= HdRprRenderDevice::FIRST && m_prefData.mRenderDevice <= HdRprRenderDevice::LAST)
			&& (m_prefData.mAov >= HdRprAov::FIRST && m_prefData.mAov <= HdRprAov::LAST)
			&& (m_prefData.mFilterType >= FilterType::FIRST && m_prefData.mFilterType <= FilterType::LAST);
	}

	void SetDefault()
	{
		m_prefData.mRenderDevice = HdRprRenderDevice::GPU;
		m_prefData.mAov = HdRprAov::COLOR;
		m_prefData.mFilterType = FilterType::BilateralDenoise;
	}

	struct PrefData
	{
		HdRprRenderDevice mRenderDevice = HdRprRenderDevice::NONE;
		HdRprAov mAov = HdRprAov::NONE;
		FilterType mFilterType = FilterType::None;
	} m_prefData;
	

	bool m_isDirty = true;
	bool m_isFilterDirty = true;

};


class HdRprApiImpl
{
public:
	void Init()
	{
		InitRpr();
		InitMaterialSystem();
		CreateScene();
		CreateFramebuffer(k_defaultFbWidth, k_defaultFbHeight);
		CreatePosteffects();
		CreateCamera();
	}

	void Deinit()
	{
		DeleteFramebuffers();

		SAFE_DELETE_RPR_OBJECT(m_scene);
		SAFE_DELETE_RPR_OBJECT(m_camera);
		SAFE_DELETE_RPR_OBJECT(m_tonemap);
		SAFE_DELETE_RPR_OBJECT(m_matsys);
	}

	void CreateScene() {
		
		if (!m_context)
		{
			return;
		}

		if (RPR_ERROR_CHECK(rprContextCreateScene(m_context, &m_scene), "Fail to create scene")) return;
		if (RPR_ERROR_CHECK(rprContextSetScene(m_context, m_scene), "Fail to set scene")) return;
	}

	void CreateCamera() {
		if (!m_context)
		{
			return;
		}

		RPR_ERROR_CHECK(rprContextCreateCamera(m_context, &m_camera), "Fail to create camera");
		RPR_ERROR_CHECK(rprCameraLookAt(m_camera, 20.0f, 60.0f, 40.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f), "Fail to set camera Look At");
		
		const rpr_float  sensorSize[] = { 1.f , 1.f};
		RPR_ERROR_CHECK(rprCameraSetSensorSize(m_camera, sensorSize[0], sensorSize[1]), "Fail to to set camera sensor size");
		RPR_ERROR_CHECK(rprSceneSetCamera(m_scene, m_camera), "Fail to to set camera to scene");
		//unlock();
	}

	void * CreateMesh(const VtVec3fArray & points, const VtVec3fArray & normals, const VtVec2fArray & uv, const VtIntArray & indexes, const VtIntArray & vpf, rpr_material_node material = nullptr)
	{
		if (!m_context)
		{
			return nullptr;
		}

		rpr_int status = RPR_SUCCESS;
		rpr_shape mesh = nullptr;

		VtIntArray newIndexes, newVpf;
		SplitPolygons(indexes, vpf, newIndexes, newVpf);

		lock();
		if (RPR_ERROR_CHECK(rprContextCreateMesh(m_context,
			(rpr_float const*)points.data(), points.size(), sizeof(GfVec3f),
			(rpr_float const*)((normals.size() == 0) ? 0 : normals.data()), normals.size(), sizeof(GfVec3f),
			(rpr_float const*)((uv.size() == 0) ? 0 : uv.data()), uv.size(), sizeof(GfVec2f),
			(rpr_int const*)newIndexes.data(), sizeof(rpr_int),
			(rpr_int const*)newIndexes.data(), sizeof(rpr_int),
			(rpr_int const*)newIndexes.data(), sizeof(rpr_int),
			newVpf.data(), newVpf.size(), &mesh)
			, "Fail create mesh")) {
			unlock();
			return nullptr;
		}
		unlock();

		if (RPR_ERROR_CHECK(rprSceneAttachShape(m_scene, mesh), "Fail attach mesh to scene")) return nullptr;

		if (material)
		{
			lock();
			rprShapeSetMaterial(mesh, material);
			unlock();
		}

		return mesh;
	}

	void SetMeshTransform(rpr_shape mesh, const GfMatrix4f & transform)
	{
		lock();
		RPR_ERROR_CHECK(rprShapeSetTransform(mesh, false, transform.GetArray()), "Fail set mesh transformation");
		unlock();
	}

	void SetMeshRefineLevel(rpr_shape mesh, const int level, const TfToken boundaryInterpolation)
	{
		rpr_int status;
		lock();
		status = RPR_ERROR_CHECK(rprShapeSetSubdivisionFactor(mesh, level), "Fail set mesh subdividion");
		unlock();

		if (status != RPR_SUCCESS) {
			return;
		}

		if (level > 0) {
			rpr_subdiv_boundary_interfop_type interfopType = RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_AND_CORNER
				? boundaryInterpolation == PxOsdOpenSubdivTokens->edgeAndCorner :
				RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_ONLY;
			RPR_ERROR_CHECK(rprShapeSetSubdivisionBoundaryInterop(mesh, interfopType),"Fail set mesh subdividion boundary");
		}
	}

	void SetMeshMaterial(rpr_shape mesh, const RprApiMaterial * material)
	{
		MaterialFactory * materialFactory = GetMaterialFactory(material->GetType());
		
		if (!materialFactory)
		{
			return;
		}

		lock();
		materialFactory->AttachMaterialToShape(mesh, material);
		unlock();
	}
	void SetMeshHeteroVolume(rpr_shape mesh, const RprApiObject heteroVolume)
	{
		RPR_ERROR_CHECK(rprShapeSetHeteroVolume(mesh, heteroVolume), "Fail set mesh hetero volume");
	}

	void SetCurveMaterial(rpr_shape curve, const RprApiMaterial * material)
	{
		MaterialFactory * materialFactory = GetMaterialFactory(material->GetType());

		if (!materialFactory)
		{
			return;
		}
		
		lock();
		materialFactory->AttachCurveToShape(curve, material);
		unlock();
	}

	void * CreateMeshInstance(rpr_shape mesh)
	{
		if (!m_context)
		{
			return nullptr;
		}

		rpr_int status;
		rpr_shape meshInstance;
		lock();
		status = RPR_ERROR_CHECK(rprContextCreateInstance(m_context, mesh, &meshInstance), "Fail to create mesh instance");
		if (status != RPR_SUCCESS) {
			unlock();
			return nullptr;
		}

		status = RPR_ERROR_CHECK(rprSceneAttachShape(m_scene, meshInstance), "Fail to attach mesh instance");
		if (status != RPR_SUCCESS) {
			unlock();
			return nullptr;
		}

		unlock();
		return meshInstance;
	}

	void SetMeshVisibility(rpr_shape mesh, bool isVisible)
	{
		RPR_ERROR_CHECK(rprShapeSetVisibility(mesh, isVisible), "Fail to set mesh visibility");
	}

	void * CreateCurve(const VtVec3fArray & points, const VtIntArray & indexes, const float & width)
	{
		if (!m_context || points.empty() || indexes.empty())
		{
			return nullptr;
		}

		const size_t k_segmentSize = 4;

		rpr_int status;
		rpr_curve curve = 0;

		VtVec3fArray newPoints = points;
		VtIntArray newIndexes = indexes;

		if (size_t extraPoints = newPoints.size() % k_segmentSize)
		{
			newPoints.resize(points.size() + k_segmentSize - extraPoints);
			newIndexes.resize(indexes.size() + k_segmentSize - extraPoints);

			for (int i = 0; i < k_segmentSize; ++i)
			{
				newPoints[newPoints.size() - i - 1] = points[points.size() - i - 1];
				newIndexes[newIndexes.size() - i - 1] = indexes[indexes.size() - i - 1];
			}

		}

		const size_t segmentPerCurve = newPoints.size() / k_segmentSize;
		std::vector<float> curveWidths(points.size(), width);
		std::vector<int> segmentsPerCurve(points.size(), segmentPerCurve);
		
		lock();
		if (RPR_ERROR_CHECK(rprContextCreateCurve(m_context,
			&curve
			, newPoints.size()
			, (float*)newPoints.data()
			, sizeof(GfVec3f)
			, newIndexes.size()
			, 1
			, (const rpr_uint *)newIndexes.data()
			, &width, NULL
			, segmentsPerCurve.data()), "Fail to create curve"))
		{
			unlock();
			return nullptr;
		};
		unlock();


		if (RPR_ERROR_CHECK(rprSceneAttachCurve(m_scene, curve), "Fail to attach curve")) return nullptr;

		return curve;
	}

	void CreateEnvironmentLight(const std::string & path, float intensity)
	{
		if (!m_context)
		{
			return;
		}

		rpr_light light;
		rpr_image image = nullptr;

		if (RPR_ERROR_CHECK(rprContextCreateImageFromFile(m_context, path.c_str(), &image), std::string("Fail to load image ") + path)) return;
		if (RPR_ERROR_CHECK(rprContextCreateEnvironmentLight(m_context, &light), "Fail to create environment light")) return;
		if (RPR_ERROR_CHECK(rprEnvironmentLightSetImage(light, image),"Fail to set image to environment light")) return;
		if (RPR_ERROR_CHECK(rprEnvironmentLightSetIntensityScale(light, intensity), "Fail to set environment light intencity")) return;
		if (RPR_ERROR_CHECK(rprSceneAttachLight(m_scene, light), "Fail to attach environment light")) return;

		m_isLightPresent = true;
	}

	void CreateEnvironmentLight(GfVec3f color, float intensity)
	{
		if (!m_context)
		{
			return;
		}

		rpr_image image = nullptr;

		// Add an environment light to the scene with the image attached.
		rpr_light light;

		// Set the background image to a solid color.
		std::array<float, 3> backgroundColor = { color[0],  color[1],  color[2]};
		rpr_image_format format = { 3, RPR_COMPONENT_TYPE_FLOAT32 };
		rpr_image_desc desc = { 1, 1, 1, 3, 3 };
		//lock();

		if (RPR_ERROR_CHECK(rprContextCreateImage(m_context, format, &desc, backgroundColor.data(), &image),"Fail to create image from color")) return;
		if (RPR_ERROR_CHECK(rprContextCreateEnvironmentLight(m_context, &light), "Fail to create environment light")) return;
		if (RPR_ERROR_CHECK(rprEnvironmentLightSetImage(light, image), "Fail to set image to environment light")) return;
		if (RPR_ERROR_CHECK(rprEnvironmentLightSetIntensityScale(light, intensity), "Fail to set environment light intensity")) return;
		if (RPR_ERROR_CHECK(rprSceneAttachLight(m_scene, light), "Fail to attach environment light to scene")) return;

		m_isLightPresent = true;
	}

	void * CreateRectLightGeometry(const float & width, const float & height)
	{
		constexpr const size_t rectVertexCount = 4;
		VtVec3fArray positions(rectVertexCount);
		positions[0] = GfVec3f(width * 0.5f, height * 0.5f, 0.f);
		positions[1] = GfVec3f(width * 0.5f, height * -0.5f, 0.f);
		positions[2] = GfVec3f(width * -0.5f, height * -0.5f, 0.f);
		positions[3] = GfVec3f(width * -0.5f, height * 0.5f, 0.f);

		// All normals -z
		VtVec3fArray normals(rectVertexCount, GfVec3f(0.f,0.f,-1.f));

		VtIntArray idx(rectVertexCount);
		idx[0] = 0;
		idx[1] = 1;
		idx[2] = 2;
		idx[3] = 3;

		VtIntArray vpf(1, rectVertexCount);

		VtVec2fArray uv; // empty

		m_isLightPresent = true;

		return CreateMesh(positions, normals, uv, idx, vpf);
	}

	void * CreateDiskLight(const float & width, const float & height, const GfVec3f & color)
	{

		VtVec3fArray positions;
		VtVec3fArray normals;
		VtVec2fArray uv; // empty
		VtIntArray idx;
		VtIntArray vpf;

		const float step = M_PI * 2 / k_diskVertexCount;
		for (int i = 0; i < k_diskVertexCount; ++i)
		{
			positions.push_back(GfVec3f(width * sin(step * i), height * cos(step * i), 0.f));
			positions.push_back(GfVec3f(width * sin(step * (i + 1)), height * cos(step * (i + 1)), 0.f));
			positions.push_back(GfVec3f(0., 0., 0.f));

			normals.push_back(GfVec3f(0.f, 0.f, -1.f));
			normals.push_back(GfVec3f(0.f, 0.f, -1.f));
			normals.push_back(GfVec3f(0.f, 0.f, -1.f));

			idx.push_back(i * 3);
			idx.push_back(i * 3 + 1);
			idx.push_back(i * 3 + 2);

			vpf.push_back(3);
		}

		rpr_material_node material = NULL;

		if (RPR_ERROR_CHECK(rprMaterialSystemCreateNode(m_matsys, RPR_MATERIAL_NODE_EMISSIVE, &material), "Fail create emmisive material")) return nullptr;
		if (RPR_ERROR_CHECK(rprMaterialNodeSetInputF(material, "color", color[0], color[1], color[2], 0.0f),"Fail set material color")) return nullptr;

		m_isLightPresent = true;

		return CreateMesh(positions, normals, uv, idx, vpf, material);
	}

	void * CreateSphereLightGeometry(const float & radius)
	{
		VtVec3fArray positions;
		VtVec3fArray normals;
		VtVec2fArray uv;
		VtIntArray idx;
		VtIntArray vpf;

		constexpr int nx = 16, ny = 16;

		const float d = radius;

		for (int j = ny - 1; j >= 0; j--)
		{
			for (int i = 0; i < nx; i++)
			{
				float t = i / (float)nx * M_PI;
				float p = j / (float)ny * 2.f * M_PI;
				positions.push_back(d * GfVec3f(sin(t)*cos(p), cos(t), sin(t)*sin(p)));
				normals.push_back(GfVec3f(sin(t)*cos(p), cos(t), sin(t)*sin(p)));
			}
		}

		for (int j = 0; j < ny; j++)
		{
			for (int i = 0; i < nx - 1; i++)
			{
				int o0 = j*nx;
				int o1 = ((j + 1) % ny)*nx;
				idx.push_back(o0 + i);
				idx.push_back(o0 + i + 1);
				idx.push_back(o1 + i + 1);
				idx.push_back(o1 + i);
				vpf.push_back(4);
			}
		}

		m_isLightPresent = true;

		return CreateMesh(positions, normals, uv, idx, vpf);

	}

	RprApiMaterial * CreateMaterial(const MaterialAdapter & materialAdapter)
	{
		MaterialFactory * materialFactory = GetMaterialFactory(materialAdapter.GetType());

		if (!materialFactory)
		{
			return nullptr;
		}

		lock();
		RprApiMaterial * material = materialFactory->CreateMaterial(materialAdapter.GetType());

		materialFactory->SetMaterialInputs(material, materialAdapter);
		unlock();

		return material;
	}

	void * CreateHeterVolume(const VtArray<float> & gridDencityData, const VtArray<size_t> & indexesDencity, const VtArray<float> & gridAlbedoData, const VtArray<unsigned int> & indexesAlbedo, const GfVec3i & grigSize)
	{
		if (!m_context)
		{
			return nullptr;
		}

		rpr_hetero_volume heteroVolume = nullptr;

		rpr_grid rprGridDencity;
		if (RPR_ERROR_CHECK(rprContextCreateGrid(m_context, &rprGridDencity
			, grigSize[0], grigSize[1], grigSize[2], &indexesDencity[0]
			, indexesDencity.size(), RPR_GRID_INDICES_TOPOLOGY_I_U64
			, &gridDencityData[0], gridDencityData.size() * sizeof(gridDencityData[0])
			, NULL)
			, "Fail create dencity grid")) return nullptr;

		rpr_grid rprGridAlbedo;
		if (RPR_ERROR_CHECK(rprContextCreateGrid(m_context, &rprGridAlbedo
			, grigSize[0], grigSize[1], grigSize[2], &indexesAlbedo[0]
			, indexesAlbedo.size() / 3, RPR_GRID_INDICES_TOPOLOGY_XYZ_U32
			, &gridAlbedoData[0], gridAlbedoData.size() * sizeof(gridAlbedoData[0])
			, NULL)
			, "Fail create albedo grid")) return nullptr;

		

		if (RPR_ERROR_CHECK(rprContextCreateHeteroVolume( m_context, &heteroVolume), "Fail create hetero dencity volume")) return nullptr;
		if (RPR_ERROR_CHECK(rprHeteroVolumeSetDensityGrid(heteroVolume, rprGridDencity), "Fail to set density hetero volume")) return nullptr;
		if (RPR_ERROR_CHECK(rprHeteroVolumeSetAlbedoGrid(heteroVolume, rprGridAlbedo),"Fail to set albedo hetero volume")) return nullptr;
		if (RPR_ERROR_CHECK(rprSceneAttachHeteroVolume(m_scene, heteroVolume), "Fail attach hetero volume to scene")) return nullptr;

		return heteroVolume;
	}

	void SetHeteroVolumeTransform(RprApiObject heteroVolume, const GfMatrix4f & m)
	{
		RPR_ERROR_CHECK(rprHeteroVolumeSetTransform(heteroVolume, false, m.GetArray()), "Fail to set hetero volume transform");
	}

	
	void * CreateVolume(const VtArray<float> & gridDencityData, const VtArray<size_t> & indexesDencity, const VtArray<float> & gridAlbedoData, const VtArray<unsigned int> & indexesAlbedo, const GfVec3i & grigSize, const GfVec3f & voxelSize, RprApiObject out_mesh, RprApiObject out_heteroVolume)
	{
		RprApiObject heteroVolume = CreateHeterVolume(gridDencityData, indexesDencity, gridAlbedoData, indexesAlbedo, grigSize);

		if (!heteroVolume)
		{
			return nullptr;
		}


		RprApiObject cubeMesh = CreateCubeMesh(0.5f, 0.5f, 0.5f);

		if (!cubeMesh)
		{
			return nullptr;
		}


		MaterialAdapter matAdapter = MaterialAdapter(EMaterialType::TRANSPERENT,
			MaterialParams{ { TfToken("color"), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f))
				} }); // TODO: use token

		RprApiMaterial * transperantMaterial = CreateMaterial(matAdapter);

		if (!transperantMaterial)
		{
			return nullptr;
		}

		GfMatrix4f meshTransform;
		GfVec3f volumeSize = GfVec3f(voxelSize[0] * grigSize[0], voxelSize[1] * grigSize[1], voxelSize[2] * grigSize[2]);
		meshTransform.SetScale(volumeSize);

		SetMeshMaterial(cubeMesh, transperantMaterial);
		SetMeshHeteroVolume(cubeMesh, heteroVolume);
		SetMeshTransform(cubeMesh, meshTransform);
		SetHeteroVolumeTransform(heteroVolume, meshTransform);

		out_mesh = cubeMesh;
		out_heteroVolume = heteroVolume;

		return heteroVolume;
	}

	void CreatePosteffects()
	{
		if (!m_context)
		{
			return;
		}

		if(RPR_ERROR_CHECK(rprContextCreatePostEffect(m_context, RPR_POST_EFFECT_TONE_MAP, &m_tonemap), "Fail to create post effect")) return;
		RPR_ERROR_CHECK(rprContextAttachPostEffect(m_context, m_tonemap), "Fail to attach posteffect");
	}

	void CreateFramebuffer(const rpr_uint width, const rpr_uint height)
	{
		if (!m_context)
		{
			return;
		}

		m_framebufferDesc.fb_width = width;
		m_framebufferDesc.fb_height = height;

		rpr_framebuffer_format fmt = { 4, RPR_COMPONENT_TYPE_FLOAT32 };

		if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_colorBuffer), "Fail create color framebuffer")) return;
		if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_positionBuffer),"Fail create depth framebuffer")) return;
		if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_depthBuffer), "Fail create depth framebuffer")) return;;
		if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_normalBuffer), "Fail create normal framebuffer")) return;
		if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_objId), "Fail create object ID framebuffer")) return;
		if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_uv),"Fail create UV framebuffer")) return ;

#ifdef USE_GL_INTEROP

		glGenFramebuffers(1, &m_framebufferGL);
		glBindFramebuffer(GL_FRAMEBUFFER, m_framebufferGL);



		// Allocate an OpenGL texture.
		glGenTextures(1, &m_textureFramebufferGL);
		glBindTexture(GL_TEXTURE_2D, m_textureFramebufferGL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);

		glGenRenderbuffers(1, &m_depthrenderbufferGL);
		glBindRenderbuffer(GL_RENDERBUFFER, m_depthrenderbufferGL);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthrenderbufferGL);

		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_textureFramebufferGL, 0);

		GLenum glFbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if ( glFbStatus != GL_FRAMEBUFFER_COMPLETE)
		{
			TF_CODING_ERROR("Fail create GL framebuffer. Error code %d", glFbStatus);
			ClearFramebuffers();
			return;
		}

		rpr_int status = rprContextCreateFramebufferFromGLTexture2D(m_context, GL_TEXTURE_2D, 0, m_textureFramebufferGL, &m_resolvedBuffer);
		if (status != RPR_SUCCESS)
		{
			ClearFramebuffers();
			TF_CODING_ERROR("Fail create framebuffer. Error code %d", status);
			return;
		}

		glBindTexture(GL_TEXTURE_2D, 0);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
#else
		if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_resolvedBuffer), "Fail create resolved framebuffer")) return;
		m_framebufferData.resize(m_framebufferDesc.fb_width * m_framebufferDesc.fb_height * 4, 0.f);
#endif
		//unlock();

		ClearFramebuffers();

#ifdef USE_RIF
		CreateImageFilter();
#endif // USE_RIF
	
		//lock();
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_COLOR, m_colorBuffer), "fail to set color AOV");
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_WORLD_COORDINATE, m_positionBuffer), "fail to set coordinate AOV");
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_DEPTH, m_depthBuffer), "fail to set depth AOV");
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_OBJECT_ID, m_objId), "fail to set object id AOV");
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_UV, m_uv), "fail to set uv AOV");
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_GEOMETRIC_NORMAL, m_normalBuffer), "fail to set normal AOV");

		//unlock();
	}


	void SetFramebufferDirty(bool isDirty)
	{
		m_isFramebufferDirty = isDirty;
	}

	void ClearFramebuffers()
	{
		//lock();
		RPR_ERROR_CHECK(rprFrameBufferClear(m_colorBuffer), "Fail to clear color framebuffer");
		RPR_ERROR_CHECK(rprFrameBufferClear(m_positionBuffer), "Fail to clear position framebuffer");
		RPR_ERROR_CHECK(rprFrameBufferClear(m_depthBuffer), "Fail to clear depth framebuffer");
		RPR_ERROR_CHECK(rprFrameBufferClear(m_objId), "Fail to clear object ID framebuffer");
		RPR_ERROR_CHECK(rprFrameBufferClear(m_uv), "Fail to clear uv framebuffer");
		RPR_ERROR_CHECK(rprFrameBufferClear(m_normalBuffer), "Fail to clear normal framebuffer");
		RPR_ERROR_CHECK(rprFrameBufferClear(m_resolvedBuffer), "Fail to clear resolved framebuffer");
		//unlock();
	}

	void SetCameraViewMatrix(const GfMatrix4d & m)
	{

		const GfMatrix4d & iwvm = m.GetInverse();
		const GfMatrix4d & wvm = m;


		GfVec3f eye(iwvm[3][0], iwvm[3][1], iwvm[3][2]);
		GfVec3f up(wvm[0][1], wvm[1][1], wvm[2][1]);
		GfVec3f n(wvm[0][2], wvm[1][2], wvm[2][2]);
		GfVec3f at(eye - n);

		//lock();
		RPR_ERROR_CHECK(rprCameraLookAt(m_camera, eye[0], eye[1], eye[2], at[0], at[1], at[2], up[0], up[1], up[2]), "Fail to set camera Look At");
		//unlock();

		m_cameraViewMatrix = m;
	}

	void SetCameraProjectionMatrix(const GfMatrix4d & proj)
	{
		float sensorSize[2];

		if(RPR_ERROR_CHECK(rprCameraGetInfo(m_camera, RPR_CAMERA_SENSOR_SIZE, sizeof(sensorSize), &sensorSize, NULL), "Fail to get camera swnsor size parameter")) return;
		
		const float focalLength = sensorSize[1] * proj[1][1] / 2;
		if (RPR_ERROR_CHECK(rprCameraSetFocalLength(m_camera, focalLength), "Fail to set focal length parameter")) return;

		m_cameraProjectionMatrix = proj;
	}

	const GfMatrix4d & GetCameraViewMatrix() const
	{
		return m_cameraViewMatrix;
	}

	const GfMatrix4d & GetCameraProjectionMatrix() const
	{
		return m_cameraProjectionMatrix;
	}

#ifdef USE_GL_INTEROP
	const GLuint GetFramebufferGL() const
	{
		return m_framebufferGL;
	}

#else
	const float * GetFramebufferData()
	{
		size_t fb_data_size = 0;

		if (RPR_ERROR_CHECK(rprFrameBufferGetInfo(m_resolvedBuffer, RPR_FRAMEBUFFER_DATA, 0, NULL, &fb_data_size), "Fail to get frafebuffer data size")) return nullptr;

		RPR_ERROR_CHECK(rprFrameBufferGetInfo(m_resolvedBuffer, RPR_FRAMEBUFFER_DATA, fb_data_size, m_framebufferData.data(), NULL), "Fail to get frafebuffer data");
		return m_framebufferData.data();
	}
#endif

	void GetFramebufferSize(rpr_int & width, rpr_int & height) const
	{
		width = m_framebufferDesc.fb_width;
		height = m_framebufferDesc.fb_height;
	}

	void ResizeFramebuffer(const GfVec2i & resolution) {
		DeleteFramebuffers();
		CreateFramebuffer(resolution[0], resolution[1]);

		rpr_float  sensorSize[] = { 1.f ,(float)resolution[1] / (float)resolution[0]};
		RPR_ERROR_CHECK(rprCameraSetSensorSize(m_camera, sensorSize[0], sensorSize[1]), "Fail to set camera sensor size");
	}

    void SetAov(const HdRprAov & aov)
    {
        m_currentAov = aov;
    }

	void Render()
	{
		if (!m_context)
		{
			return;
		}

		if (m_isFramebufferDirty)
		{
			ClearFramebuffers();
			SetFramebufferDirty(false);
		}


		// In case there is no Lights in scene - create dafault
		if (!m_isLightPresent)
		{
			CreateEnvironmentLight(k_defaultLightColor, 1.f);
		}

		if(RPR_ERROR_CHECK(rprContextRender(m_context),"Fail contex render framebuffer")) return;

        rpr_framebuffer targetFB = GetTargetFB();

       

#ifdef USE_RIF
		if (HdRprPreferences::GetInstance().IsFilterTypeDirty())
		{
			CreateImageFilter();
		}


		if (m_imageFilterPtr && m_imageFilterPtr->GetType() != FilterType::None && m_currentAov == HdRprAov::COLOR)
		{
			m_imageFilterPtr->Run();
		}
		else
		{
			auto status = rprContextResolveFrameBuffer(m_context, targetFB, m_resolvedBuffer, false);
			//unlock();
			if (status != RPR_SUCCESS)
			{
				TF_CODING_ERROR("Fail contex resolve. Error code %d", status);
			}
		}
#else
		if (RPR_ERROR_CHECK(rprContextResolveFrameBuffer(m_context, targetFB, m_resolvedBuffer, false), "Fail contex resolve")) return;
#endif // USE_RIF
	}

	void DeleteFramebuffers()
	{
		SAFE_DELETE_RPR_OBJECT(m_colorBuffer);
		SAFE_DELETE_RPR_OBJECT(m_positionBuffer);
        SAFE_DELETE_RPR_OBJECT(m_depthBuffer);
        SAFE_DELETE_RPR_OBJECT(m_objId);
        SAFE_DELETE_RPR_OBJECT(m_uv);
        SAFE_DELETE_RPR_OBJECT(m_normalBuffer);
		SAFE_DELETE_RPR_OBJECT(m_resolvedBuffer);

#ifdef USE_GL_INTEROP
		if (m_depthrenderbufferGL != INVALID_FRAMEBUFFER)
		{
			glDeleteRenderbuffers(1, &m_depthrenderbufferGL);
			m_depthrenderbufferGL = INVALID_FRAMEBUFFER;
		}

		if (m_framebufferGL != INVALID_FRAMEBUFFER)
		{
			glDeleteFramebuffers(1, &m_framebufferGL);
			m_framebufferGL = INVALID_FRAMEBUFFER;
		}

		if (m_textureFramebufferGL != INVALID_TEXTURE)
		{
			glDeleteTextures(1, &m_textureFramebufferGL);
			m_textureFramebufferGL = INVALID_TEXTURE;
		}
#endif

	}

	void DeleteRprObject(void * object)
	{
		SAFE_DELETE_RPR_OBJECT(object);
	}

	void DeleteMesh(void * mesh)
	{
		rpr_int status;

		lock();

		status = RPR_ERROR_CHECK(rprShapeSetMaterial(mesh, NULL),"Fail reset mesh material") ;

		if (status != RPR_SUCCESS)
		{
			unlock();
			return;
		}
		status = RPR_ERROR_CHECK(rprSceneDetachShape(m_scene, mesh), "Fail detach mesh from scene");
		unlock();

		if (status != RPR_SUCCESS)
		{
			return;
		}

		SAFE_DELETE_RPR_OBJECT(mesh);
	}

private:
	void InitRpr()
	{
		//lock();

        const std::string rprSdkPath = GetRprSdkPath();
		const std::string rprTmpDir = HdRprApi::GetTmpDir();
        const std::string tahoePath = (rprSdkPath.empty()) ? k_TahoeLibName : rprSdkPath + "/" + k_TahoeLibName;
		rpr_int tahoePluginID = rprRegisterPlugin(tahoePath.c_str());
		rpr_int plugins[] = { tahoePluginID };


		rpr_creation_flags flags = getRprCreationFlags(HdRprPreferences::GetInstance().GetRenderDevice());
		if (!flags)
		{
			return;
		}

		if (RPR_ERROR_CHECK(rprCreateContext(RPR_API_VERSION, plugins, 1, flags, NULL, rprTmpDir.c_str(), &m_context), std::string("Fail to create context with plugin ") + k_TahoeLibName)) return;

		if(RPR_ERROR_CHECK(rprContextSetActivePlugin(m_context, plugins[0]), "fail to set active plugin")) return;


		RPR_ERROR_CHECK(rprContextSetParameter1u(m_context, "yflip", 0), "Fail to set context YFLIP parameter");

		GLenum err = glewInit();
		if (err != GLEW_OK) {
			TF_CODING_ERROR("Fail init GLEW. Error code %s", glewGetErrorString(err));
		}
	}

	void InitMaterialSystem()
	{
		if (!m_context)
		{
			return;
		}

		if(RPR_ERROR_CHECK(rprContextCreateMaterialSystem(m_context, 0, &m_matsys), "Fail create Material System resolve")) return;

		m_rprMaterialFactory.reset(new RprMaterialFactory(m_matsys));
		m_rprxMaterialFactory.reset(new RprXMaterialFactory(m_matsys, m_context));
	}


#ifdef USE_RIF
	void CreateImageFilter()
	{
		const FilterType filterType = HdRprPreferences::GetInstance().GetFilterType();
		if (filterType == FilterType::None)
		{
			m_imageFilterPtr.reset();
			return;
		}

		m_imageFilterPtr.reset( new ImageFilter(m_context, m_framebufferDesc.fb_width, m_framebufferDesc.fb_height));
		m_imageFilterPtr->CreateFilter(filterType);

		m_imageFilterPtr->Resize(m_framebufferDesc.fb_width, m_framebufferDesc.fb_height);
		switch (m_imageFilterPtr->GetType())
		{
		case FilterType::BilateralDenoise:
		{
			RifParam p = { RifParamType::RifInt, {2} };
			m_imageFilterPtr->AddParam("radius", p);

			m_imageFilterPtr->SetInput(RifFilterInput::RifColor, m_colorBuffer, 1.0f);
			m_imageFilterPtr->SetInput(RifFilterInput::RifDepth, m_depthBuffer, 1.0f);
		}
		break;
		case FilterType::EawDenoise:
		{
			RifParam rifParam;
			rifParam.mData.f = 1.f;
			rifParam.mType = RifParamType::RifFloat;
			m_imageFilterPtr->AddParam("colorSigma", rifParam);
			m_imageFilterPtr->AddParam("normalSigma", rifParam);
			m_imageFilterPtr->AddParam("depthSigma", rifParam);
			m_imageFilterPtr->AddParam("transSigma", rifParam);

			m_imageFilterPtr->SetInput(RifFilterInput::RifColor, m_colorBuffer, 1.0f);
			m_imageFilterPtr->SetInput(RifFilterInput::RifWorldCoordinate, m_positionBuffer, 1.0f);
			m_imageFilterPtr->SetInput(RifFilterInput::RifDepth, m_depthBuffer, 1.0f);
			m_imageFilterPtr->SetInput(RifFilterInput::RifNormal, m_normalBuffer, 1.0f);
			m_imageFilterPtr->SetInput(RifFilterInput::RifTrans, m_objId, 1.0f);
			m_imageFilterPtr->SetInput(RifFilterInput::RifObjectId, m_objId, 1.0f);
		}
		break;
		default:
			return;
		}

#ifdef USE_GL_INTEROP
		m_imageFilterPtr->SetOutputGlTexture(m_textureFramebufferGL);
#else
		m_imageFilterPtr->SetOutput(m_resolvedBuffer);
#endif
		m_imageFilterPtr->AttachFilter();
	}

	void DeleteImageFilter()
	{
		m_imageFilterPtr.reset();
	}
#endif // USE_RIF

	void SplitPolygons(const VtIntArray & indexes, const VtIntArray & vpf, VtIntArray & out_newIndexes, VtIntArray & out_newVpf)
	{
		out_newIndexes.clear();
		out_newVpf.clear();

		out_newIndexes.reserve(indexes.size());
		out_newVpf.reserve(vpf.size());

		VtIntArray::const_iterator idxIt = indexes.begin();
		for (const int vCount : vpf)
		{
			if (vCount == 3 || vCount == 4)
			{
				for (int i = 0; i < vCount; ++i)
				{
					out_newIndexes.push_back(*idxIt);
					++idxIt;
				}
				out_newVpf.push_back(vCount);
			}
			else
			{
				constexpr int triangleVertexCount = 3;
				for (int i = 0; i < vCount - 2; ++i)
				{
					out_newIndexes.push_back(*(idxIt + i + 0));
					out_newIndexes.push_back(*(idxIt + i + 1));
					out_newIndexes.push_back(*(idxIt + i + 2));
					out_newVpf.push_back(triangleVertexCount);
				}
				idxIt += vCount;
			}
		}
	}


	MaterialFactory * GetMaterialFactory(const EMaterialType type)
	{
		switch (type)
		{
		case EMaterialType::USD_PREVIEW_SURFACE:
			return m_rprxMaterialFactory.get();

		case EMaterialType::COLOR:
		case EMaterialType::EMISSIVE:
		case EMaterialType::TRANSPERENT:
			return m_rprMaterialFactory.get();
		default:
				break;
		}

		TF_CODING_WARNING("Unknown material type");
		return nullptr;
	}

    const rpr_framebuffer GetTargetFB() const
    {
		switch (HdRprPreferences::GetInstance().GetAov())
        {
            case HdRprAov::COLOR: return m_colorBuffer;
            case HdRprAov::NORMAL: return m_normalBuffer;
            case HdRprAov::PRIM_ID: return m_objId;
            case HdRprAov::DEPTH: return m_depthBuffer;
            case HdRprAov::UV: return m_uv;

            default:
                break;
        }

		TF_CODING_WARNING("Unknown terget aov type. Used default: HdRprAov::COLOR");
		return m_colorBuffer;
    }

	void * CreateCubeMesh(const float & width, const float & height, const float & depth)
	{
		constexpr const size_t cubeVertexCount = 24;
		constexpr const size_t cubeNormalCount = 24;
		constexpr const size_t cubeIndexCount = 36;
		constexpr const size_t cubeVpfCount = 12;

		VtVec3fArray position(cubeVertexCount);
		position[0] = GfVec3f(-width, height, -depth);
		position[1] = GfVec3f(width, height, -depth);
		position[2] = GfVec3f(width, height, depth);
		position[3] = GfVec3f(-width, height, depth);

		position[4] = GfVec3f(-width, -height, -depth);
		position[5] = GfVec3f(width, -height, -depth);
		position[6] = GfVec3f(width, -height, depth);
		position[7] = GfVec3f(-width, -height, depth);

		position[8] = GfVec3f(-width, -height, depth);
		position[9] = GfVec3f(-width, -height, -depth);
		position[10] = GfVec3f(-width, height, -depth);
		position[11] = GfVec3f(-width, height, depth);

		position[12] = GfVec3f(width, -height, depth);
		position[13] = GfVec3f(width, -height, -depth);
		position[14] = GfVec3f(width, height, -depth);
		position[15] = GfVec3f(width, height, depth);

		position[16] = GfVec3f(-width, -height, -depth);
		position[17] = GfVec3f(width, -height, -depth);
		position[18] = GfVec3f(width, height, -depth);
		position[19] = GfVec3f(-width, height, -depth);

		position[20] = GfVec3f(-width, -height, depth);
		position[21] = GfVec3f(width, -height, depth);
		position[22] = GfVec3f(width, height, depth);
		position[23] = GfVec3f(-width, height, depth);

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

		VtIntArray indexes(cubeIndexCount);
		{
			std::array<int, cubeIndexCount> indexArray =
			{
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

			memcpy(indexes.data(), indexArray.data(), sizeof(int) * cubeIndexCount);
		}

		VtIntArray vpf(cubeVpfCount, 3);
		VtVec2fArray uv; // empty

		return CreateMesh(position, normals, uv, indexes, vpf);
	}

	void lock() {
		while(m_lock.test_and_set(std::memory_order_acquire));
	}

	void unlock() {
		m_lock.clear(std::memory_order_release);
	}

	rpr_context m_context = nullptr;
	rpr_scene m_scene = nullptr;
	rpr_camera m_camera = nullptr;

	rpr_framebuffer m_colorBuffer = nullptr;
	rpr_framebuffer m_positionBuffer = nullptr;
    rpr_framebuffer m_depthBuffer = nullptr;
    rpr_framebuffer m_normalBuffer = nullptr;
    rpr_framebuffer m_objId = nullptr;
    rpr_framebuffer m_uv = nullptr;
	rpr_framebuffer m_resolvedBuffer = nullptr;
	rpr_post_effect m_tonemap = nullptr;

#ifdef USE_GL_INTEROP
	GLuint m_framebufferGL = INVALID_FRAMEBUFFER;
	GLuint m_depthrenderbufferGL;
	rpr_GLuint m_textureFramebufferGL = INVALID_TEXTURE;
#else
	std::vector<float> m_framebufferData;
#endif

	rpr_material_system m_matsys = nullptr;

	rpr_framebuffer_desc m_framebufferDesc = {};

	GfMatrix4d m_cameraViewMatrix = GfMatrix4d(1.f);
	GfMatrix4d m_cameraProjectionMatrix = GfMatrix4d(1.f);


	std::unique_ptr<RprMaterialFactory> m_rprMaterialFactory;
	std::unique_ptr<RprXMaterialFactory> m_rprxMaterialFactory;

	bool m_isLightPresent = false;

	bool m_isFramebufferDirty = true;

    bool m_isRenderModeDirty = true;

    HdRprAov m_currentAov = HdRprAov::COLOR;

	// simple spinlock for locking RPR calls
	std::atomic_flag m_lock = ATOMIC_FLAG_INIT;

#ifdef USE_RIF
	std::unique_ptr<ImageFilter> m_imageFilterPtr;
#endif // USE_RIF
};

	HdRprApi::HdRprApi() : m_impl(new HdRprApiImpl)
	{
	}

	HdRprApi::~HdRprApi()
	{
		delete m_impl;
	}

	void HdRprApi::SetRenderDevice(const HdRprRenderDevice & renderDevice)
	{
		HdRprPreferences::GetInstance().SetRenderDevice(renderDevice);
	}

	void HdRprApi::SetFilter(const FilterType & type)
	{
		HdRprPreferences::GetInstance().SetFilterType(type);
	}

	void HdRprApi::SetAov(const HdRprAov & aov)
	{
		HdRprPreferences::GetInstance().SetAov(aov);
	}

	void HdRprApi::Init()
	{
		m_impl->Init();
	}

	void HdRprApi::Deinit()
	{
		m_impl->Deinit();
	}

	RprApiObject HdRprApi::CreateMesh(const VtVec3fArray & points, const VtVec3fArray & normals, const VtVec2fArray & uv, const VtIntArray & indexes, const VtIntArray & vpf)
	{
		return m_impl->CreateMesh(points, normals, uv, indexes, vpf);
	}

	RprApiObject HdRprApi::CreateCurve(const VtVec3fArray & points, const VtIntArray & indexes, const float & width)
	{
		return m_impl->CreateCurve(points, indexes, width);
	}

	void HdRprApi::CreateInstances(RprApiObject prototypeMesh, const VtMatrix4dArray & transforms, VtArray<RprApiObject>& out_instances)
	{
		out_instances.clear();
		out_instances.reserve(transforms.size());
		for (const GfMatrix4d & transform : transforms)
		{
			if (void * meshInstamce = m_impl->CreateMeshInstance(prototypeMesh))
			{
				m_impl->SetMeshTransform(meshInstamce, GfMatrix4f(transform));
				out_instances.push_back(meshInstamce);
			}
		}

		// Hide prototype
		m_impl->SetMeshVisibility(prototypeMesh, false);
	}

	void HdRprApi::CreateEnvironmentLight(const std::string & prthTotexture, float intensity)
	{
		m_impl->CreateEnvironmentLight(prthTotexture, intensity);
		m_impl->SetFramebufferDirty(true);
	}

	RprApiObject HdRprApi::CreateRectLightMesh(const float & width, const float & height)
	{
		m_impl->SetFramebufferDirty(true);
		return m_impl->CreateRectLightGeometry(width, height);
	}

	RprApiObject HdRprApi::CreateSphereLightMesh(const float & radius)
	{
		m_impl->SetFramebufferDirty(true);
		return m_impl->CreateSphereLightGeometry(radius);
	}

	RprApiObject HdRprApi::CreateDiskLight(const float & width, const float & height, const GfVec3f & emmisionColor)
	{
		return m_impl->CreateDiskLight(width, height, emmisionColor);
		m_impl->SetFramebufferDirty(true);
	}

	void HdRprApi::CreateVolume(const VtArray<float> & gridDencityData, const VtArray<size_t> & indexesDencity, const VtArray<float> & gridAlbedoData, const VtArray<unsigned int> & indexesAlbedo, const GfVec3i & gridSize, const GfVec3f & voxelSize, RprApiObject out_mesh, RprApiObject out_heteroVolume)
	{
		m_impl->CreateVolume(gridDencityData, indexesDencity, gridAlbedoData, indexesAlbedo, gridSize, voxelSize, out_mesh, out_heteroVolume);
	}

	RprApiMaterial * HdRprApi::CreateMaterial(MaterialAdapter & materialAdapter)
	{
		return m_impl->CreateMaterial(materialAdapter);
	}

	void HdRprApi::ClearFramebuffer()
	{
		m_impl->SetFramebufferDirty(true);
	}

	void HdRprApi::SetMeshTransform(RprApiObject mesh, const GfMatrix4d & transform)
	{
		GfMatrix4f transformF(transform);
		m_impl->SetMeshTransform(mesh, transformF);
		m_impl->SetFramebufferDirty(true);
	}

	void HdRprApi::SetMeshRefineLevel(RprApiObject mesh, int level, TfToken boundaryInterpolation)
	{
		m_impl->SetMeshRefineLevel(mesh, level, boundaryInterpolation);
		m_impl->SetFramebufferDirty(true);
	}

	void HdRprApi::SetMeshMaterial(RprApiObject mesh, const RprApiMaterial * material)
	{
		m_impl->SetMeshMaterial(mesh, material);
	}

	void HdRprApi::SetCurveMaterial(RprApiObject curve, const RprApiMaterial * material)
	{
		m_impl->SetCurveMaterial(curve, material);
	}

	const GfMatrix4d & HdRprApi::GetCameraViewMatrix() const
	{
		return m_impl->GetCameraViewMatrix();
	}

	const GfMatrix4d & HdRprApi::GetCameraProjectionMatrix() const
	{
		return m_impl->GetCameraProjectionMatrix();
	}

	void HdRprApi::SetCameraViewMatrix(const GfMatrix4d & m)
	{
		m_impl->SetCameraViewMatrix(m);
		m_impl->SetFramebufferDirty(true);
	}

	void HdRprApi::SetCameraProjectionMatrix(const GfMatrix4d & m)
	{
		m_impl->SetCameraProjectionMatrix(m);
		m_impl->SetFramebufferDirty(true);
	}

	void HdRprApi::Resize(const GfVec2i & resolution)
	{
		m_impl->ResizeFramebuffer(resolution);
	}


	void HdRprApi::GetFramebufferSize(GfVec2i & resolution) const
	{
		m_impl->GetFramebufferSize(resolution[0], resolution[1]);
	}

	void HdRprApi::Render() {
		m_impl->Render();
	}

#ifdef USE_GL_INTEROP
	const GLuint HdRprApi::GetFramebufferGL() const
	{
		return m_impl->GetFramebufferGL();
	}
#else
	const float * HdRprApi::GetFramebufferData() const
	{
		return m_impl->GetFramebufferData();
	}
#endif

	void HdRprApi::DeleteRprApiObject(RprApiObject object)
	{
		m_impl->DeleteRprObject(object);
	}

	void HdRprApi::DeleteMesh(RprApiObject mesh)
	{
		m_impl->DeleteMesh(mesh);
	}


PXR_NAMESPACE_CLOSE_SCOPE
