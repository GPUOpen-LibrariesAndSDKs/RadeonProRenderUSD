#include "rprApi.h"

#include "RprSupport.h"
#include "RadeonProRender.h"
#include "RadeonProRender_GL.h"

#include "material.h"
#include "materialFactory.h"
#include "materialAdapter.h"

#include <GL/glew.h>
#include <vector>

#include "pxr/imaging/pxOsd/tokens.h"

#define USE_INTEROP

// we lock()/unlock() around rpr calls that might be called multithreaded.
#define SAFE_DELETE_RPR_OBJECT(x) if(x) {lock(); rprObjectDelete( x ); x = nullptr; unlock();}
#define INVALID_TEXTURE -1

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
#ifdef WIN32
	const char* k_TahoeLibName = "Tahoe64.dll";
#elif defined __linux__
	const char* k_TahoeLibName = "libTahoe64.so";
#elif defined __APPLE__
	const char* k_TahoeLibName = "libTahoe64.dylib";
#endif

	constexpr const rpr_uint k_defaultFbWidth = 800;
	constexpr const rpr_uint k_defaultFbHeight = 600;

	const GfVec3f k_defaultLightColor(0.5f, 0.5f, 0.5f);

	const uint32_t k_diskVertexCount = 32;

	// TODO: change *.dat file location
	constexpr const char * k_pathToRprPreference = "rprPreferences.dat";
}


const rpr_render_mode GetRprRenderMode(const HdRprRenderMode & hdRprRenderMode) 
{	
	switch (hdRprRenderMode)
	{
	case HdRprRenderMode::GLOBAL_ILLUMINATION : return RPR_RENDER_MODE_GLOBAL_ILLUMINATION;
	case HdRprRenderMode::DIRECT_ILLUMINATION : return RPR_RENDER_MODE_DIRECT_ILLUMINATION;
	case HdRprRenderMode::DIRECT_ILLUMINATION_NO_SHADOW: return RPR_RENDER_MODE_DIRECT_ILLUMINATION_NO_SHADOW;
	case HdRprRenderMode::WIREFRAME: return RPR_RENDER_MODE_WIREFRAME;
	case HdRprRenderMode::MATERIAL_INDEX: return RPR_RENDER_MODE_MATERIAL_INDEX;
	case HdRprRenderMode::POSITION: return RPR_RENDER_MODE_POSITION;
	case HdRprRenderMode::NORMAL: return RPR_RENDER_MODE_NORMAL;
	case HdRprRenderMode::TEXCOORD: return RPR_RENDER_MODE_TEXCOORD;
	case HdRprRenderMode::AMBIENT_OCCLUSION: return RPR_RENDER_MODE_AMBIENT_OCCLUSION;
	case HdRprRenderMode::DIFFUSE : return RPR_RENDER_MODE_DIFFUSE;
	default: return RPR_ERROR_UNSUPPORTED;
	}
}


const rpr_creation_flags GetRprCreationFlags(const HdRprRenderDevice renderDevice)
{
	rpr_creation_flags flags = 0x0;

#ifdef  USE_INTEROP
	flags |= RPR_CREATION_FLAGS_ENABLE_GL_INTEROP;
#endif


	switch (renderDevice)
	{
	case HdRprRenderDevice::CPU:
	{
#ifdef  USE_INTEROP
		TF_CODING_WARNING("Do not support GL Interop with CPU device. Switched to GPU.");
		flags |= RPR_CREATION_FLAGS_ENABLE_GPU0;
#elif
		flags |= RPR_CREATION_FLAGS_ENABLE_CPU;
#endif
	}
	break;

	case HdRprRenderDevice::GPU0:
		flags |= RPR_CREATION_FLAGS_ENABLE_GPU0;
		break;

	default: return RPR_ERROR_UNSUPPORTED;
	}

	return flags;
}


class HdRprPreferences
{
public:
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

	void SetRenderMode(const HdRprRenderMode & renderMode)
	{
		m_renderMode = renderMode;
		Save();
		SetDitry(true);
	}

	HdRprRenderMode GetRenderMode() const
	{
		return m_renderMode;
	}

