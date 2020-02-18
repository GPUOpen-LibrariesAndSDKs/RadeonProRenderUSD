#ifndef HDRPR_LIGHT_H
#define HDRPR_LIGHT_H

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/imaging/hd/light.h"

#include "boostIncludePath.h"
#include BOOST_INCLUDE_PATH(variant.hpp)

namespace rpr { class Shape; class PointLight; class SpotLight; class IESLight; }

PXR_NAMESPACE_OPEN_SCOPE

class HdRprApi;
struct HdRprApiMaterial;

class HdRprLight : public HdLight {
public:
    HdRprLight(SdfPath const& id, TfToken const& lightType)
        : HdLight(id), m_lightType(lightType) {

    }

    ~HdRprLight() override = default;

    void Sync(HdSceneDelegate* sceneDelegate,
              HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Finalize(HdRenderParam* renderParam) override;

private:
    void CreateIESLight(HdRprApi* rprApi, std::string const& path);

    void CreateAreaLightMesh(HdRprApi* rprApi);
    rpr::Shape* CreateDiskLightMesh(HdRprApi* rprApi);
    rpr::Shape* CreateRectLightMesh(HdRprApi* rprApi, bool applyTransform = false, GfMatrix4f const& transform = GfMatrix4f(1.0f));
    rpr::Shape* CreateSphereLightMesh(HdRprApi* rprApi);
    rpr::Shape* CreateCylinderLightMesh(HdRprApi* rprApi);

    struct AreaLight;
    void SyncAreaLightGeomParams(AreaLight* light, HdSceneDelegate* sceneDelegate, float* intensity);

    void ReleaseLight(HdRprApi* rprApi);

private:
    const TfToken m_lightType;

    struct AreaLight {
        HdRprApiMaterial* material = nullptr;
        std::vector<rpr::Shape*> meshes;
        GfMatrix4f localTransform;
    };

    struct LightVariantEmpty {};
    using Light = BOOST_NS::variant<LightVariantEmpty, rpr::PointLight*, rpr::SpotLight*, rpr::IESLight*, AreaLight*>;
    enum LightType {
        kLightTypeNone,
        kLightTypePoint,
        kLightTypeSpot,
        kLightIES,
        kLightTypeArea
    };
    Light m_light;

    GfVec3f m_emisionColor = GfVec3f(0.0f);
    GfMatrix4f m_transform;

    bool m_created = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_LIGHT_H
