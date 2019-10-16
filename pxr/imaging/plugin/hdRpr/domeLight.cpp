#include "domeLight.h"

#include "rprApi.h"
#include "renderParam.h"

#include "pxr/usd/ar/resolver.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/usdLux/blackbody.h"

PXR_NAMESPACE_OPEN_SCOPE

static void removeFirstSlash(std::string & string)
{
	// Don't need this for *nix/Mac
	#ifdef _WIN32
	if (string[0] == '/' || string[0] == '\\')
	{
		string.erase(0, 1);
	}
	#endif
}

static float computeLightIntensity(float intensity, float exposure)
{
	return intensity * exp2(exposure);
}

void HdRprDomeLight::Sync(HdSceneDelegate *sceneDelegate,
	HdRenderParam   *renderParam,
	HdDirtyBits     *dirtyBits)
{
	SdfPath const & id = GetId();

	HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
	if (!rprApi)
	{
		TF_CODING_ERROR("RprApi is expired");
		return;
	}

	HdDirtyBits bits = *dirtyBits;

    if (bits & HdLight::DirtyTransform) {
        m_transform = sceneDelegate->GetLightParamValue(id, HdLightTokens->transform).Get<GfMatrix4d>();
        // XXX: Required to match orientation with Houdini's Karma
        m_transform *= GfMatrix4d(1.0).SetScale(GfVec3d(-1.0f, 1.0f, 1.0f));
    }

    bool newLight = false;
    if (bits & HdLight::DirtyParams) {
        m_rprLight = nullptr;

        float intensity = sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity).Get<float>();
        float exposure = sceneDelegate->GetLightParamValue(id, HdLightTokens->exposure).Get<float>();
        float computedIntensity = computeLightIntensity(intensity, exposure);

        std::string texturePath;
        VtValue texturePathValue = sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFile);
        if (texturePathValue.IsHolding<SdfAssetPath>()) {
            auto& assetPath = texturePathValue.UncheckedGet<SdfAssetPath>();
            if (assetPath.GetResolvedPath().empty()) {
                texturePath = ArGetResolver().Resolve(assetPath.GetAssetPath());
            } else {
                texturePath = assetPath.GetResolvedPath();
            }
            // XXX: Why?
            removeFirstSlash(texturePath);
        } else if (texturePathValue.IsHolding<std::string>()) {
            // XXX: Is it even possible?
            texturePath = texturePathValue.UncheckedGet<std::string>();
        }

        if (texturePath.empty()) {
            GfVec3f color = sceneDelegate->GetLightParamValue(id, HdPrimvarRoleTokens->color).Get<GfVec3f>();
            if (sceneDelegate->GetLightParamValue(id, HdLightTokens->enableColorTemperature).Get<bool>()) {
                GfVec3f temperatureColor = UsdLuxBlackbodyTemperatureAsRgb(sceneDelegate->GetLightParamValue(id, HdLightTokens->colorTemperature).Get<float>());
                color[0] *= temperatureColor[0];
                color[1] *= temperatureColor[1];
                color[2] *= temperatureColor[2];
            }

            m_rprLight = rprApi->CreateEnvironmentLight(color, computedIntensity);
        } else {
            m_rprLight = rprApi->CreateEnvironmentLight(texturePath, computedIntensity);
        }

        if (m_rprLight) {
            newLight = true;
        }
    }

    if (newLight && (bits & HdLight::DirtyTransform)) {
        rprApi->SetLightTransform(m_rprLight.get(), m_transform);
    }

    *dirtyBits = HdLight::Clean;
}


HdDirtyBits HdRprDomeLight::GetInitialDirtyBitsMask() const {
		return HdLight::AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE
