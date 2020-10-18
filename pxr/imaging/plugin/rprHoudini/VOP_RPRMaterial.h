/************************************************************************
Copyright 2020 Advanced Micro Devices, Inc
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
************************************************************************/

#ifndef RPR_VOPS_MATERIAL_H
#define RPR_VOPS_MATERIAL_H

#include "pxr/pxr.h"
#include "pxr/imaging/rprUsd/materialRegistry.h"

#include <rprMtlxLoader.h>

#include <UT/UT_HDKVersion.h>
#include <VOP/VOP_Node.h>
#include <VOP/VOP_Operator.h>

#include <vector>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class RprUsdMaterialNodeInfo;

struct VOP_RPRMaterialOperator : public VOP_Operator {
    RprUsdMaterialNodeInfo const* shaderInfo;

    static VOP_RPRMaterialOperator* Create(RprUsdMaterialNodeInfo const* shaderInfo);

private:
    template <typename VOP>
    static VOP_RPRMaterialOperator* _Create(RprUsdMaterialNodeInfo const* shaderInfo);

    VOP_RPRMaterialOperator(RprUsdMaterialNodeInfo const* shaderInfo, OP_Constructor construct, PRM_Template* templates);
};

class VOP_RPRMaterial : public VOP_Node {
public:
    /// Returns the templates for the shader's input parameters
    static PRM_Template* GetTemplates(RprUsdMaterialNodeInfo const* shaderInfo);

    VOP_RPRMaterial(OP_Network* parent, const char* name, OP_Operator* entry);

    /// Returns the label for input port at index i_idx
    const char* inputLabel(unsigned i_idx) const override;
    /// Returns the label for output port at index i_idx
    const char* outputLabel(unsigned i_idx) const override;

    /// Minimum inputs that must be connected to a node for it to cook.
    unsigned minInputs() const override;

    /// Returns the number input ports that should be visible
    unsigned getNumVisibleInputs() const override;

    /// Returns the number of ordered (ie : non-indexed) inputs
    unsigned orderedInputs() const override;

#if HDK_API_VERSION >= 18000000
    /// From VOP_Node
    UT_StringHolder getShaderName(
        VOP_ShaderNameStyle style,
        VOP_Type shader_type) const override;

    VOP_Type getShaderType() const override;
#endif

protected:
    /// Returns the internal name of an input parameter.
    void getInputNameSubclass(UT_String &in, int i_idx) const override;
    /// Returns the index of the named input
    int getInputFromNameSubclass(const UT_String &in) const override;
    /// Fills the info about the source of an input parameter
    void getInputTypeInfoSubclass(
        VOP_TypeInfo &o_type_info,
        int i_idx) override;
    /**
        Fills the info about the acceptable types for the source of an input
        parameter.
    */
    void getAllowedInputTypeInfosSubclass(
        unsigned i_idx,
        VOP_VopTypeInfoArray &o_type_infos) override;

    /// Returns the internal name of an output parameter.
    void getOutputNameSubclass(UT_String &out, int i_idx) const override;
    /// Fills the info about an output parameter
    void getOutputTypeInfoSubclass(
        VOP_TypeInfo &o_type_info,
        int i_idx) override;

private:
    RprUsdMaterialNodeInfo const* m_shaderInfo;
    VOP_Type m_shaderType = VOP_SURFACE_SHADER;
};

class VOP_MaterialX : public VOP_RPRMaterial {
public:
    VOP_MaterialX(OP_Network* parent, const char* name, OP_Operator* entry);

    static PRM_Template* GetTemplates(RprUsdMaterialNodeInfo const* shaderInfo);

    void opChanged(OP_EventType reason, void* data) override;
    bool runCreateScript() override;

private:
    static void ElementChoiceGenFunc(void* op, PRM_Name* choices, int maxChoicesSize, const PRM_SpareData*, const PRM_Parm*);

private:
    UT_String m_file;
    int m_reloadDummy = 0;
    double m_fileModificationTime;
    RPRMtlxLoader::RenderableElements m_renderableElements;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPR_VOPS_MATERIAL_H