	void SetRenderDevice(const HdRprRenderDevice & renderDevice)
	{
		m_renderDevice = renderDevice;
		Save();
		SetDitry(true);
	}

	HdRprRenderDevice GetRenderDevice() const
	{
		return m_renderDevice;
	}

	bool IsDirty() const
	{
		return m_isDirty;
	}

	void SetDitry(bool isDirty)
	{
		m_isDirty = isDirty;
	}

private:
	bool Load()
	{
		if (FILE * f = fopen(k_pathToRprPreference, "rb"))
		{
			if (!fread(this, sizeof(this), 1, f))
			{
				TF_CODING_ERROR("Fail to read rpr preferences dat file");
			}
			fclose(f);
			return true;
		}

		return false;
	}

	void Save()
	{
		if (FILE * f = fopen(k_pathToRprPreference, "wb"))
		{
			if (!fwrite(this, sizeof(this), 1, f))
			{
				TF_CODING_ERROR("Fail to write rpr preferences dat file");
			}
			fclose(f);
		}
	}

	void SetDefault()
	{
		m_renderDevice = HdRprRenderDevice::CPU;
		m_renderMode = HdRprRenderMode::GLOBAL_ILLUMINATION;
	}

	HdRprRenderDevice m_renderDevice = HdRprRenderDevice::NONE;
	HdRprRenderMode m_renderMode = HdRprRenderMode::NONE;

	bool m_isDirty = true;

};


class HdRprApiImpl
{
public:
	static HdRprPreferences & GetPreferences()
	{
		return s_preferences;
	}

	void Init()
	{
		this->InitRpr();
		this->InitMaterialSystem();
		this->CreateScene();
	}

	void Deinit()
	{
		DeleteFramebuffers();

		SAFE_DELETE_RPR_OBJECT(scene);
		SAFE_DELETE_RPR_OBJECT(camera);
		SAFE_DELETE_RPR_OBJECT(tonemap);
		SAFE_DELETE_RPR_OBJECT(matsys);
	}

	void CreateScene() {
		//lock();
		rprContextCreateScene(context, &scene);
		rprContextSetScene(context, scene);
		//unlock();
	}

