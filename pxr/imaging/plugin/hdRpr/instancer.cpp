#include "instancer.h"

#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
	_tokens,
	(instanceTransform)
	(rotate)
	(scale)
	(translate)
);


void HdRprInstancer::Sync()
{
	HD_TRACE_FUNCTION();
	HF_MALLOC_TAG_FUNCTION();

	SdfPath const& instancerId = GetId();

	HdChangeTracker &changeTracker = GetDelegate()->GetRenderIndex().GetChangeTracker();

	// Use the double-checked locking pattern to check if this instancer's
	// primvars are dirty.
	int dirtyBits = changeTracker.GetInstancerDirtyBits(instancerId);
	if (!HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, instancerId)) {
		return;
	}

	std::lock_guard<std::mutex> lock(m_syncMutex);
	dirtyBits = changeTracker.GetInstancerDirtyBits(instancerId);
	if (!HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, instancerId)) {
		return;
	}

	HdPrimvarDescriptorVector primvars = GetDelegate()->GetPrimvarDescriptors(instancerId, HdInterpolationInstance);
	TF_FOR_ALL(primvarIt, primvars) {
		if (!HdChangeTracker::IsPrimvarDirty(dirtyBits, instancerId, primvarIt->name))
		{
			continue;
		}

		VtValue value = GetDelegate()->Get(instancerId, primvarIt->name);
		if (value.IsEmpty())
		{
			continue;
		}

		if (primvarIt->name == _tokens->translate)
		{
			m_translate = value.UncheckedGet<VtVec3fArray>();
		}
		else if (primvarIt->name == _tokens->rotate)
		{
			m_rotate = value.UncheckedGet<VtQuaternionArray>();
		}
		else if (primvarIt->name == _tokens->scale)
		{
			m_scale = value.UncheckedGet<VtVec3fArray>();
		}
		else if (primvarIt->name == _tokens->instanceTransform)
		{
			m_transform = value.UncheckedGet<VtMatrix4dArray>();
		}
	}

	// Mark the instancer as clean
	changeTracker.MarkInstancerClean(instancerId);
}

VtMatrix4dArray HdRprInstancer::ComputeTransforms(SdfPath const& prototypeId)
{
	Sync();

	GfMatrix4d instancerTransform =
		GetDelegate()->GetInstancerTransform(GetId());

	VtIntArray instanceIndices =
		GetDelegate()->GetInstanceIndices(GetId(), prototypeId);

	VtMatrix4dArray transforms;
	transforms.reserve(instanceIndices.size());
	for (int idx : instanceIndices)
	{
		GfMatrix4d translateMat(1);
		GfMatrix4d rotateMat(1);
		GfMatrix4d scaleMat(1);
		GfMatrix4d transform(1);

		if (!m_translate.empty())
		{
			translateMat.SetTranslate(GfVec3d(m_translate[idx]));
		}

		if (!m_rotate.empty())
		{
			rotateMat.SetRotate(m_rotate[idx]);
		}

		if (!m_scale.empty())
		{
			scaleMat.SetScale(GfVec3d(m_scale[idx]));
		}
		if (!m_transform.empty())
		{
			transform = m_transform[idx];
		}

		transforms.push_back(transform * scaleMat * rotateMat * translateMat * instancerTransform);
	}

	HdRprInstancer * parentInstancer = static_cast<HdRprInstancer *>( GetDelegate()->GetRenderIndex().GetInstancer(GetParentId()));
	if (!parentInstancer) {
		return transforms;
	}

	VtMatrix4dArray wordTransform;
	for (const GfMatrix4d & parentTransform : parentInstancer->ComputeTransforms(GetId()))
	{
		for (const GfMatrix4d & localTransform : transforms)
		{
			wordTransform.push_back(parentTransform * localTransform);
		}
	}

	return wordTransform;
}

PXR_NAMESPACE_CLOSE_SCOPE


