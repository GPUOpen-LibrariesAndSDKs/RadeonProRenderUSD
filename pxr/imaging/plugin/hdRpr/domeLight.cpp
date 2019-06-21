#include "domeLight.h"

#include "rprApi.h"
#include "renderParam.h"

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE
      

TF_DEFINE_PRIVATE_TOKENS(
	HdRprDomeLightTokens,
	(exposure)                                  \
	(intensity)                                 \
	(transform)                                 \
	(params)                                    \
	(texturePath)
);

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

	HdRprApiSharedPtr rprApi = m_rprApiWeakPrt.lock();
	if (!rprApi)
	{
		TF_CODING_ERROR("RprApi is expired");
		return;
	}

	HdDirtyBits bits = *dirtyBits;

	if (bits & DirtyParams)
	{
		// Get the color of the light
		// GfVec3f color = sceneDelegate->GetLightParamValue(id,HdPrimvarRoleTokens->color).Get<GfVec3f>();

		// Extract intensity
		float intensity = sceneDelegate->GetLightParamValue(id, HdRprDomeLightTokens->intensity).Get<float>();

		// Extract the exposure of the light
		float exposure = sceneDelegate->GetLightParamValue(id, HdRprDomeLightTokens->exposure).Get<float>();

		// Extract the transform of the light
		GfMatrix4d transform = sceneDelegate->Get(id, HdRprDomeLightTokens->transform).Get<GfMatrix4d>();
		
		VtValue texturePathValue = sceneDelegate->GetLightParamValue(id, HdRprDomeLightTokens->texturePath);

		std::string path;
		if (!texturePathValue.IsEmpty())
		{
			path = sceneDelegate->GetLightParamValue(id, HdRprDomeLightTokens->texturePath).Get<std::string>();
			removeFirstSlash(path);
		}
		
		float computed_intensity = computeLightIntensity(intensity, exposure);
		rprApi->CreateEnvironmentLight(path, computed_intensity, transform);
	}

	*dirtyBits = DirtyBits::Clean;
}


HdDirtyBits HdRprDomeLight::GetInitialDirtyBitsMask() const {
		return DirtyBits::AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE
