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

#ifndef RPRUSD_MATERIAL_NODES_USD_NODE_H
#define RPRUSD_MATERIAL_NODES_USD_NODE_H

#include "rpr/baseNode.h"

PXR_NAMESPACE_OPEN_SCOPE

class RprUsdCoreImage;
class RprUsd_RprArithmeticNode;

class RprUsd_UsdPreviewSurface : public RprUsd_BaseRuntimeNode {
public:
    RprUsd_UsdPreviewSurface(
        RprUsd_MaterialBuilderContext* ctx,
        std::map<TfToken, VtValue> const& hydraParameters);
    ~RprUsd_UsdPreviewSurface() override = default;

    VtValue GetOutput(TfToken const& outputId) override;

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value) override;

private:
    bool m_useSpecular;
    VtValue m_albedo;
    VtValue m_reflection;

    std::unique_ptr<RprUsd_RprArithmeticNode> m_emissiveWeightNode;
    std::unique_ptr<RprUsd_RprArithmeticNode> m_refractionWeightNode;

    std::unique_ptr<RprUsd_RprArithmeticNode> m_normalMapScaleNode;
    std::unique_ptr<RprUsd_RprArithmeticNode> m_normalMapBiasNode;
    std::unique_ptr<RprUsd_BaseRuntimeNode> m_normalMapNode;

    std::shared_ptr<RprUsd_BaseRuntimeNode> m_displaceNode;
    VtValue m_displacementOutput;
};

#define RPRUSD_USD_UV_TEXTURE_TOKENS \
    (file) \
    (scale) \
    (bias) \
    (wrapS) \
    (wrapT) \
    (black) \
    (clamp) \
    (mirror) \
    (repeat) \
    (sourceColorSpace) \
    (sRGB) \
    (srgblinear) \
    (raw) \
    ((colorSpaceAuto, "auto")) \
    (st) \
    (rgba) \
    (rgb) \
    (r) \
    (g) \
    (b) \
    (a)

TF_DECLARE_PUBLIC_TOKENS(RprUsd_UsdUVTextureTokens, RPRUSD_USD_UV_TEXTURE_TOKENS);

class RprUsd_UsdUVTexture : public RprUsd_MaterialNode {
public:
    RprUsd_UsdUVTexture(
        RprUsd_MaterialBuilderContext* ctx,
        std::map<TfToken, VtValue> const& hydraParameters);
    ~RprUsd_UsdUVTexture() override = default;

    VtValue GetOutput(TfToken const& outputId) override;

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value) override;

private:
    RprUsd_MaterialBuilderContext* m_ctx;

    std::shared_ptr<RprUsdCoreImage> m_image;
    std::shared_ptr<rpr::MaterialNode> m_imageNode;
    std::shared_ptr<RprUsd_RprArithmeticNode> m_scaleNode;
    std::shared_ptr<RprUsd_RprArithmeticNode> m_biasNode;

    std::map<TfToken, VtValue> m_outputs;
};

class RprUsd_UsdPrimvarReader : public RprUsd_BaseRuntimeNode {
public:
    RprUsd_UsdPrimvarReader(
        RprUsd_MaterialBuilderContext* ctx,
        std::map<TfToken, VtValue> const& hydraParameters);
    ~RprUsd_UsdPrimvarReader() override = default;

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value) override;
};

class RprUsd_UsdTransform2d : public RprUsd_MaterialNode {
public:
    RprUsd_UsdTransform2d(
        RprUsd_MaterialBuilderContext* ctx,
        std::map<TfToken, VtValue> const& hydraParameters);
    ~RprUsd_UsdTransform2d() override = default;

    VtValue GetOutput(TfToken const& outputId) override;

    bool SetInput(
        TfToken const& inputId,
        VtValue const& value) override;

private:
    RprUsd_MaterialBuilderContext* m_ctx;

    std::unique_ptr<RprUsd_RprArithmeticNode> m_setZToOneNode;
    std::shared_ptr<RprUsd_RprArithmeticNode> m_transformNode;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_MATERIAL_NODES_USD_NODE_H
