#pragma once

#include "pxr/pxr.h"

#include "pxr/imaging/hd/sprim.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/usd/sdf/path.h"


#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprLightBase : public HdLight {

public:
	HdRprLightBase(SdfPath const & id, HdRprApiSharedPtr rprApi)
		: HdLight(id)
		, m_rprApiWeakPrt(rprApi) {}

	~HdRprLightBase();

	// change tracking for HdStLight
	enum DirtyBits : HdDirtyBits {
		Clean = 0,
		DirtyTransform = 1 << 0,
		DirtyParams = 1 << 1,
		AllDirty = (DirtyTransform
		| DirtyParams)
	};

	/// Synchronizes state from the delegate to this object.
	/// @param[in, out]  dirtyBits: On input specifies which state is
	///                             is dirty and can be pulled from the scene
	///                             delegate.
	///                             On output specifies which bits are still
	///                             dirty and were not cleaned by the sync. 
	///                             
	virtual void Sync(HdSceneDelegate *sceneDelegate,
		HdRenderParam   *renderParam,
		HdDirtyBits     *dirtyBits) override;

	/// Returns the minimal set of dirty bits to place in the
	/// change tracker for use in the first sync of this prim.
	/// Typically this would be all dirty bits.
	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:

	virtual bool IsDirtyGeomParam(std::map<TfToken, float> & params) = 0;
	
	virtual bool IsDirtyMaterial(const GfVec3f & emmisionColor);

	// Ferch required params for geometry
	virtual const TfTokenVector & FetchLightGeometryParamNames() const = 0;

	//virtual RprApiObject CreateGeometryLight(std::map<TfToken, float> & params, const GfVec3f & emmisionColor) = 0;
	virtual RprApiObject CreateLightMesh(std::map<TfToken, float> & params) = 0;

	virtual RprApiMaterial * CreateLightMaterial(const GfVec3f & illumColor);

	// Normalize Light Color with surface area
	virtual GfVec3f NormalizeLightColor(const GfMatrix4d & transform, std::map<TfToken, float> & params, const GfVec3f & emmisionColor) = 0;

	HdRprApiWeakPtr m_rprApiWeakPrt;

	// Mesh with emmisive material
	RprApiObject m_lightMesh = nullptr;

	RprApiMaterial * m_lightMaterial = nullptr;

	GfVec3f m_emmisionColor = GfVec3f(0.f, 0.f, 0.f);
};

PXR_NAMESPACE_CLOSE_SCOPE



