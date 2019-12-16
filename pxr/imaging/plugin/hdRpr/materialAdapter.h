#ifndef HDRPR_MATERIAL_ADAPTER_H
#define HDRPR_MATERIAL_ADAPTER_H

#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/matrix3f.h"
#include "pxr/imaging/hd/material.h"

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
    (translation)

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
};

struct MaterialTexture {
    std::string path;

    EColorChannel channel = EColorChannel::NONE;

    EWrapMode wrapS = EWrapMode::NONE;
    EWrapMode wrapT = EWrapMode::NONE;

    GfVec4f scale = GfVec4f(1.0f);
    GfVec4f bias = GfVec4f(0.0f);

    GfMatrix3f uvTransform = GfMatrix3f(1.0f);
};

typedef std::map<TfToken, VtValue> MaterialParams;
typedef std::map<TfToken, MaterialTexture> MaterialTextures;

typedef std::map<uint32_t, GfVec4f> MaterialRprParamsVec4f;
typedef std::map<uint32_t, uint32_t> MaterialRprParamsU;
typedef std::map<uint32_t, MaterialTexture> MaterialRprParamsTexture;

enum class EMaterialType : int32_t {
    NONE = -1
    , COLOR = 0
    , EMISSIVE
    , TRANSPERENT
    , USD_PREVIEW_SURFACE
};

class MaterialAdapter {
public:
    MaterialAdapter(EMaterialType type, const MaterialParams& params);

    MaterialAdapter(EMaterialType type, const HdMaterialNetwork& materialNetwork);

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

    void MarkAsDoublesided() {
        m_doublesided = true;
    }
    bool IsDoublesided() const {
        return m_doublesided;
    }

private:
    void PopulateRprColor(const MaterialParams& params);
    void PopulateEmissive(const MaterialParams& params);
    void PopulateTransparent(const MaterialParams& params);
    void PopulateUsdPreviewSurface(const MaterialParams& params, const MaterialTextures& textures);

    EMaterialType m_type;

    MaterialRprParamsVec4f m_vec4fRprParams;
    MaterialRprParamsU m_uRprParams;
    MaterialRprParamsTexture m_texRpr;

    MaterialTexture m_displacementTexture;
    bool m_doublesided = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_MATERIAL_ADAPTER_H
