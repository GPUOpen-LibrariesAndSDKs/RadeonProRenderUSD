#ifndef HDRPR_INSTANCER_H
#define HDRPR_INSTANCER_H

#include "pxr/pxr.h"

#include "pxr/imaging/hdSt/instancer.h"

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/base/gf/quaternion.h"

#include <vector>
#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;
class SdfPath;

class HdRprInstancer : public HdStInstancer {
public:
	HdRprInstancer(HdSceneDelegate* delegate, SdfPath const &id,
		SdfPath const &parentInstancerId) : 
		HdStInstancer(delegate, id, parentInstancerId) {}

	VtMatrix4dArray ComputeTransforms(SdfPath const& prototypeId);

private:
	void Sync();

	VtMatrix4dArray m_transform;

	VtVec3fArray m_translate;
	VtQuaternionArray m_rotate;
	VtVec3fArray m_scale;

	std::mutex m_mutex;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_INSTANCER_H
