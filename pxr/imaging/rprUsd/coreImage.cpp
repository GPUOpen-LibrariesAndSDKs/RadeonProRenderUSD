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

#include "pxr/imaging/glf/glew.h"

#include "pxr/imaging/rprUsd/coreImage.h"
#include "pxr/imaging/rprUsd/helpers.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

rpr::ImageDesc GetRprImageDesc(rpr::ImageFormat format, uint32_t width, uint32_t height, uint32_t depth = 1) {
    int bytesPerComponent = 1;
    if (format.type == RPR_COMPONENT_TYPE_FLOAT16) {
        bytesPerComponent = 2;
    } else if (format.type == RPR_COMPONENT_TYPE_FLOAT32) {
        bytesPerComponent = 4;
    }

    rpr::ImageDesc desc = {};
    desc.image_width = width;
    desc.image_height = height;
    desc.image_depth = depth;
    desc.image_row_pitch = width * format.num_components * bytesPerComponent;
    desc.image_slice_pitch = desc.image_row_pitch * height;

    return desc;
}

rpr::Image* CreateRprImage(rpr::Context* context, GlfUVTextureData* textureData) {
    rpr::ImageFormat format = {};
    switch (textureData->GLType()) {
        case GL_UNSIGNED_BYTE:
            format.type = RPR_COMPONENT_TYPE_UINT8;
            break;
        case GL_HALF_FLOAT:
            format.type = RPR_COMPONENT_TYPE_FLOAT16;
            break;
        case GL_FLOAT:
            format.type = RPR_COMPONENT_TYPE_FLOAT32;
            break;
        default:
            TF_RUNTIME_ERROR("Unsupported pixel data GLtype: %#x", textureData->GLType());
            return nullptr;
    }

    switch (textureData->GLFormat()) {
        case GL_RED:
            format.num_components = 1;
            break;
        case GL_RGB:
            format.num_components = 3;
            break;
        case GL_RGBA:
            format.num_components = 4;
            break;
        default:
            TF_RUNTIME_ERROR("Unsupported pixel data GLformat: %#x", textureData->GLFormat());
            return nullptr;
    }
    rpr::ImageDesc desc = GetRprImageDesc(format, textureData->ResizedWidth(), textureData->ResizedHeight());

    rpr::Status status;
    auto rprImage = context->CreateImage(format, desc, textureData->GetRawBuffer(), &status);
    if (!rprImage) {
        RPR_ERROR_CHECK(status, "Failed to create image from data", context);
        return nullptr;
    }

    return rprImage;
}

} // namespace anonymous

RprUsdCoreImage* RprUsdCoreImage::Create(rpr::Context* context, std::string const& path) {
    auto textureData = GlfUVTextureData::New(path, INT_MAX, 0, 0, 0, 0);
    if (!textureData || !textureData->Read(0, false)) {
        return nullptr;
    }

    return Create(context, {{0, textureData.operator->()}});
}

RprUsdCoreImage* RprUsdCoreImage::Create(rpr::Context* context, uint32_t width, uint32_t height, rpr::ImageFormat format, void const* data, rpr::Status* status) {
    auto rootImage = context->CreateImage(format, GetRprImageDesc(format, width, height), data, status);
    if (!rootImage) {
        return nullptr;
    }

    return new RprUsdCoreImage(rootImage);
}

RprUsdCoreImage* RprUsdCoreImage::Create(
    rpr::Context* context,
    std::vector<UDIMTile> const& tiles) {

    if (tiles.empty()) {
        return nullptr;
    }

    if (tiles.size() == 1 && tiles[0].id == 0) {
        // Single non-UDIM tile
        auto rprImage = CreateRprImage(context, tiles[0].textureData);
        if (!rprImage) {
            return nullptr;
        }

        return new RprUsdCoreImage(rprImage);
    } else {
        // Process UDIM
        RprUsdCoreImage* coreImage = nullptr;

        for (auto tile : tiles) {
            if (tile.id < 1001 || tile.id > 1100) {
                TF_RUNTIME_ERROR("Invalid UDIM tile id - %u", tile.id);
                continue;
            }

            auto rprImage = CreateRprImage(context, tile.textureData);
            if (!rprImage) {
                continue;
            }

            if (!coreImage) {
                coreImage = new RprUsdCoreImage;

                rpr::ImageFormat rootImageFormat = {};
                rootImageFormat.num_components = 0;
                rootImageFormat.type = RPR_COMPONENT_TYPE_UINT8;
                rpr::ImageDesc rootImageDesc = {};

                rpr::Status status;
                coreImage->m_rootImage = context->CreateImage(rootImageFormat, rootImageDesc, nullptr, &status);
                if (!coreImage->m_rootImage) {
                    delete coreImage;
                    delete rprImage;
                    RPR_ERROR_CHECK(status, "Failed to create UDIM root image", context);
                    return nullptr;
                }
            }

            RPR_ERROR_CHECK(coreImage->m_rootImage->SetUDIM(tile.id, rprImage), "Failed to set UDIM");
            coreImage->m_subImages.push_back(rprImage);
        }

        return coreImage;
    }
}

RprUsdCoreImage::~RprUsdCoreImage() {
    delete m_rootImage;
    for (auto image : m_subImages) {
        delete image;
    }

    m_rootImage = nullptr;
    m_subImages.clear();
}

rpr::Image* RprUsdCoreImage::GetBaseImage() {
    return m_subImages.empty() ? m_rootImage : m_subImages[0];
}

template <typename F>
rpr::Status RprUsdCoreImage::ForEachImage(F f) {
    if (m_subImages.empty()) {
        return f(m_rootImage);
    } else {
        for (auto image : m_subImages) {
            auto status = f(image);
            if (status != RPR_SUCCESS) {
                return status;
            }
        }
        return RPR_SUCCESS;
    }
}

rpr::ImageFormat RprUsdCoreImage::GetFormat() {
    return RprUsdGetInfo<rpr::ImageFormat>(GetBaseImage(), RPR_IMAGE_FORMAT);
}

rpr::ImageDesc RprUsdCoreImage::GetDesc() {
    return RprUsdGetInfo<rpr::ImageDesc>(GetBaseImage(), RPR_IMAGE_DESC);
}

rpr::Status RprUsdCoreImage::GetInfo(rpr::ImageInfo imageInfo, size_t size, void* data, size_t* size_ret) {
    return GetBaseImage()->GetInfo(imageInfo, size, data, size_ret);
}

rpr::Status RprUsdCoreImage::SetWrap(rpr::ImageWrapType type) {
    return ForEachImage([type](rpr::Image* image) { return image->SetWrap(type); });
}

rpr::Status RprUsdCoreImage::SetGamma(float gamma) {
    return ForEachImage([gamma](rpr::Image* image) { return image->SetGamma(gamma); });
}

rpr::Status RprUsdCoreImage::SetMipmapEnabled(bool enabled) {
    return ForEachImage([enabled](rpr::Image* image) { return image->SetMipmapEnabled(enabled); });
}

rpr::Status RprUsdCoreImage::SetFilter(rpr::ImageFilterType type) {
    return ForEachImage([type](rpr::Image* image) { return image->SetFilter(type); });
}

rpr::Status RprUsdCoreImage::SetName(const char* name) {
    return ForEachImage([name](rpr::Image* image) { return image->SetName(name); });
}

PXR_NAMESPACE_CLOSE_SCOPE
