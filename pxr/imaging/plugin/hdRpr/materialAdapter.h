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

#ifndef HDRPR_MATERIAL_ADAPTER_H
#define HDRPR_MATERIAL_ADAPTER_H

#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/matrix3f.h"
#include "pxr/imaging/hd/material.h"

#include <RadeonProRender.hpp>

PXR_NAMESPACE_OPEN_SCOPE

#define HDRPR_MATERIAL_TOKENS \
    (bxdf) \
    (file) \
    (scale) \
    (bias) \
    (wrapS) \
    (wrapT) \
    (black) \
    (clamp) \
    (repeat) \
    (mirror) \
    (UsdPreviewSurface) \
    (UsdUVTexture) \
    (UsdTransform2d) \
    (color) \
    (diffuseColor) \
    (emissiveColor) \
    (useSpecularWorkflow) \
    (useFakeCaustics) \
    (specularColor) \
    (metallic) \
    (roughness) \
    (clearcoat) \
    (clearcoatRoughness) \
    (opacity) \
    (opacityThreshold) \
    (ior) \
    (normal) \
    (displacement) \
    (transparency) \
    (rotation) \
    (translation) \
    (varname)

TF_DECLARE_PUBLIC_TOKENS(HdRprMaterialTokens, HDRPR_MATERIAL_TOKENS);

enum class EWrapMode {
    NONE = -1
    , BLACK
    , CLAMP
    , REPEAT
    , MIRROR
};

enum class EColorChannel {
    NONE = -1
    , RGBA
    , RGB
    , R
    , G
    , B
    , A
    , LUMINANCE
};

struct MaterialTexture {
    std::string path;

    EColorChannel channel = EColorChannel::NONE;

    EWrapMode wrapMode = EWrapMode::NONE;

    GfVec4f scale = GfVec4f(1.0f);
    GfVec4f bias = GfVec4f(0.0f);

    GfMatrix3f uvTransform = GfMatrix3f(1.0f);

    bool forceLinearSpace = false;
};

typedef std::map<TfToken, VtValue> MaterialParams;
typedef std::map<TfToken, MaterialTexture> MaterialTextures;

typedef std::map<rpr::MaterialNodeInput, GfVec4f> MaterialRprParamsVec4f;
typedef std::map<rpr::MaterialNodeInput, uint32_t> MaterialRprParamsU;
typedef std::map<rpr::MaterialNodeInput, MaterialTexture> MaterialRprParamsTexture;

struct NormalMapParam {
    MaterialTexture texture;
    float effectScale = 1.0f;
};
using MaterialRprParamsNormalMap = std::vector<std::pair<std::vector<rpr::MaterialNodeInput>, NormalMapParam>>;

enum class EMaterialType : int32_t {
    NONE = -1
    , COLOR = 0
    , EMISSIVE
    , TRANSPERENT
    , USD_PREVIEW_SURFACE
    , HOUDINI_PRINCIPLED_SHADER
};

class MaterialAdapter {
public:
    MaterialAdapter(EMaterialType type, MaterialParams const& params);

    MaterialAdapter(EMaterialType type, HdMaterialNetwork const& surfaceNetwork, HdMaterialNetwork const& displacementNetwork);

    EMaterialType GetType() const {
        return m_type;
    }

    const MaterialRprParamsVec4f& GetVec4fRprParams() const {
        return m_vec4fRprParams;
    }

    const MaterialRprParamsU& GetURprParams() const {
        return m_uRprParams;
    }

    const MaterialRprParamsTexture& GetTexRprParams() const {
        return m_texRpr;
    }

    const MaterialTexture& GetDisplacementTexture() const {
        return m_displacementTexture;
    }

    const MaterialRprParamsNormalMap& GetNormalMapParams() const {
        return m_normalMapParams;
    }

    bool IsDoublesided() const {
        return m_doublesided;
    }

    TfToken GetStName() const {
        return m_stName;
    }

private:
    void PopulateRprColor(const MaterialParams& params);
    void PopulateEmissive(const MaterialParams& params);
    void PopulateTransparent(const MaterialParams& params);
    void PopulateUsdPreviewSurface(const MaterialParams& params, const MaterialTextures& textures);
    void PopulateHoudiniPrincipledShader(HdMaterialNetwork const& surfaceNetwork, HdMaterialNetwork const& displacementNetwork);

    EMaterialType m_type;

    MaterialRprParamsVec4f m_vec4fRprParams;
    MaterialRprParamsU m_uRprParams;
    MaterialRprParamsTexture m_texRpr;
    MaterialRprParamsNormalMap m_normalMapParams;

    MaterialTexture m_displacementTexture;
    bool m_doublesided = false;

    TfToken m_stName;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_ADAPTER_H