	void CreateCamera() {
		//lock();
		rprContextCreateCamera(context, &camera);
		rprCameraLookAt(camera, 20.0f, 60.0f, 40.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
		rprSceneSetCamera(scene, camera);
		//unlock();
	}

	void * CreateMesh(const VtVec3fArray & points, const VtVec3fArray & normals, const VtVec2fArray & uv, const VtIntArray & indexes, const VtIntArray & vpf, rpr_material_node material = nullptr)
	{
		rpr_int status = RPR_SUCCESS;
		rpr_shape mesh = nullptr;

		VtIntArray newIndexes, newVpf;
		SplitPolygons(indexes, vpf, newIndexes, newVpf);
		
		lock();
		status = rprContextCreateMesh(context,
			(rpr_float const*)points.data(), points.size(), sizeof(GfVec3f),
			(rpr_float const*)((normals.size() == 0) ? 0 : normals.data()), normals.size(), sizeof(GfVec3f),
			(rpr_float const*)((uv.size() == 0) ? 0 : uv.data()), uv.size(), sizeof(GfVec2f),
			(rpr_int const*)newIndexes.data(), sizeof(rpr_int),
			(rpr_int const*)newIndexes.data(), sizeof(rpr_int),
			(rpr_int const*)newIndexes.data(), sizeof(rpr_int),
			newVpf.data(), newVpf.size(), &mesh);
		unlock();

		if (status != RPR_SUCCESS) {
			TF_CODING_ERROR("Fail create mesh. Error code: %d", status);
			return nullptr;
		}

		status = rprSceneAttachShape(scene, mesh);
		if (status != RPR_SUCCESS) {
			TF_CODING_ERROR("Fail attach mesh. Error code: %d", status);
			return nullptr;
		}


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
		rpr_int status;
		lock();
		status = rprShapeSetTransform(mesh, false, transform.GetArray());
		unlock();
		if (status != RPR_SUCCESS) {
			TF_CODING_ERROR("Fail set mesh transformation. Error code: %d", status);
		}
	}

	void SetMeshRefineLevel(rpr_shape mesh, const int level, const TfToken boundaryInterpolation)
	{
		rpr_int status;
		lock();
		status = rprShapeSetSubdivisionFactor(mesh, level);
		unlock();
		if (status != RPR_SUCCESS) {
			TF_CODING_ERROR("Fail set mesh subdividion. Error code: %d", status);
		}

		if (level > 0) {
			rpr_subdiv_boundary_interfop_type interfopType = RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_AND_CORNER 
				? boundaryInterpolation == PxOsdOpenSubdivTokens->edgeAndCorner :
				RPR_SUBDIV_BOUNDARY_INTERFOP_TYPE_EDGE_ONLY;
			status = rprShapeSetSubdivisionBoundaryInterop(mesh, interfopType);
			if (status != RPR_SUCCESS) {
				TF_CODING_ERROR("Fail set mesh subdividion boundary. Error code: %d", status);
			}
		}
	}

	void SetMeshMaterial(rpr_shape mesh, const RprApiMaterial * material)
	{
		MaterialFactory * materialFactory = GetMaterialFactory(material->GetType());
		// all rprmaterial calls in here
		lock();
		materialFactory->AttachMaterialToShape(mesh, material);
		unlock();
	}

	void * CreateMeshInstance(rpr_shape mesh)
	{
		rpr_int status;
		rpr_shape meshInstance;
		lock();
		status = rprContextCreateInstance(context, mesh, &meshInstance);
		
		if (status != RPR_SUCCESS) {
			TF_CODING_ERROR("Fail to create mesh instance. Error code: %d", status);
			return nullptr;
		}

		status = rprSceneAttachShape(scene, meshInstance);
		unlock();
				
		if (status != RPR_SUCCESS) {
			TF_CODING_ERROR("Fail attach mesh instance. Error code: %d", status);
			return nullptr;
		}

		return meshInstance;
	}

	void SetMeshVisibility(rpr_shape mesh, bool isVisible)
	{
		//lock();
		rprShapeSetVisibility(mesh, isVisible);
		//unlock();
	}

	void CreateEnvironmentLight(const std::string & path, float intensity)
	{	
		rpr_int status;
		rpr_image image = nullptr;
		//lock();
		status = rprContextCreateImageFromFile(context, path.c_str(), &image);
		
		if (status != RPR_SUCCESS) {
			TF_CODING_ERROR("Fail to load image %s\n Error code: %d", path.c_str(), status);
		}
		
		// Add an environment light to the scene with the image attached.
		rpr_light light;
		status = rprContextCreateEnvironmentLight(context, &light);
		status = rprEnvironmentLightSetImage(light, image);
		status = rprEnvironmentLightSetIntensityScale(light, intensity);
		status = rprSceneAttachLight(scene, light);
		//unlock();
		isLightPresent = true;
	}

	void CreateEnvironmentLight(GfVec3f color, float intensity)
	{
		rpr_int status;
		rpr_image image = nullptr;

		// Add an environment light to the scene with the image attached.
		rpr_light light;

		// Set the background image to a solid color.
		std::array<float, 3> backgroundColor = { color[0],  color[1],  color[2]};
		rpr_image_format format = { 3, RPR_COMPONENT_TYPE_FLOAT32 };
		rpr_image_desc desc = { 1, 1, 1, 3, 3 };
		//lock();
		
		status = rprContextCreateImage(context, format, &desc, backgroundColor.data(), &image);
		if (status != RPR_SUCCESS)
			TF_CODING_ERROR("Fail to create image from color.  Error code: %d", status);

		status = rprContextCreateEnvironmentLight(context, &light);
		status = rprEnvironmentLightSetImage(light, image);
		status = rprEnvironmentLightSetIntensityScale(light, intensity);
		status = rprSceneAttachLight(scene, light);
		//unlock();
		
		isLightPresent = true;
	}

	void * CreateRectLightGeometry(const float & width, const float & height)
	{
		constexpr float rectVertexCount = 4;
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

		isLightPresent = true;

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

		rpr_int status;
		rpr_material_node material = NULL;
		//lock();
		
		status = rprMaterialSystemCreateNode(matsys, RPR_MATERIAL_NODE_EMISSIVE, &material);
		if (status != RPR_SUCCESS) {
			TF_CODING_ERROR("Fail create emmisive material. Error code: %d", status);
			return nullptr;
		}

		status = rprMaterialNodeSetInputF(material, "color", color[0], color[1], color[2], 0.0f);
		//unlock();
		
		if (status != RPR_SUCCESS) {
			TF_CODING_ERROR("Fail set material color. Error code: %d", status);
			return nullptr;
		}

		isLightPresent = true;
		
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

		const float d = 0.5f*radius;

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

		isLightPresent = true;

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

	void CreatePosteffects()
	{
		//lock();
		
		rprContextCreatePostEffect(context, RPR_POST_EFFECT_TONE_MAP, &tonemap);
		rprContextAttachPostEffect(context, tonemap);
		
		//unlock();
	}

	void CreateFramebuffer(const rpr_uint width, const rpr_uint height)
	{		
		framebuffer_desc.fb_width = width; 
		framebuffer_desc.fb_height = height;
		
		rpr_int status;
		
		
		rpr_framebuffer_format fmt = { 4, RPR_COMPONENT_TYPE_FLOAT32 };

		//lock();
		
		status = rprContextCreateFrameBuffer(context, fmt, &framebuffer_desc, &color_buffer);
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail create framebuffer. Error code %d", status);
		}

		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail create framebuffer. Error code %d", status);
		}

#ifdef USE_INTEROP

		glGenFramebuffers(1, &framebufferGL);
		glBindFramebuffer(GL_FRAMEBUFFER, framebufferGL);



		// Allocate an OpenGL texture.
		glGenTextures(1, &texture2D);
		glBindTexture(GL_TEXTURE_2D, texture2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);

		GLuint depthrenderbuffer;
		glGenRenderbuffers(1, &depthrenderbuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, depthrenderbuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthrenderbuffer);
		
		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture2D, 0);

