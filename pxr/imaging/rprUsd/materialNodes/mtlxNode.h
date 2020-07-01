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

#ifndef RPRUSD_MATERIAL_NODES_MTLX_NODE_H
#define RPRUSD_MATERIAL_NODES_MTLX_NODE_H

#include "pxr/imaging/rprUsd/materialRegistry.h"

#include <MaterialXCore/Document.h>

#include <stdexcept>

PXR_NAMESPACE_OPEN_SCOPE

class RprUsd_MtlxNodeElement : public RprUsdMaterialNodeInput {
public:
    RprUsd_MtlxNodeElement(
        MaterialX::ValueElementPtr element,
        RprUsdMaterialNodeElement::Type type);
    ~RprUsd_MtlxNodeElement() override = default;

    const char* GetName() const override { return GetCStr(m_mtlx->getName()); }
    const char* GetUIName() const override { return GetCStr(m_mtlx->getAttribute(MaterialX::ValueElement::UI_NAME_ATTRIBUTE)); }
    const char* GetUIMin() const override { return GetCStr(m_mtlx->getAttribute(MaterialX::ValueElement::UI_MIN_ATTRIBUTE)); }
    const char* GetUISoftMin() const override { return GetCStr(m_mtlx->getAttribute(MaterialX::ValueElement::UI_SOFT_MIN_ATTRIBUTE)); }
    const char* GetUIMax() const override { return GetCStr(m_mtlx->getAttribute(MaterialX::ValueElement::UI_MAX_ATTRIBUTE)); }
    const char* GetUISoftMax() const override { return GetCStr(m_mtlx->getAttribute(MaterialX::ValueElement::UI_SOFT_MAX_ATTRIBUTE)); }
    const char* GetUIFolder() const override { return GetCStr(m_mtlx->getAttribute(MaterialX::ValueElement::UI_FOLDER_ATTRIBUTE)); }
    const char* GetDocString() const override { return GetCStr(m_mtlx->getAttribute(MaterialX::ValueElement::DOC_ATTRIBUTE)); }
    const char* GetValueString() const override { return GetCStr(m_mtlx->getValueString()); }
    std::vector<TfToken> const& GetTokenValues() const override { return m_tokenValues; }

private:
    MaterialX::ValueElementPtr m_mtlx;
    std::vector<TfToken> m_tokenValues;
};

/// \class RprUsd_MtlxNodeInfo
///
/// RprUsd_MtlxNodeInfo describes the node that is defined by .mtlx file.
/// Right now, we support only those .mtlx definitions that corresponds to
/// the native RPR material node (RPR_MATERIAL_NODE_*).
/// In the future, obviously, we would like to be able to process custom
/// nodes that are implemented as MaterialX implementation graphs.
///
class RprUsd_MtlxNodeInfo : public RprUsdMaterialNodeInfo {
public:
    RprUsd_MtlxNodeInfo(
        MaterialX::DocumentPtr const& mtlxDoc,
        MaterialX::NodeDefPtr const& mtlxNodeDef,
        std::string const& uiFolder);
    ~RprUsd_MtlxNodeInfo() override = default;

    const char* GetName() const override { return GetCStr(m_mtlxNodeDef->getNodeString()); }
    const char* GetUIName() const override { return GetCStr(m_mtlxNodeDef->getAttribute(MaterialX::ValueElement::UI_NAME_ATTRIBUTE)); }
    const char* GetUIFolder() const override { return GetCStr(m_uiFolder); }

    size_t GetNumInputs() const override { return m_mtlxInputs.size(); }
    RprUsdMaterialNodeInput const* GetInput(size_t idx) const override { return &m_mtlxInputs[idx]; }

    size_t GetNumOutputs() const override { return m_mtlxOutputs.size(); }
    RprUsdMaterialNodeElement const* GetOutput(size_t idx) const override { return &m_mtlxOutputs[idx]; }

    RprUsdMaterialNodeFactoryFnc GetFactory() const;

private:
    std::string m_uiFolder;
    MaterialX::DocumentPtr m_mtlxDoc;
    MaterialX::NodeDefPtr m_mtlxNodeDef;
    std::vector<RprUsd_MtlxNodeElement> m_mtlxInputs;
    std::vector<RprUsd_MtlxNodeElement> m_mtlxOutputs;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_NODES_MTLX_NODE_H
