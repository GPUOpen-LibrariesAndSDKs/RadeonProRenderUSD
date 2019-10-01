#ifndef HDRPR_MESH_H
#define HDRPR_MESH_H

#include "pxr/pxr.h"

#include "pxr/imaging/hd/mesh.h"
#include "pxr/imaging/hd/vertexAdjacency.h"

#include "rprApi.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprParam;

class HdRprMesh final : public HdMesh {
public:
	HF_MALLOC_TAG_NEW("new HdRprMesh");

	HdRprMesh(SdfPath const& id, HdRprApiSharedPtr rprApi, SdfPath const& instancerId = SdfPath());
	~HdRprMesh() override = default;

	void Sync(
		HdSceneDelegate* sceneDelegate,
		HdRenderParam*   renderParam,
		HdDirtyBits*     dirtyBits,
		TfToken const&   reprName
	) override;

protected:
	// Inform the scene graph which state needs to be downloaded in the
	// first Sync() call: in this case, topology and points data to build
	// the geometry object in the ProRender scene graph.
	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

	// This callback from Rprim gives the prim an opportunity to set
	// additional dirty bits based on those already set.  This is done
	// before the dirty bits are passed to the scene delegate, so can be
	// used to communicate that extra information is needed by the prim to
	// process the changes.
	//
	// The return value is the new set of dirty bits, which replaces the bits
	// passed in.
	//
	// See HdRprim::PropagateRprimDirtyBits()
	virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

	// Initialize the given representation of this Rprim.
	// This is called prior to syncing the prim, the first time the repr
	// is used.
	//
	// reprName is the name of the repr to initalize.  HdRprim has already
	// resolved the reprName to its final value.
	//
	// dirtyBits is an in/out value.  It is initialized to the dirty bits
	// from the change tracker.  InitRepr can then set additional dirty bits
	// if additional data is required from the scene delegate when this
	// repr is synced.  InitRepr occurs before dirty bit propagation.
	//
	// See HdRprim::InitRepr()
	virtual void _InitRepr(TfToken const &reprName,
		HdDirtyBits *dirtyBits) override;
		
private:

    HdRprApiWeakPtr m_rprApiWeakPtr;
    std::vector<RprApiObjectPtr> m_rprMeshes;
    std::vector<std::vector<RprApiObjectPtr>> m_rprMeshInstances;
    RprApiObjectPtr m_fallbackMaterial;
    GfMatrix4d m_transform;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MESH_H
