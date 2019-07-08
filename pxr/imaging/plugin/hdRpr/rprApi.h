#ifndef HDRPR_RPR_API_H
#define HDRPR_RPR_API_H

#include "pxr/pxr.h"
#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/matrix4f.h"

#include <memory>

#include <GL/glew.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApiImpl;
class RprMaterialObject;
struct MaterialNode;

class RprApiMaterial;
class MaterialAdapter;

typedef void * RprApiObject;

enum class HdRprRenderDevice
{
	NONE = -1,
	CPU = 0,
	GPU,
	FIRST = CPU,
	LAST = GPU
};

enum class HdRprAov
{
    NONE = -1,
    COLOR = 0,
    NORMAL,
    DEPTH,
    PRIM_ID,
    UV,
	FIRST = COLOR,
	LAST = UV
};

enum class FilterType
{
	None = -1,
	BilateralDenoise,
	LwrDenoise,
	EawDenoise,
	FIRST = BilateralDenoise,
	LAST = EawDenoise
};

class HdRprApi final
{
public:
	HdRprApi();
	~HdRprApi();

	static void SetRenderDevice(const HdRprRenderDevice & renderMode);

	static void SetFilter(const FilterType & type);

	static void SetAov(const HdRprAov & aov);
	
	static const char* GetTmpDir();

	void Init();

	void Deinit();

	RprApiObject CreateMesh(const VtVec3fArray & points, const VtVec3fArray & normals, const VtVec2fArray & uv, const VtIntArray & indexes, const VtIntArray & vpf);

	RprApiObject CreateCurve(const VtVec3fArray & points, const VtIntArray & indexes, const float & width);

	void CreateInstances(RprApiObject prototypeMesh, const VtMatrix4dArray & transforms, VtArray<RprApiObject> & out_instances);

	void CreateEnvironmentLight(const std::string & prthTotexture, float intensity);

	RprApiObject CreateRectLightMesh(const float & width, const float & height);

	RprApiObject CreateSphereLightMesh(const float & radius);

	RprApiObject CreateDiskLight(const float & width, const float & height, const GfVec3f & color);

	void CreateVolume(const VtArray<float> & gridDencityData, const VtArray<size_t> & indexesDencity, const VtArray<float> & gridAlbedoData, const VtArray<unsigned int> & indexesAlbedo, const GfVec3i & grigSize, const GfVec3f & voxelSize, RprApiObject out_mesh, RprApiObject out_heteroVolume);

	RprApiMaterial * CreateMaterial(MaterialAdapter & materialAdapter);

	void SetMeshTransform(RprApiObject mesh, const GfMatrix4d & transform);

	void SetMeshRefineLevel(RprApiObject mesh, int level, TfToken boundaryInterpolation);

	void SetMeshMaterial(RprApiObject mesh, const RprApiMaterial * material);

	void SetCurveMaterial(RprApiObject curve, const RprApiMaterial * material);

	void ClearFramebuffer();

	const GfMatrix4d & GetCameraViewMatrix() const;

	const GfMatrix4d & GetCameraProjectionMatrix() const;

	void SetCameraViewMatrix(const GfMatrix4d & m );

	void SetCameraProjectionMatrix(const GfMatrix4d & m);

	void Resize(const GfVec2i & resolution);

	void Render();
    
	void GetFramebufferSize(GfVec2i & resolution) const;

	const GLuint GetFramebufferGL() const;

	const float * GetFramebufferData() const;

	void DeleteRprApiObject(RprApiObject object);

	void DeleteMesh(RprApiObject mesh);

	bool IsGlInteropUsed() const;

private:
	HdRprApiImpl * m_impl = nullptr;
};

typedef std::shared_ptr<HdRprApi> HdRprApiSharedPtr;

typedef std::weak_ptr<HdRprApi> HdRprApiWeakPtr;

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_H
