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

#ifndef RPRUSD_MATERIAL_REGISTRY_H
#define RPRUSD_MATERIAL_REGISTRY_H

#include "pxr/imaging/rprUsd/api.h"
#include "pxr/imaging/rprUsd/debugCodes.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/base/arch/demangle.h"
#include "pxr/base/tf/singleton.h"

#include <MaterialXCore/Document.h>

#include <RadeonProRender.hpp>

class RPRMtlxLoader;

PXR_NAMESPACE_OPEN_SCOPE

class RprUsdImageCache;
class RprUsdCoreImage;
class RprUsdMaterial;
class RprUsdMaterialNodeInfo;

class RprUsd_MaterialNode;
class RprUsd_MtlxNodeInfo;
struct RprUsd_MaterialBuilderContext;

using RprUsdMaterialNodeFactoryFnc = std::function<
    RprUsd_MaterialNode*(
        RprUsd_MaterialBuilderContext* context,
        std::map<TfToken, VtValue> const& parameters)>;

struct RprUsdMaterialNodeDesc {
    RprUsdMaterialNodeFactoryFnc factory;
    RprUsdMaterialNodeInfo const* info;
};

#ifdef USE_USDSHADE_MTLX
using RprUsd_MaterialNetworkConnection = HdMaterialConnection2;
using RprUsd_MaterialNetwork = HdMaterialNetwork2;
inline void RprUsd_MaterialNetworkFromHdMaterialNetworkMap(HdMaterialNetworkMap const& hdNetworkMap, RprUsd_MaterialNetwork& result, bool* isVolume = nullptr) {
#if PXR_VERSION >= 2205
    result = HdConvertToHdMaterialNetwork2(hdNetworkMap, isVolume);
#else
    HdMaterialNetwork2ConvertFromHdMaterialNetworkMap(hdNetworkMap, &result, isVolume);
#endif
}
#else
struct RprUsd_MaterialNetworkConnection {
    SdfPath upstreamNode;
    TfToken upstreamOutputName;
};

struct RprUsd_MaterialNetwork {
    struct Node {
        TfToken nodeTypeId;
        std::map<TfToken, VtValue> parameters;
        std::map<TfToken, std::vector<RprUsd_MaterialNetworkConnection>> inputConnections;
    };

    std::map<SdfPath, Node> nodes;
    std::map<TfToken, RprUsd_MaterialNetworkConnection> terminals;
};
void RprUsd_MaterialNetworkFromHdMaterialNetworkMap(
    HdMaterialNetworkMap const& hdNetworkMap,
    RprUsd_MaterialNetwork& result,
    bool* isVolume = nullptr);
#endif // USE_USDSHADE_MTLX

/// \class RprUsdMaterialRegistry
///
/// Interface for the material resolution system. An RPR materials library is
/// responsible for resolving material node definitions and may create materials
/// from HdMaterialNetworkMap that are ready for use in hdRpr.
///
class RprUsdMaterialRegistry {
public:
    ~RprUsdMaterialRegistry();

    RPRUSD_API
    static RprUsdMaterialRegistry& GetInstance() {
        return TfSingleton<RprUsdMaterialRegistry>::GetInstance();
    }

    RPRUSD_API
    RprUsdMaterial* CreateMaterial(
        SdfPath const& materialId,
        HdSceneDelegate* sceneDelegate,
        HdMaterialNetworkMap const& networkMap,
        rpr::Context* rprContext,
        RprUsdImageCache* imageCache);

    RPRUSD_API
    TfToken const& GetMaterialNetworkSelector();

    RPRUSD_API
    std::vector<RprUsdMaterialNodeDesc> const& GetRegisteredNodes();

#ifdef USE_CUSTOM_MATERIALX_LOADER
    RPRUSD_API
    RPRMtlxLoader* GetMtlxLoader() const { return m_mtlxLoader.get(); }
#endif

    /// Register implementation of the Node with \p id
    RPRUSD_API
    void Register(
        TfToken const& id,
        RprUsdMaterialNodeFactoryFnc factory,
        RprUsdMaterialNodeInfo const* info = nullptr);

    struct TextureLoadRequest {
        std::string filepath;
        std::string colorspace;
        rpr::ImageWrapType wrapType;
        uint32_t numComponentsRequired = 0;

        std::function<void(std::shared_ptr<RprUsdCoreImage> const&)> onDidLoadTexture;
    };

    RPRUSD_API
    void EnqueueTextureLoadRequest(std::weak_ptr<TextureLoadRequest> textureLoadRequest);

