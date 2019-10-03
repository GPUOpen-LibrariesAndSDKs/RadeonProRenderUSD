#ifndef HDRPR_INSTANCER_H
#define HDRPR_INSTANCER_H

#include "pxr/imaging/hd/instancer.h"

#include "pxr/base/vt/array.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/base/gf/quaternion.h"

#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;
class SdfPath;

class HdRprInstancer : public HdInstancer {
public:
	HdRprInstancer(HdSceneDelegate* delegate, SdfPath const &id,
		SdfPath const &parentInstancerId) :
		HdInstancer(delegate, id, parentInstancerId) {}

	VtMatrix4dArray ComputeTransforms(SdfPath const& prototypeId);

private:
	void Sync();

	VtMatrix4dArray m_transform;

	VtVec3fArray m_translate;
	VtQuaternionArray m_rotate;
	VtVec3fArray m_scale;

    std::mutex m_syncMutex;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_INSTANCER_H
