#ifndef HDRPR_RPR_API_H
#define HDRPR_RPR_API_H

#include "tokens.h"

#include "pxr/pxr.h"
#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/imaging/hd/types.h"

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApiImpl;
class RprMaterialObject;
struct MaterialNode;

struct RprApiMaterial;
class MaterialAdapter;

typedef void* RprApiObject;

enum class HdRprRenderDevice
{
    NONE = -1,
    CPU = 0,
    GPU,
    FIRST = CPU,
    LAST = GPU
};

#define HD_RPR_AOV_TOKENS \
    (color)                                     \
    (albedo)                                    \
    (depth)                                     \
    (linearDepth)                               \
    (primId)                                    \
    (instanceId)                                \
    (elementId)                                 \
    (normal)                                    \
    (worldCoordinate)                           \
    ((primvarsSt, "primvars:st"))

TF_DECLARE_PUBLIC_TOKENS(HdRprAovTokens, HDRPR_API, HD_RPR_AOV_TOKENS);

enum class HdRprAov
{
    NONE = -1,
    COLOR = 0,
    NORMAL,
    ALBEDO,
    DEPTH,
    PRIM_ID,
    UV,
    FIRST = COLOR,
    LAST = UV
};

enum class HdRprHybridQuality
{
	NONE = -1,
	LOW = 0,
	MEDIUM,
	HIGH,
	FIRST = LOW,
	LAST = HIGH
};

enum class HdRprPluginType : int
{
	NONE = -1,
	TAHOE = 0,
	HYBRID,
	FIRST = TAHOE,
	LAST = HYBRID
};

class HdRprApi final
{
public:
    HdRprApi();
    ~HdRprApi();

    void Init();
    void Deinit();

    void CreateEnvironmentLight(const std::string& pathTotexture, float intensity);
    RprApiObject CreateRectLightMesh(const float& width, const float& height);
    RprApiObject CreateSphereLightMesh(const float& radius);
    RprApiObject CreateDiskLight(const float& width, const float& height, const GfVec3f& color);

    void CreateVolume(const VtArray<float>& gridDencityData, const VtArray<size_t>& indexesDencity, const VtArray<float>& gridAlbedoData, const VtArray<unsigned int>& indexesAlbedo, const GfVec3i& grigSize, const GfVec3f& voxelSize, RprApiObject out_mesh, RprApiObject out_heteroVolume);

    RprApiObject CreateMesh(const VtVec3fArray& points, const VtIntArray& pointIndexes, const VtVec3fArray& normals, const VtIntArray& normalIndexes, const VtVec2fArray& uv, const VtIntArray& uvIndexes, const VtIntArray& vpf);
    void SetMeshTransform(RprApiObject mesh, const GfMatrix4d& transform);
    void SetMeshRefineLevel(RprApiObject mesh, int level, TfToken boundaryInterpolation);
    void SetMeshMaterial(RprApiObject mesh, const RprApiMaterial* material);
    void SetMeshVisibility(RprApiObject mesh, bool isVisible);

    RprApiObject CreateCurve(const VtVec3fArray& points, const VtIntArray& indexes, const float& width);
    void SetCurveMaterial(RprApiObject curve, const RprApiMaterial* material);

    RprApiObject CreateMeshInstance(RprApiObject prototypeMesh);

    RprApiMaterial* CreateMaterial(MaterialAdapter& materialAdapter);
    void DeleteMaterial(RprApiMaterial* rprApiMaterial);

    const GfMatrix4d& GetCameraViewMatrix() const;
    const GfMatrix4d& GetCameraProjectionMatrix() const;

    void SetCameraViewMatrix(const GfMatrix4d& m );
    void SetCameraProjectionMatrix(const GfMatrix4d& m);

    void EnableAov(TfToken const& aovName, HdFormat format = HdFormatCount);
    void DisableAov(TfToken const& aovName);
    void DisableAovs();
    bool IsAovEnabled(TfToken const& aovName);
    TfToken GetActiveAov() const;

    void ResizeAovFramebuffers(int width, int height);
    void GetFramebufferSize(GfVec2i* resolution) const;
    std::shared_ptr<char> GetFramebufferData(TfToken const& aovName, std::shared_ptr<char> buffer = nullptr, size_t* bufferSize = nullptr);
    void ClearFramebuffers();

    void Render();

    void DeleteMesh(RprApiObject mesh);
    void DeleteInstance(RprApiObject instance);

    bool IsGlInteropUsed() const;

    static void SetRenderDevice(int renderDevice);
    static void SetDenoising(bool enableDenoising);
    static void SetHybridQuality(int quality);
    static void SetRendererPlugin(int plugin);

    static int GetPluginType();
    static const char* GetTmpDir();
    static bool IsDenoisingEnabled();

private:
    HdRprApiImpl* m_impl = nullptr;
};

typedef std::shared_ptr<HdRprApi> HdRprApiSharedPtr;
typedef std::weak_ptr<HdRprApi> HdRprApiWeakPtr;

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RPR_API_H