		GLenum glFbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if ( glFbStatus != GL_FRAMEBUFFER_COMPLETE)
		{
			TF_CODING_ERROR("Fail create GL framebuffer. Error code %d", glFbStatus);
			ClearFramebuffers();
			return;
		}

		status = rprContextCreateFramebufferFromGLTexture2D(context, GL_TEXTURE_2D, 0, texture2D, &resolved_buffer);
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
		status = rprContextCreateFrameBuffer(context, fmt, &framebuffer_desc, &resolved_buffer);
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail create framebuffer. Error code %d", status);
			return;
		}
#endif
		//unlock();
		
		ClearFramebuffers();

		//lock();
		rprContextSetAOV(context, RPR_AOV_COLOR, color_buffer);
		//unlock();
		bufferData.resize(framebuffer_desc.fb_width * framebuffer_desc.fb_height * 4, 0.f);
	}


	void SetFramebufferDirty(bool isDirty)
	{
		isFramebufferDirty = isDirty;
	}

	void ClearFramebuffers()
	{
		//lock();
		rprFrameBufferClear(color_buffer);
		rprFrameBufferClear(resolved_buffer);
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
		rprCameraLookAt(camera, eye[0], eye[1], eye[2], at[0], at[1], at[2], up[0], up[1], up[2]);
		//unlock();

		m_cameraViewMatrix = m;
	}

	void SetCameraProjectionMatrix(const GfMatrix4d & proj)
	{
		float sensorSize[2];

		rpr_int status;
		//lock();
		status = rprCameraGetInfo(camera, RPR_CAMERA_SENSOR_SIZE, sizeof(sensorSize), &sensorSize, NULL);
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail to get camera swnsor size parameter. Error code %d", status);
		}

		const float focalLength = sensorSize[1] * proj[1][1] / 2;
		status = rprCameraSetFocalLength(camera, focalLength);
		
		//unlock();
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail to set focal length parameter. Error code %d", status);
		}

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
		return framebufferGL;
	}

	const float * GetFramebufferData()
	{
		rpr_int status;

#ifdef USE_INTEROP
		glBindTexture(GL_TEXTURE_2D, texture2D);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, bufferData.data());
		glBindTexture(GL_TEXTURE_2D, 0);

		if (GLenum errorCode = glGetError())
		{
			TF_CODING_ERROR("Fail get texture pixels. Error code %d", errorCode);
		}
