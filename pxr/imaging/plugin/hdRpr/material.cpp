#include "material.h"

#include "materialFactory.h"
#include "materialAdapter.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"

#include "tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
	HdRprMaterialTokens,
	(bxdf)				\
	(UsdPreviewSurface)
);


static const bool getMaterial(const HdMaterialNetworkMap & networkMap, EMaterialType & out_materialType, HdMaterialNetwork & out_surface)
{
	for (const auto & networkIt : networkMap.map)
	{
		const HdMaterialNetwork & network = networkIt.second;

		if (network.nodes.empty())
		{
			continue;
		}

		for (const HdMaterialNode & node : network.nodes)
		{
			if (node.identifier == HdRprMaterialTokens->UsdPreviewSurface)
			{
				out_surface = network;
				out_materialType = EMaterialType::USD_PREVIEW_SURFACE;

				return true;
			}
		}
	}

	out_materialType = EMaterialType::NONE;
	return false;
}

HdRprMaterial::HdRprMaterial(SdfPath const & id, HdRprApiSharedPtr rprApi) : HdMaterial(id) 
{
	m_rprApiWeakPtr = rprApi;
}

HdRprMaterial::~HdRprMaterial()
{
    if (auto rprApi = m_rprApiWeakPtr.lock()) {
        rprApi->DeleteMaterial(m_rprMaterial);
    }
}

void HdRprMaterial::Sync(HdSceneDelegate *sceneDelegate,
	HdRenderParam   *renderParam,
	HdDirtyBits     *dirtyBits)
{
	HdRprApiSharedPtr rprApi = m_rprApiWeakPtr.lock();
	if (!rprApi)
	{
		TF_CODING_ERROR("RprApi is expired");
		return;
	}

	if (*dirtyBits & HdMaterial::DirtyResource) {
		VtValue vtMat = sceneDelegate->GetMaterialResource(GetId());
		if (vtMat.IsHolding<HdMaterialNetworkMap>()) {

			HdMaterialNetworkMap networkMap = vtMat.UncheckedGet<HdMaterialNetworkMap>();

			EMaterialType materialType;
			HdMaterialNetwork surface;

			if (!getMaterial(networkMap, materialType, surface))
			{
				TF_CODING_WARNING("Material type not supported");
				return;
			}

			MaterialAdapter matAdapter = MaterialAdapter(materialType, surface);
			m_rprMaterial = rprApi->CreateMaterial(matAdapter);
		}
	}

	*dirtyBits = Clean;
}

HdDirtyBits HdRprMaterial::GetInitialDirtyBitsMask() const
{
	return AllDirty;
}

void HdRprMaterial::Reload()
{
	// no-op
}

const RprApiMaterial * HdRprMaterial::GetRprMaterialObject() const
{
	return m_rprMaterial;
}

PXR_NAMESPACE_CLOSE_SCOPE