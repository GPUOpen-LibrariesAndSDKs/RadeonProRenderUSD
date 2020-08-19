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

#ifndef PXR_IMAGING_RPR_USD_CORE_IMAGE_H
#define PXR_IMAGING_RPR_USD_CORE_IMAGE_H

#include "pxr/imaging/rprUsd/api.h"
#include "pxr/imaging/glf/uvTextureData.h"

#include <RadeonProRender.hpp>

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class RprUsdCoreImage {
public:
    RPRUSD_API
    static RprUsdCoreImage* Create(rpr::Context* context, std::string const& path);

    struct UDIMTile {
        uint32_t id;
        GlfUVTextureData* textureData;

        UDIMTile(uint32_t id, GlfUVTextureData* textureData) : id(id), textureData(textureData) {}
    };
    RPRUSD_API
    static RprUsdCoreImage* Create(rpr::Context* context, std::vector<UDIMTile> const& textureData);

    RPRUSD_API
    static RprUsdCoreImage* Create(rpr::Context* context, uint32_t width, uint32_t height, rpr::ImageFormat format, void const* data, rpr::Status* status = nullptr);

    RPRUSD_API
    ~RprUsdCoreImage();

    RPRUSD_API
    rpr::Image* GetRootImage() { return m_rootImage; }

    RPRUSD_API
    rpr::ImageFormat GetFormat();

    RPRUSD_API
    rpr::ImageDesc GetDesc();

    RPRUSD_API
    rpr::Status GetInfo(rpr::ImageInfo imageInfo, size_t size, void* data, size_t* size_ret);

    RPRUSD_API
    rpr::Status SetWrap(rpr::ImageWrapType type);

    RPRUSD_API
    rpr::Status SetGamma(float gamma);

    RPRUSD_API
    rpr::Status SetMipmapEnabled(bool enabled);

    RPRUSD_API
    rpr::Status SetFilter(rpr::ImageFilterType type);

private:
    RprUsdCoreImage(rpr::Image* rootImage = nullptr) : m_rootImage(rootImage) {};
    rpr::Image* GetBaseImage();

    template <typename F>
    rpr::Status ForEachImage(F f);

private:
    rpr::Image* m_rootImage = nullptr;
    std::vector<rpr::Image*> m_subImages;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_RPR_USD_CORE_IMAGE_H