#else
		size_t fb_data_size = 0;
		//lock();
		
		status = rprFrameBufferGetInfo(resolved_buffer, RPR_FRAMEBUFFER_DATA, 0, NULL, &fb_data_size);
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail to get frafebuffer data size. Error code %d", status);
		}

		status = rprFrameBufferGetInfo(resolved_buffer, RPR_FRAMEBUFFER_DATA, fb_data_size, bufferData.data(), NULL);
		
		//unlock();
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail to get frafebuffer data. Error code %d", status);
		}
#endif

		return bufferData.data();
	}

	void GetFramebufferSize(rpr_int & width, rpr_int & height) const
	{
		width = framebuffer_desc.fb_width;
		height = framebuffer_desc.fb_height;
	}

	void ResizeFramebuffer(const GfVec2i & resolution) {
		DeleteFramebuffers();
		CreateFramebuffer(resolution[0], resolution[1]);
	}

	void Render()
	{
		rpr_int status;

		if (isFramebufferDirty)
		{
			ClearFramebuffers();
			SetFramebufferDirty(false);
		}

		// In case there is no Lights in scene - create dafault
		if (!isLightPresent)
		{
			CreateEnvironmentLight(k_defaultLightColor, 1.f);
		}

		status = rprContextRender(context);
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail contex render framebuffer. Error code %d", status);
		}

		const HdRprRenderMode renderMode = s_preferences.GetRenderMode();

		if (s_preferences.IsDirty())
		{
			
			//lock();
			status = rprContextSetParameter1u(context, "rendermode", GetRprRenderMode(renderMode));
			//unlock();
			
			if (status != RPR_SUCCESS)
			{
				TF_CODING_ERROR("Fail set rendermode. Error code %d", status);
			}

			s_preferences.SetDitry(false);
			
			SetFramebufferDirty(true);
		}

		//lock();
		status = rprContextResolveFrameBuffer(context, color_buffer, resolved_buffer, false);
		//unlock();
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail contex resolve. Error code %d", status);
		}
	}

	void DeleteFramebuffers()
	{
		SAFE_DELETE_RPR_OBJECT(color_buffer);
		SAFE_DELETE_RPR_OBJECT(resolved_buffer);
		
		if (framebufferGL != GL_INVALID_VALUE)
		{
			glDeleteFramebuffers(1, &framebufferGL);
			framebufferGL = INVALID_TEXTURE;
		}

		if (texture2D != INVALID_TEXTURE)
		{
			glDeleteTextures(1, &texture2D);
			texture2D = INVALID_TEXTURE;
		}
	}

	void DeleteRprObject(void * object)
	{
		SAFE_DELETE_RPR_OBJECT(object);
	}

	void DeleteMesh(void * mesh)
	{
		rpr_int status;
		
		lock();
		
		status = rprShapeSetMaterial(mesh, NULL);
		
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail reset mesh material. Error code %d", status);
		}
		status = rprSceneDetachShape(scene, mesh);
		unlock();
		
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail detach mesh from scene. Error code %d", status);
		}

		SAFE_DELETE_RPR_OBJECT(mesh);
	}

