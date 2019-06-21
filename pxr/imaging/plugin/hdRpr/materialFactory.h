#ifndef HDRPR_MATERIAL_FACTORY_H
#define HDRPR_MATERIAL_FACTORY_H

#include "pxr/pxr.h"

#include "RadeonProRender.h"
#include "RprSupport.h"

#include "materialAdapter.h"

PXR_NAMESPACE_OPEN_SCOPE

class RprApiMaterial
{
public:
	void SetType(EMaterialType type)
	{
		m_type = type;
	}
	EMaterialType GetType() const
	{
		return m_type;
	}


protected:
	virtual ~RprApiMaterial() {}
	EMaterialType m_type;
};

class MaterialFactory
{
public:
	virtual ~MaterialFactory() {}

	virtual RprApiMaterial * CreateMaterial(const EMaterialType type) = 0;

	virtual void DeleteMaterial(RprApiMaterial * rprmaterial) = 0;

	virtual void AttachMaterialToShape(rpr_shape mesh, const RprApiMaterial * material) = 0;

	virtual void AttachCurveToShape(rpr_shape mesh, const RprApiMaterial * material) = 0;

	virtual void SetMaterialInputs(RprApiMaterial * material, const MaterialAdapter & materialAdapter) = 0;
};

class RprMaterialFactory : public MaterialFactory
{
public:
	RprMaterialFactory(rpr_material_system matSys);

	virtual RprApiMaterial * CreateMaterial(const EMaterialType type) override;

	virtual void AttachMaterialToShape(rpr_shape mesh, const RprApiMaterial * material) override;

	virtual void AttachCurveToShape(rpr_shape mesh, const RprApiMaterial * material) override;

	virtual void SetMaterialInputs(RprApiMaterial * material, const MaterialAdapter & materialAdapter) override;

	virtual void DeleteMaterial(RprApiMaterial * rprmaterial) override;

private:
	rpr_material_system m_matSys;
};


class RprXMaterialFactory : public MaterialFactory
{
public:
	RprXMaterialFactory(rpr_material_system matSys, rpr_context rprContext);

	~RprXMaterialFactory();

	virtual RprApiMaterial * CreateMaterial(const EMaterialType type) override;

	virtual void AttachMaterialToShape(rpr_shape mesh, const RprApiMaterial * material) override;

	virtual void AttachCurveToShape(rpr_shape mesh, const RprApiMaterial * material) override;

	virtual void SetMaterialInputs(RprApiMaterial * material, const MaterialAdapter & materialAdapter) override;

	virtual void DeleteMaterial(RprApiMaterial * material) override;

private:
	const rpr_material_system m_matSys;

	const rpr_context m_context;

	rprx_context m_contextX = nullptr;
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_FACTORY_H