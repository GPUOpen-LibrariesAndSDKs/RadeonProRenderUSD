#include "rprApi.h"

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
#if defined __APPLE__
	const char* k_RadeonProRenderLibName = "libRadeonProRender64.dylib";
#endif

	const char* k_PluginLibNames[] = {
#ifdef WIN32
		"Tahoe64.dll",
		"Hybrid.dll",
#elif defined __linux__
		"libTahoe64.so",
		"libHybrid.so",
#elif defined __APPLE__
		"libTahoe64.dylib",
		"libHybrid.dylib",
#endif
	};

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

	std::string rprErrorString = [](const rpr_status s) -> std::string {
		switch (s)
		{
		case RPR_ERROR_INVALID_API_VERSION: return "invalide api version";
		case RPR_ERROR_INVALID_PARAMETER: return "invalide parameter";
		case RPR_ERROR_UNSUPPORTED: return "unsupported";
		default:
			break;
		}

		return "error code - " + std::to_string(s);
	}(status);

	Tf_PostErrorHelper(context, TF_DIAGNOSTIC_CODING_ERROR_TYPE, "%s %s: %s", "[RPR ERROR] ", messageOnFail.c_str(), rprErrorString.c_str());

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
    if (auto homeEnv = getenv("HOME")) {
        static char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/Library/Application Support/hdRPR", homeEnv);
        return path;
    }
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