private:
	void InitRpr()
	{		
		//lock();
		
		rpr_int tahoePluginID = rprRegisterPlugin(k_TahoeLibName);
		rpr_int plugins[] = { tahoePluginID };

		const HdRprRenderDevice renderDevice = s_preferences.GetRenderDevice();
		rpr_int status = rprCreateContext(RPR_API_VERSION, plugins, 1, GetRprCreationFlags(renderDevice), NULL, NULL, &context);
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail Load %s. Error code %d", k_TahoeLibName, status);
		}
		
		if (GLenum errorCode = glGetError())
		{
			TF_CODING_ERROR("Fail get texture pixels. Error code %d", errorCode);
		}

		status = rprContextSetActivePlugin(context, plugins[0]);

		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail Set plugin %s resolve. Error code %d", k_TahoeLibName, status);
		}
		
		rprContextSetParameter1u(context, "yflip", 0);
		//unlock();
		
	}

	void InitMaterialSystem()
	{
		rpr_int status;
		//lock();
		
		status = rprContextCreateMaterialSystem(context, 0, &matsys);
		//unlock();
	
		if (status != RPR_SUCCESS)
		{
			TF_CODING_ERROR("Fail create Material System resolve. Error code %d", status);
			return;
		}

		m_rprMaterialFactory.reset(new RprMaterialFactory(matsys));
		m_rprxMaterialFactory.reset(new RprXMaterialFactory(matsys, context));
	}

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
		case EMaterialType::COLOR:
		case EMaterialType::USD_PREVIEW_SURFACE:
			return m_rprxMaterialFactory.get();

		case EMaterialType::EMISSIVE:
			return m_rprMaterialFactory.get();
		}
		
		return nullptr;
	}

	void lock() {
		while(m_lock.test_and_set(std::memory_order_acquire));
	}

	void unlock() {
		m_lock.clear(std::memory_order_release);
	}

	static HdRprPreferences s_preferences;

	rpr_context context = nullptr;
	rpr_scene scene = nullptr;
	rpr_camera camera = nullptr;

	rpr_framebuffer color_buffer = nullptr;
	rpr_framebuffer resolved_buffer = nullptr;
	rpr_post_effect tonemap = nullptr;

	GLuint framebufferGL = GL_INVALID_VALUE;
	rpr_GLuint texture2D = INVALID_TEXTURE;

	rpr_material_system matsys = nullptr;

	rpr_framebuffer_desc framebuffer_desc = {};

	std::vector<float> bufferData;

	GfMatrix4d m_cameraViewMatrix = GfMatrix4d(1.f);
	GfMatrix4d m_cameraProjectionMatrix = GfMatrix4d(1.f);


	std::unique_ptr<RprMaterialFactory> m_rprMaterialFactory;
	std::unique_ptr<RprXMaterialFactory> m_rprxMaterialFactory;

	bool isLightPresent = false;

	bool isFramebufferDirty = true;
	// simple spinlock for locking RPR calls
	std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
};

HdRprPreferences HdRprApiImpl::s_preferences = HdRprPreferences();

	HdRprApi::HdRprApi() : m_impl(new HdRprApiImpl)
	{
	}

	HdRprApi::~HdRprApi()
	{
		delete m_impl;
	}

	void HdRprApi::SetRenderMode(const HdRprRenderMode & renderMode)
	{
		HdRprApiImpl::GetPreferences().SetRenderMode(renderMode);
	}

	void HdRprApi::SetRenderDevice(const HdRprRenderDevice & renderMode)
	{
		HdRprApiImpl::GetPreferences().SetRenderDevice(renderMode);
	}

	void HdRprApi::Init()
	{
		m_impl->Init();
		m_impl->CreateFramebuffer(k_defaultFbWidth, k_defaultFbHeight);
		m_impl->CreatePosteffects();
		m_impl->CreateCamera();
	}

	void HdRprApi::Deinit()
	{
		m_impl->Deinit();
	}

	RprApiObject HdRprApi::CreateMesh(const VtVec3fArray & points, const VtVec3fArray & normals, const VtVec2fArray & uv, const VtIntArray & indexes, const VtIntArray & vpf)
	{
		return m_impl->CreateMesh(points, normals, uv, indexes, vpf);
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

	void HdRprApi::DeleteRprApiObject(RprApiObject object)
	{
		m_impl->DeleteRprObject(object);
	}

	void HdRprApi::DeleteMesh(RprApiObject mesh)
	{
		m_impl->DeleteMesh(mesh);
	}


PXR_NAMESPACE_CLOSE_SCOPE