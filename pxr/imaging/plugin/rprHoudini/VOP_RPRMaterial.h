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

#include <UT/UT_HDKVersion.h>
#include <VOP/VOP_Node.h>
#include <VOP/VOP_Operator.h>

#include <vector>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class RprUsdMaterialNodeInfo;

struct VOP_RPRMaterialOperator : public VOP_Operator {
    VOP_RPRMaterialOperator(RprUsdMaterialNodeInfo const* shaderInfo);

    RprUsdMaterialNodeInfo const* shaderInfo;
};

class VOP_RPRMaterial : public VOP_Node {
public:
    /// Adds an instance of VOP_RPRMaterial to a network.
    static OP_Node* Create(OP_Network* net, const char* name, OP_Operator* entry);

    /// Returns the templates for the shader's input parameters
    static PRM_Template* GetTemplates(RprUsdMaterialNodeInfo const* shaderInfo);

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
    VOP_RPRMaterial(OP_Network* parent, const char* name, VOP_RPRMaterialOperator* entry);

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

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPR_VOPS_MATERIAL_H