rpr_creation_flags getAllCompatibleGpuFlags(rpr_int pluginID, const char* cachePath)
{
    rpr_creation_flags additionalFlags = 0x0;
#ifdef WIN32
    RPR_TOOLS_OS rprToolOs = RPR_TOOLS_OS::RPRTOS_WINDOWS;
#elif defined(__APPLE__)
    RPR_TOOLS_OS rprToolOs = RPR_TOOLS_OS::RPRTOS_MACOS;
    additionalFlags |= RPR_CREATION_FLAGS_ENABLE_METAL;
#else
    RPR_TOOLS_OS rprToolOs = RPR_TOOLS_OS::RPRTOS_LINUX;
#endif // WIN32

    rpr_creation_flags creationFlags = 0x0;
#define TEST_GPU_COMPATIBILITY(index) \
    if (rprIsDeviceCompatible(pluginID, RPRTD_GPU ## index, cachePath, false, rprToolOs, additionalFlags) == RPRTC_COMPATIBLE) { \
        creationFlags |= RPR_CREATION_FLAGS_ENABLE_GPU ## index; \
    }

    TEST_GPU_COMPATIBILITY(0);
    TEST_GPU_COMPATIBILITY(1);
    TEST_GPU_COMPATIBILITY(2);
    TEST_GPU_COMPATIBILITY(3);
    TEST_GPU_COMPATIBILITY(4);
    TEST_GPU_COMPATIBILITY(5);
    TEST_GPU_COMPATIBILITY(6);
    TEST_GPU_COMPATIBILITY(7);

    return creationFlags;
}

const rpr_creation_flags getRprCreationFlags(const HdRprRenderDevice renderDevice, rpr_int pluginID, const char* cachePath)
{
	rpr_creation_flags flags = 0x0;

	if (HdRprRenderDevice::CPU == renderDevice)
	{
		flags = RPR_CREATION_FLAGS_ENABLE_CPU;
	}
	else if (HdRprRenderDevice::GPU == renderDevice)
	{
		flags = getAllCompatibleGpuFlags(pluginID, cachePath);
	}
	else
	{
		TF_CODING_ERROR("Unknown HdRprRenderDevice");
		return 0x0;
	}
	
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

	void SetHybridQuality(HdRprHybridQuality quality)
	{
		if (m_prefData.mHybridQuality != quality)
		{
			m_prefData.mHybridQuality = quality;
			Save();
			SetDitry(true);
		}
	}

	HdRprHybridQuality GetHybridQuality() const
	{
		if (m_prefData.mHybridQuality == HdRprHybridQuality::MEDIUM) {
			// temporarily disable until issues on hybrid side is not solved
			//   otherwise driver crashes guaranteed
			return HdRprHybridQuality::HIGH;
		}
		return m_prefData.mHybridQuality;
	}

	void SetPlugin(HdRprPluginType plugin)
	{
		if (m_prefData.mPlugin != plugin)
		{
			m_prefData.mPlugin = plugin;
			Save();
			SetDitry(true);
		}
	}

	HdRprPluginType GetPlugin()
	{
		return m_prefData.mPlugin;
	}

	void SetDenoising(bool enableDenoising)
	{
		m_prefData.mEnableDenoising = enableDenoising;
		SetFilterDirty(true);
	}

	bool IsDenoisingEnabled() const
	{
		return m_prefData.mEnableDenoising;
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

	void SetFilterDirty(bool isDirty)
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
			&& (m_prefData.mAov >= HdRprAov::NONE && m_prefData.mAov <= HdRprAov::LAST);
	}

	void SetDefault()
	{
		m_prefData.mRenderDevice = HdRprRenderDevice::GPU;
		m_prefData.mAov = HdRprAov::COLOR;
		m_prefData.mPlugin = HdRprPluginType::TAHOE;
		m_prefData.mHybridQuality = HdRprHybridQuality::LOW;
		m_prefData.mEnableDenoising = true;
	}

	struct PrefData
	{
		HdRprRenderDevice mRenderDevice = HdRprRenderDevice::GPU;
		HdRprAov mAov = HdRprAov::COLOR;
		bool mEnableDenoising = true;
		HdRprPluginType mPlugin = HdRprPluginType::TAHOE;
		HdRprHybridQuality mHybridQuality = HdRprHybridQuality::LOW;
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
	    for (auto material : m_materialsToRelease) {
	        DeleteMaterial(material);
	    }
	    m_materialsToRelease.clear();

		DeleteFramebuffers();

        for (auto rprObject : m_rprObjectsToRelease)
        {
            SAFE_DELETE_RPR_OBJECT(rprObject);
        }
		SAFE_DELETE_RPR_OBJECT(m_context);
	}

	void CreateScene() {
		
		if (!m_context)
		{
			return;
		}

		if (RPR_ERROR_CHECK(rprContextCreateScene(m_context, &m_scene), "Fail to create scene")) return;
        m_rprObjectsToRelease.push_back(m_scene);
		if (RPR_ERROR_CHECK(rprContextSetScene(m_context, m_scene), "Fail to set scene")) return;
	}

	void CreateCamera() {
		if (!m_context)
		{
			return;
		}

		RPR_ERROR_CHECK(rprContextCreateCamera(m_context, &m_camera), "Fail to create camera");
        m_rprObjectsToRelease.push_back(m_camera);
		RPR_ERROR_CHECK(rprCameraLookAt(m_camera, 20.0f, 60.0f, 40.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f), "Fail to set camera Look At");
		
		const rpr_float  sensorSize[] = { 1.f , 1.f};
		RPR_ERROR_CHECK(rprCameraSetSensorSize(m_camera, sensorSize[0], sensorSize[1]), "Fail to to set camera sensor size");
		RPR_ERROR_CHECK(rprSceneSetCamera(m_scene, m_camera), "Fail to to set camera to scene");
		//unlock();
	}

	void * CreateMesh(const VtVec3fArray & points, const VtIntArray & pointIndexes, const VtVec3fArray & normals, const VtIntArray & normalIndexes, const VtVec2fArray & uv, const VtIntArray & uvIndexes, const VtIntArray & vpf, rpr_material_node material = nullptr)
	{
		if (!m_context)
		{
			return nullptr;
		}

		rpr_shape mesh = nullptr;

		VtIntArray newIndexes, newVpf;
		SplitPolygons(pointIndexes, vpf, newIndexes, newVpf);

		VtIntArray newUvIndexes;
		if (!uvIndexes.empty()) {
			SplitPolygons(uvIndexes, vpf, newUvIndexes);
		}
		
		VtIntArray newNormalIndexes;
		if (!normalIndexes.empty()) {
			SplitPolygons(normalIndexes, vpf, newNormalIndexes);
		}

		lock();
		if (RPR_ERROR_CHECK(rprContextCreateMesh(m_context,
			(rpr_float const*)points.data(), points.size(), sizeof(GfVec3f),
			(rpr_float const*)((normals.size() == 0) ? 0 : normals.data()), normals.size(), sizeof(GfVec3f),
			(rpr_float const*)((uv.size() == 0) ? 0 : uv.data()), uv.size(), sizeof(GfVec2f),
			(rpr_int const*)newIndexes.data(), sizeof(rpr_int),
			(rpr_int const*)(!newNormalIndexes.empty() ? newNormalIndexes.data() : newIndexes.data()), sizeof(rpr_int),
			(rpr_int const*)(!newUvIndexes.empty() ? newUvIndexes.data() : newIndexes.data()), sizeof(rpr_int),
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
		if (m_currentPlugin == HdRprPluginType::HYBRID)
		{
			// Not supported
			return;
		}

		rpr_int status;
		lock();
		status = RPR_ERROR_CHECK(rprShapeSetSubdivisionFactor(mesh, level), "Fail set mesh subdividion");
		unlock();

		if (status != RPR_SUCCESS) {
			return;
		}

		if (level > 0) {
			rpr_subdiv_boundary_interfop_type interfopType = boundaryInterpolation == PxOsdOpenSubdivTokens->edgeAndCorner ?
                RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_AND_CORNER :
                RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_ONLY;
			RPR_ERROR_CHECK(rprShapeSetSubdivisionBoundaryInterop(mesh, interfopType),"Fail set mesh subdividion boundary");
		}
	}

	void SetMeshMaterial(rpr_shape mesh, const RprApiMaterial * material)
	{
		lock();
		m_rprMaterialFactory->AttachMaterialToShape(mesh, material);
		unlock();
	}
	void SetMeshHeteroVolume(rpr_shape mesh, const RprApiObject heteroVolume)
	{
		RPR_ERROR_CHECK(rprShapeSetHeteroVolume(mesh, heteroVolume), "Fail set mesh hetero volume");
	}

	void SetCurveMaterial(rpr_shape curve, const RprApiMaterial * material)
	{
		lock();
		m_rprMaterialFactory->AttachCurveToShape(curve, material);
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
        m_rprObjectsToRelease.push_back(image);
		if (RPR_ERROR_CHECK(rprContextCreateEnvironmentLight(m_context, &light), "Fail to create environment light")) return;
        m_rprObjectsToRelease.push_back(light);
		if (RPR_ERROR_CHECK(rprEnvironmentLightSetImage(light, image),"Fail to set image to environment light")) return;
		if (RPR_ERROR_CHECK(rprEnvironmentLightSetIntensityScale(light, intensity), "Fail to set environment light intencity")) return;
		if (m_currentPlugin == HdRprPluginType::HYBRID)
		{
			if (RPR_ERROR_CHECK(rprSceneSetEnvironmentLight(m_scene, light), "Fail to set environment light")) return;
		}
		else
		{
			if (RPR_ERROR_CHECK(rprSceneAttachLight(m_scene, light), "Fail to attach environment light to scene")) return;
		}

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
		std::array<float, 3> backgroundColor = { color[0],  color[1],  color[2] };
		rpr_image_format format = { 3, RPR_COMPONENT_TYPE_FLOAT32 };
		rpr_uint imageSize = m_currentPlugin == HdRprPluginType::HYBRID ? 64 : 1;
		rpr_image_desc desc = { imageSize, imageSize, 0, static_cast<rpr_uint>(imageSize * imageSize * 3 * sizeof(float)), 0 };
		std::vector<std::array<float, 3>> imageData(imageSize * imageSize, backgroundColor);
		//lock();

		if (RPR_ERROR_CHECK(rprContextCreateImage(m_context, format, &desc, imageData.data(), &image),"Fail to create image from color")) return;
        m_rprObjectsToRelease.push_back(image);
		if (RPR_ERROR_CHECK(rprContextCreateEnvironmentLight(m_context, &light), "Fail to create environment light")) return;
        m_rprObjectsToRelease.push_back(light);
		if (RPR_ERROR_CHECK(rprEnvironmentLightSetImage(light, image), "Fail to set image to environment light")) return;
		if (RPR_ERROR_CHECK(rprEnvironmentLightSetIntensityScale(light, intensity), "Fail to set environment light intensity")) return;
		if (m_currentPlugin == HdRprPluginType::HYBRID)
		{
			if (RPR_ERROR_CHECK(rprSceneSetEnvironmentLight(m_scene, light), "Fail to set environment light")) return;
		}
		else
		{
			if (RPR_ERROR_CHECK(rprSceneAttachLight(m_scene, light), "Fail to attach environment light to scene")) return;
		}
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

		return CreateMesh(positions, idx, normals, VtIntArray(), uv, VtIntArray(), vpf);
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
        m_rprObjectsToRelease.push_back(material);
		if (RPR_ERROR_CHECK(rprMaterialNodeSetInputF(material, "color", color[0], color[1], color[2], 0.0f),"Fail set material color")) return nullptr;

		m_isLightPresent = true;

		return CreateMesh(positions, idx, normals, VtIntArray(), uv, VtIntArray(), vpf, material);
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

		return CreateMesh(positions, idx, normals, VtIntArray(), uv, VtIntArray(), vpf);

	}

	RprApiMaterial* CreateMaterial(const MaterialAdapter & materialAdapter)
	{
		if (!m_context)
		{
			return nullptr;
		}
		
		lock();
		auto material = m_rprMaterialFactory->CreateMaterial(materialAdapter.GetType(), materialAdapter);
		unlock();

		return material;
	}

    void DeleteMaterial(RprApiMaterial* material)
    {
        lock();
        m_rprMaterialFactory->DeleteMaterial(material);
        unlock();
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
			, 0)
			, "Fail create dencity grid")) return nullptr;
        m_rprObjectsToRelease.push_back(rprGridDencity);

		rpr_grid rprGridAlbedo;
		if (RPR_ERROR_CHECK(rprContextCreateGrid(m_context, &rprGridAlbedo
			, grigSize[0], grigSize[1], grigSize[2], &indexesAlbedo[0]
			, indexesAlbedo.size() / 3, RPR_GRID_INDICES_TOPOLOGY_XYZ_U32
			, &gridAlbedoData[0], gridAlbedoData.size() * sizeof(gridAlbedoData[0])
			, 0)
			, "Fail create albedo grid")) return nullptr;
        m_rprObjectsToRelease.push_back(rprGridAlbedo);
		

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
        m_materialsToRelease.push_back(transperantMaterial);

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

		if (m_currentPlugin == HdRprPluginType::TAHOE)
		{
			if (!RPR_ERROR_CHECK(rprContextCreatePostEffect(m_context, RPR_POST_EFFECT_TONE_MAP, &m_tonemap), "Fail to create post effect"))
			{
                m_rprObjectsToRelease.push_back(m_tonemap);
				RPR_ERROR_CHECK(rprContextAttachPostEffect(m_context, m_tonemap), "Fail to attach posteffect");
			}
		}
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
		if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_albedoBuffer), "Fail create diffuse albedo framebuffer")) return;
		if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_objId), "Fail create object ID framebuffer")) return;
		if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_uv),"Fail create UV framebuffer")) return ;

		if (m_useGlInterop) {
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
		} else {
			if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_resolvedBuffer), "Fail create resolved framebuffer")) return;
			m_framebufferData.resize(m_framebufferDesc.fb_width * m_framebufferDesc.fb_height * 4, 0.f);
		}
		//unlock();

		ClearFramebuffers();