    RPRUSD_API
    void CommitResources(RprUsdImageCache* imageCache);

private:
    friend class TfSingleton<RprUsdMaterialRegistry>;
    RprUsdMaterialRegistry();

private:
    /// Material network selector for the current session, controlled via env variable
    TfToken m_materialNetworkSelector;

#ifdef USE_CUSTOM_MATERIALX_LOADER
    std::unique_ptr<RPRMtlxLoader> m_mtlxLoader;
#endif

    MaterialX::DocumentPtr m_stdLibraries;

    std::vector<std::unique_ptr<RprUsd_MtlxNodeInfo>> m_mtlxInfos;
    bool m_mtlxDefsDirty = true;

    std::vector<RprUsdMaterialNodeDesc> m_registeredNodes;
    std::map<TfToken, size_t> m_registeredNodesLookup;

    std::vector<std::weak_ptr<TextureLoadRequest>> m_textureLoadRequests;
};

class RprUsdMaterialNodeInput;
class RprUsdMaterialNodeElement;

struct RprUsdMaterialNodeStateProvider {
    virtual VtValue GetValue(const char* name) = 0;
};

/// \class RprUsdMaterialNodeInfo
///
/// Describes resolved node, its name, inputs, outputs, etc
///
class RprUsdMaterialNodeInfo {
public:
    virtual ~RprUsdMaterialNodeInfo() = default;

    virtual const char* GetName() const = 0;

    virtual size_t GetNumInputs() const = 0;
    virtual RprUsdMaterialNodeInput const* GetInput(size_t idx) const = 0;

    virtual size_t GetNumOutputs() const = 0;
    virtual RprUsdMaterialNodeElement const* GetOutput(size_t idx) const = 0;

    virtual const char* GetUIName() const = 0;
    virtual const char* GetUIFolder() const { return nullptr; };

    virtual bool HasDynamicVisibility() const { return false; }
    struct VisibilityUpdate {
        struct Visibility {
            const char* name;
            bool isVisible;
        };
        std::vector<Visibility> parmsVisibility;

        void Add(bool isVisible, const char* name) {
            parmsVisibility.push_back({ name, isVisible });
        }
    };
    virtual VisibilityUpdate GetVisibilityUpdate(const char* changedParam, RprUsdMaterialNodeStateProvider*) const { return {}; };
};

class RprUsdMaterialNodeElement {
public:
    virtual ~RprUsdMaterialNodeElement() = default;

    virtual const char* GetName() const = 0;
    virtual const char* GetUIName() const = 0;
    virtual const char* GetDocString() const = 0;

    enum Type {
        kInvalid,
        kFloat,
        kAngle,
        kVector2,
        kVector3,
        kColor3,
        kNormal,
        kBoolean,
        kInteger,
        kToken,
        kString,
        kFilepath,
        kVolumeShader,
        kSurfaceShader,
        kDisplacementShader,
    };
    Type GetType() const { return m_type; }

protected:
    RprUsdMaterialNodeElement(Type type) : m_type(type) {}

protected:
    Type m_type;
};

class RprUsdMaterialNodeInput : public RprUsdMaterialNodeElement {
public:
    ~RprUsdMaterialNodeInput() override = default;

    virtual const char* GetUIMin() const = 0;
    virtual const char* GetUISoftMin() const = 0;
    virtual const char* GetUIMax() const = 0;
    virtual const char* GetUISoftMax() const = 0;
    virtual const char* GetUIFolder() const = 0;
    virtual const char* GetValueString() const = 0;
    virtual std::vector<TfToken> const& GetTokenValues() const = 0;

protected:
    RprUsdMaterialNodeInput(Type type) : RprUsdMaterialNodeElement(type) {}
};

inline TfToken const& RprUsdMaterialRegistry::GetMaterialNetworkSelector() {
    return m_materialNetworkSelector;
}

inline void RprUsdMaterialRegistry::Register(
    TfToken const& id,
    RprUsdMaterialNodeFactoryFnc factory,
    RprUsdMaterialNodeInfo const* info) {
    TF_DEBUG(RPR_USD_DEBUG_MATERIAL_REGISTRY).Msg("Registering material node with id \"%s\"\n", id.GetText());

    RprUsdMaterialNodeDesc desc = {};
    desc.factory = std::move(factory);
    desc.info = info;

    auto status = m_registeredNodesLookup.emplace(id, m_registeredNodes.size());
    if (!status.second) {
        TF_CODING_ERROR("Failed to register %s: already registered", id.GetText());
    } else {
        m_registeredNodes.push_back(std::move(desc));
    }
}

inline const char* GetCStr(std::string const& str) {
    return !str.empty() ? str.c_str() : nullptr;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_REGISTRY_H