#ifdef USE_RIF
		CreateImageFilter();
#endif // USE_RIF
	
		//lock();
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_COLOR, m_colorBuffer), "fail to set color AOV");
        RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_DIFFUSE_ALBEDO, m_albedoBuffer), "fail to set albedo AOV");
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_WORLD_COORDINATE, m_positionBuffer), "fail to set coordinate AOV");
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_DEPTH, m_depthBuffer), "fail to set depth AOV");
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_OBJECT_ID, m_objId), "fail to set object id AOV");
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, RPR_AOV_UV, m_uv), "fail to set uv AOV");
		auto normalAovId = RPR_AOV_GEOMETRIC_NORMAL;
		if (m_currentPlugin == HdRprPluginType::HYBRID)
		{
			// TODO: remove me when Hybrid gain RPR_AOV_GEOMETRIC_NORMAL support
			normalAovId = RPR_AOV_SHADING_NORMAL;
		}
		RPR_ERROR_CHECK(rprContextSetAOV(m_context, normalAovId, m_normalBuffer), "fail to set normal AOV");
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
        RPR_ERROR_CHECK(rprFrameBufferClear(m_albedoBuffer), "Fail to clear color framebuffer");
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

	const GLuint GetFramebufferGL() const
	{
		return m_framebufferGL;
	}

    const float * GetFramebufferData()
    {
        if (m_currentRenderDevice == HdRprRenderDevice::CPU &&
            m_imageFilterPtr && HdRprPreferences::GetInstance().GetAov() == HdRprAov::COLOR)
        {
            m_framebufferData = m_imageFilterPtr->GetData();
        }
        else
        {
            rpr_framebuffer framebuffer = nullptr;
			if (m_currentPlugin == HdRprPluginType::TAHOE)
			{
				framebuffer = m_resolvedBuffer;
			}
			else if (m_currentPlugin == HdRprPluginType::HYBRID)
			{
				// So as Hybrid plugin does not support framebuffer resolving (rprContextResolveFrameBuffer)
				//   we have to distinguish renders with filter and without
				if (m_isRenderedWithFilter)
				{
					framebuffer = m_resolvedBuffer;
				}
				else
				{
					framebuffer = GetTargetFB();
				}
			}

			if (!framebuffer)
			{
				TF_CODING_ERROR("Could not get framebuffer data: invalid framebuffer");
				return nullptr;
			}

            size_t fb_data_size = 0;
			if (RPR_ERROR_CHECK(rprFrameBufferGetInfo(framebuffer, RPR_FRAMEBUFFER_DATA, 0, NULL, &fb_data_size), "Fail to get frafebuffer data size")) return nullptr;

			RPR_ERROR_CHECK(rprFrameBufferGetInfo(framebuffer, RPR_FRAMEBUFFER_DATA, fb_data_size, m_framebufferData.data(), NULL), "Fail to get frafebuffer data");
        }
        return m_framebufferData.data();
    }

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

	void Render()
	{
		if (!m_context)
		{
			return;
		}

		auto& preferences = HdRprPreferences::GetInstance();
		if (preferences.IsDirty())
		{
			if (m_currentPlugin == HdRprPluginType::HYBRID)
			{
				rprContextSetParameter1u(m_context, "render_quality", int(preferences.GetHybridQuality()));
			}
			preferences.SetDitry(false);
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

       
		m_isRenderedWithFilter = false;
#ifdef USE_RIF
		if (HdRprPreferences::GetInstance().IsFilterTypeDirty())
		{
			CreateImageFilter();
			HdRprPreferences::GetInstance().SetFilterDirty(false);
		}


		if (m_imageFilterPtr && HdRprPreferences::GetInstance().GetAov() == HdRprAov::COLOR)
		{
            switch (m_imageFilterType)
            {
	            case FilterType::AIDenoise:
	            {
	                RPR_ERROR_CHECK(rprContextResolveFrameBuffer(m_context, m_colorBuffer, m_colorFilterBuffer, true), "Failed to resolve filter buffer");
	                RPR_ERROR_CHECK(rprContextResolveFrameBuffer(m_context, m_depthBuffer, m_depthFilterBuffer, true), "Failed to resolve filter buffer");
	                RPR_ERROR_CHECK(rprContextResolveFrameBuffer(m_context, m_normalBuffer, m_normalFilterBuffer, true), "Failed to resolve filter buffer");
	                RPR_ERROR_CHECK(rprContextResolveFrameBuffer(m_context, m_albedoBuffer, m_albedoFilterBuffer, true), "Failed to resolve filter buffer");
	                break;
	            }
	            case FilterType::EawDenoise:
	            {
	                RPR_ERROR_CHECK(rprContextResolveFrameBuffer(m_context, m_colorBuffer, m_colorFilterBuffer, true), "Failed to resolve filter buffer");
	                RPR_ERROR_CHECK(rprContextResolveFrameBuffer(m_context, m_depthBuffer, m_depthFilterBuffer, true), "Failed to resolve filter buffer");
	                RPR_ERROR_CHECK(rprContextResolveFrameBuffer(m_context, m_normalBuffer, m_normalFilterBuffer, true), "Failed to resolve filter buffer");
	                RPR_ERROR_CHECK(rprContextResolveFrameBuffer(m_context, m_objId, m_transFilterBuffer, true), "Failed to resolve filter buffer");
	                RPR_ERROR_CHECK(rprContextResolveFrameBuffer(m_context, m_positionBuffer, m_positionFilterBuffer, true), "Failed to resolve filter buffer");
	                break;
	            }
                default:
                    break;
            }

            m_isRenderedWithFilter = true;
			m_imageFilterPtr->Run();
		}
		else
#endif // USE_RIF
		if (m_currentPlugin == HdRprPluginType::TAHOE)
		{
			RPR_ERROR_CHECK(rprContextResolveFrameBuffer(m_context, targetFB, m_resolvedBuffer, false), "Fail to resolve framebuffer");
		}
	}

	void DeleteFramebuffers()
	{
		SAFE_DELETE_RPR_OBJECT(m_colorBuffer);
		SAFE_DELETE_RPR_OBJECT(m_positionBuffer);
		SAFE_DELETE_RPR_OBJECT(m_depthBuffer);
		SAFE_DELETE_RPR_OBJECT(m_objId);
		SAFE_DELETE_RPR_OBJECT(m_uv);
		SAFE_DELETE_RPR_OBJECT(m_normalBuffer);
		SAFE_DELETE_RPR_OBJECT(m_albedoBuffer);
		SAFE_DELETE_RPR_OBJECT(m_resolvedBuffer);

		if (m_useGlInterop) {
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
		}
	}

	void DeleteRprObject(void * object)
	{
		SAFE_DELETE_RPR_OBJECT(object);
	}

	void DeleteMesh(void * mesh)
	{
	    if (!mesh) {
	        return;
	    }

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
	
	bool IsGlInteropUsed() const {
		return m_useGlInterop;
	}

private:
	void InitRpr()
	{
		//lock();

		const std::string rprSdkPath = GetRprSdkPath();
		auto registerPlugin = [this, &rprSdkPath](HdRprPluginType plugin) -> rpr_int {
			m_currentPlugin = plugin;
			int pluginIdx = static_cast<int>(m_currentPlugin);
			int numPlugins = sizeof(k_PluginLibNames) / sizeof(k_PluginLibNames[0]);
			if (pluginIdx < 0 || pluginIdx >= numPlugins) {
				TF_RUNTIME_ERROR("Invalid plugin requested: index out of bounds - %d", pluginIdx);
				return -1;
			}

			const char* pluginName = k_PluginLibNames[pluginIdx];
			const std::string pluginPath = (rprSdkPath.empty()) ? pluginName : rprSdkPath + "/" + pluginName;
			return rprRegisterPlugin(pluginPath.c_str());
		};

		auto requestedPlugin = HdRprPreferences::GetInstance().GetPlugin();
		rpr_int pluginID = registerPlugin(requestedPlugin);
		if (pluginID == -1)
		{
			TF_WARN("Failed to register the requested one renderer plugin. Trying to register first working one");
			for (auto plugin = HdRprPluginType::FIRST; plugin != HdRprPluginType::LAST; plugin = HdRprPluginType(int(plugin) + 1))
			{
				if (plugin == requestedPlugin)
					continue;
				pluginID = registerPlugin(plugin);
				if (pluginID != -1)
				{
					HdRprPreferences::GetInstance().SetPlugin(plugin);
					break;
				}
			}
		}
		if (pluginID == -1)
		{
			TF_CODING_ERROR("Could not register any of known renderer plugins");
			return;
		}

		// TODO: Query info from HdRprPreferences
		m_useGlInterop = HdRprApiImpl::EnableGLInterop();
		m_currentRenderDevice = HdRprPreferences::GetInstance().GetRenderDevice();
        if (m_useGlInterop && (m_currentRenderDevice == HdRprRenderDevice::CPU ||
                               m_currentPlugin == HdRprPluginType::HYBRID))
		{
			m_useGlInterop = false;
		}
		if (m_useGlInterop)
		{
			GLenum err = glewInit();
			if (err != GLEW_OK)
			{
				TF_WARN("Failed to init GLEW. Error code: %s. Disabling GL interop", glewGetErrorString(err));
				m_useGlInterop = false;
			}
		}

		auto cachePath = HdRprApi::GetTmpDir();
		rpr_creation_flags flags;
        if (m_currentPlugin == HdRprPluginType::HYBRID)
		{
			// Call to getRprCreationFlags is broken in case of hybrid:
			//   1) getRprCreationFlags uses 'rprContextGetInfo' to query device compatibility,
			//        but hybrid plugin does not support such call
			//   2) Hybrid is working only on GPU
			//   3) MultiGPU can be enabled only through vulkan interop
			flags = RPR_CREATION_FLAGS_ENABLE_GPU0;
		}
		else
		{
			flags = getRprCreationFlags(m_currentRenderDevice, pluginID, cachePath);
			if (!flags)
			{
				TF_CODING_ERROR("Could not find compatible device");
				return;
			}
		}
		if (m_useGlInterop)
		{
			flags |= RPR_CREATION_FLAGS_ENABLE_GL_INTEROP;
		}
		if (RPR_ERROR_CHECK(rprCreateContext(RPR_API_VERSION, &pluginID, 1, flags, nullptr, cachePath, &m_context), std::string("Fail to create context with plugin ") + k_PluginLibNames[int(m_currentPlugin)])) return;

		if(RPR_ERROR_CHECK(rprContextSetActivePlugin(m_context, pluginID), "fail to set active plugin")) return;

		RPR_ERROR_CHECK(rprContextSetParameter1u(m_context, "yflip", 0), "Fail to set context YFLIP parameter");
		if (m_currentPlugin == HdRprPluginType::HYBRID)
		{
			RPR_ERROR_CHECK(rprContextSetParameter1u(m_context, "render_quality", int(HdRprPreferences::GetInstance().GetHybridQuality())), "Fail to set context hybrid render quality");
		}
	}

	void InitMaterialSystem()
	{
		if (!m_context)
		{
			return;
		}

		if(RPR_ERROR_CHECK(rprContextCreateMaterialSystem(m_context, 0, &m_matsys), "Fail create Material System resolve")) return;
        m_rprObjectsToRelease.push_back(m_matsys);
		m_rprMaterialFactory.reset(new RprMaterialFactory(m_matsys, m_context));
	}


#ifdef USE_RIF
    void CreateImageFilter()
    {
        // XXX: RPR Hybrid context does not support filters. Discuss with Hybrid team possible workarounds
        if (m_currentPlugin == HdRprPluginType::HYBRID) {
            return;
        }

        if (!HdRprPreferences::GetInstance().IsDenoisingEnabled())
        {
            m_imageFilterPtr.reset();
            return;
        }

        m_imageFilterPtr.reset(new ImageFilter(m_context, m_framebufferDesc.fb_width, m_framebufferDesc.fb_height));
#ifdef __APPLE__
        m_imageFilterType = FilterType::EawDenoise;
#else
        if (m_currentRenderDevice == HdRprRenderDevice::CPU)
        {
            m_imageFilterType = FilterType::EawDenoise;
        }
        else
        {
            m_imageFilterType = FilterType::AIDenoise;
        }
#endif // __APPLE__
        m_imageFilterPtr->CreateFilter(m_imageFilterType);

        rpr_framebuffer_format fmt = { 4, RPR_COMPONENT_TYPE_FLOAT32 };
        switch (m_imageFilterType)
        {
            case FilterType::AIDenoise:
            {
                if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_colorFilterBuffer), "Fail create color framebuffer")) throw std::runtime_error("Fail create color framebuffer");
                if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_normalFilterBuffer), "Fail create color framebuffer")) throw std::runtime_error("Fail create color framebuffer");
                if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_albedoFilterBuffer), "Fail create color framebuffer")) throw std::runtime_error("Fail create color framebuffer");
                if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_depthFilterBuffer), "Fail create color framebuffer")) throw std::runtime_error("Fail create color framebuffer");
                
                m_imageFilterPtr->SetInput(RifFilterInput::RifColor, m_colorFilterBuffer, 1.0f);
                m_imageFilterPtr->SetInput(RifFilterInput::RifNormal, m_normalFilterBuffer, 1.0f);
                m_imageFilterPtr->SetInput(RifFilterInput::RifDepth, m_depthFilterBuffer, 1.0f);
                m_imageFilterPtr->SetInput(RifFilterInput::RifAlbedo, m_albedoFilterBuffer, 1.0f);
                break;
            }
            case FilterType::EawDenoise:
            {
                RifParam rifParam;
                rifParam.mData.f = 1.f;
                rifParam.mType = RifParamType::RifFloat;
                m_imageFilterPtr->AddParam("colorSigma", rifParam);
                m_imageFilterPtr->AddParam("normalSigma", rifParam);
                m_imageFilterPtr->AddParam("depthSigma", rifParam);
                m_imageFilterPtr->AddParam("transSigma", rifParam);

                if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_colorFilterBuffer), "Fail create color framebuffer")) throw std::runtime_error("Fail create color framebuffer");
                if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_normalFilterBuffer), "Fail create color framebuffer")) throw std::runtime_error("Fail create color framebuffer");
                if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_depthFilterBuffer), "Fail create color framebuffer")) throw std::runtime_error("Fail create color framebuffer");
                if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_transFilterBuffer), "Fail create color framebuffer")) throw std::runtime_error("Fail create color framebuffer");
                if (RPR_ERROR_CHECK(rprContextCreateFrameBuffer(m_context, fmt, &m_framebufferDesc, &m_positionFilterBuffer), "Fail create color framebuffer")) throw std::runtime_error("Fail create color framebuffer");

                m_imageFilterPtr->SetInput(RifFilterInput::RifColor, m_colorFilterBuffer, 1.0f);
                m_imageFilterPtr->SetInput(RifFilterInput::RifNormal, m_normalFilterBuffer, 1.0f);
                m_imageFilterPtr->SetInput(RifFilterInput::RifDepth, m_depthFilterBuffer, 1.0f);
                m_imageFilterPtr->SetInput(RifFilterInput::RifTrans, m_transFilterBuffer, 1.0f);
                m_imageFilterPtr->SetInput(RifFilterInput::RifObjectId, m_transFilterBuffer, 1.0f);
                m_imageFilterPtr->SetInput(RifFilterInput::RifWorldCoordinate, m_positionFilterBuffer, 1.0f);
                break;
            }
            default:
                break;
        }
        
        m_imageFilterPtr->Resize(m_framebufferDesc.fb_width, m_framebufferDesc.fb_height);

		if (m_useGlInterop) {
			m_imageFilterPtr->SetOutputGlTexture(m_textureFramebufferGL);
		} else {
			m_imageFilterPtr->SetOutput(m_resolvedBuffer);
		}
		m_imageFilterPtr->AttachFilter();
	}

	void DeleteImageFilter()
	{
        SAFE_DELETE_RPR_OBJECT(m_colorFilterBuffer);
        SAFE_DELETE_RPR_OBJECT(m_normalFilterBuffer);
        SAFE_DELETE_RPR_OBJECT(m_depthFilterBuffer);
        SAFE_DELETE_RPR_OBJECT(m_albedoFilterBuffer);
        SAFE_DELETE_RPR_OBJECT(m_transFilterBuffer);
        SAFE_DELETE_RPR_OBJECT(m_positionFilterBuffer);
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

	void SplitPolygons(const VtIntArray & indexes, const VtIntArray & vpf, VtIntArray & out_newIndexes)
	{
		out_newIndexes.clear();
		out_newIndexes.reserve(indexes.size());

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
			}
			else
			{
				for (int i = 0; i < vCount - 2; ++i)
				{
					out_newIndexes.push_back(*(idxIt + i + 0));
					out_newIndexes.push_back(*(idxIt + i + 1));
					out_newIndexes.push_back(*(idxIt + i + 2));
				}
				idxIt += vCount;
			}
		}
	}

	const rpr_framebuffer GetTargetFB() const
    {
		switch (HdRprPreferences::GetInstance().GetAov())
        {
            case HdRprAov::COLOR: return m_colorBuffer;
            case HdRprAov::NORMAL: return m_normalBuffer;
            case HdRprAov::ALBEDO: return m_albedoBuffer;
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

		return CreateMesh(position, indexes, normals, VtIntArray(), uv, VtIntArray(), vpf);
	}

	void lock() {
		while(m_lock.test_and_set(std::memory_order_acquire));
	}

	void unlock() {
		m_lock.clear(std::memory_order_release);
	}
	
	static bool EnableGLInterop() {
		// TODO: consider putting a selection on GUI settings
#ifdef USE_GL_INTEROP
		return true;
#else
		return false;
#endif
	}

	rpr_context m_context = nullptr;
	rpr_scene m_scene = nullptr;
	rpr_camera m_camera = nullptr;

	rpr_framebuffer m_colorBuffer = nullptr;
	rpr_framebuffer m_positionBuffer = nullptr;
    rpr_framebuffer m_depthBuffer = nullptr;
    rpr_framebuffer m_normalBuffer = nullptr;
    rpr_framebuffer m_albedoBuffer = nullptr;
    rpr_framebuffer m_objId = nullptr;
    rpr_framebuffer m_uv = nullptr;
	rpr_framebuffer m_resolvedBuffer = nullptr;
	rpr_post_effect m_tonemap = nullptr;

    rpr_framebuffer m_colorFilterBuffer = nullptr;
    rpr_framebuffer m_depthFilterBuffer = nullptr;
    rpr_framebuffer m_normalFilterBuffer = nullptr;
    rpr_framebuffer m_albedoFilterBuffer = nullptr;
    rpr_framebuffer m_transFilterBuffer = nullptr;
    rpr_framebuffer m_positionFilterBuffer = nullptr;

	bool m_useGlInterop = EnableGLInterop();
    HdRprRenderDevice m_currentRenderDevice = HdRprRenderDevice::NONE;
	GLuint m_framebufferGL = INVALID_FRAMEBUFFER;
	GLuint m_depthrenderbufferGL;
	rpr_GLuint m_textureFramebufferGL = INVALID_TEXTURE;
	std::vector<float> m_framebufferData;

	rpr_material_system m_matsys = nullptr;

	rpr_framebuffer_desc m_framebufferDesc = {};

	GfMatrix4d m_cameraViewMatrix = GfMatrix4d(1.f);
	GfMatrix4d m_cameraProjectionMatrix = GfMatrix4d(1.f);


	std::unique_ptr<RprMaterialFactory> m_rprMaterialFactory;

	bool m_isLightPresent = false;

	bool m_isFramebufferDirty = true;

	bool m_isRenderedWithFilter = false;

	HdRprPluginType m_currentPlugin = HdRprPluginType::NONE;

    std::vector<RprApiMaterial*> m_materialsToRelease;
    std::vector<void*> m_rprObjectsToRelease;

	// simple spinlock for locking RPR calls
	std::atomic_flag m_lock = ATOMIC_FLAG_INIT;

#ifdef USE_RIF
	std::unique_ptr<ImageFilter> m_imageFilterPtr;
    FilterType m_imageFilterType = FilterType::None;
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

	void HdRprApi::SetRendererPlugin(HdRprPluginType plugin)
	{
		HdRprPreferences::GetInstance().SetPlugin(plugin);
	}

	void HdRprApi::SetHybridQuality(HdRprHybridQuality quality)
	{
		HdRprPreferences::GetInstance().SetHybridQuality(quality);
	}

	void HdRprApi::SetDenoising(bool enableDenoising)
	{
		HdRprPreferences::GetInstance().SetDenoising(enableDenoising);
	}

	bool HdRprApi::IsDenoisingEnabled()
    {
	    return HdRprPreferences::GetInstance().IsDenoisingEnabled();
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

	RprApiObject HdRprApi::CreateMesh(const VtVec3fArray & points, const VtIntArray & pointIndexes, const VtVec3fArray & normals, const VtIntArray & normalIndexes, const VtVec2fArray & uv, const VtIntArray & uvIndexes, const VtIntArray & vpf)
	{
		return m_impl->CreateMesh(points, pointIndexes, normals, normalIndexes, uv, uvIndexes, vpf);
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

	void HdRprApi::DeleteMaterial(RprApiMaterial *rprApiMaterial)
	{
	    m_impl->DeleteMaterial(rprApiMaterial);
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

	const GLuint HdRprApi::GetFramebufferGL() const
	{
		return m_impl->GetFramebufferGL();
	}

	const float * HdRprApi::GetFramebufferData() const
	{
		return m_impl->GetFramebufferData();
	}

	void HdRprApi::DeleteRprApiObject(RprApiObject object)
	{
		m_impl->DeleteRprObject(object);
	}

	void HdRprApi::DeleteMesh(RprApiObject mesh)
	{
		m_impl->DeleteMesh(mesh);
	}

	bool HdRprApi::IsGlInteropUsed() const
	{
		return m_impl->IsGlInteropUsed();
	}

	int HdRprApi::GetPluginType()
	{
		return int(HdRprPreferences::GetInstance().GetPlugin());
	}


PXR_NAMESPACE_CLOSE_SCOPE
