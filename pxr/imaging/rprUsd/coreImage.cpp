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

template <typename ComponentT, typename PixelConverterFunc>
std::unique_ptr<uint8_t[]> _ConvertTexture(GlfUVTextureData* textureData, rpr::ImageFormat const& srcFormat, uint32_t dstNumComponents, PixelConverterFunc&& converter) {
    uint8_t* src = textureData->GetRawBuffer();

    size_t srcPixelStride = srcFormat.num_components * sizeof(ComponentT);
    size_t dstPixelStride = dstNumComponents * sizeof(ComponentT);

    size_t numPixels = size_t(textureData->ResizedWidth()) * textureData->ResizedHeight();
    auto dstData = std::make_unique<uint8_t[]>(numPixels * dstPixelStride);
    uint8_t* dst = dstData.get();

    for (size_t i = 0; i < numPixels; ++i) {
        converter((ComponentT*)(dst + i * dstPixelStride), (ComponentT*)(src + i * srcPixelStride));
    }

    return dstData;
}

template <typename T>
struct WhiteColor {
    const T value = static_cast<T>(1);
};

template <> struct WhiteColor<uint8_t> {
    const uint8_t value = 255u;
};

template <typename ComponentT>
std::unique_ptr<uint8_t[]> ConvertTexture(GlfUVTextureData* textureData, rpr::ImageFormat const& format, uint32_t dstNumComponents) {
    if (dstNumComponents < format.num_components) {
        // Trim excessive channels
        return _ConvertTexture<ComponentT>(textureData, format, dstNumComponents,
            [=](ComponentT* dst, ComponentT* src) {
                for (size_t i = 0; i < dstNumComponents; ++i) {
                    dst[i] = src[i];
                }
            }
        );
    }

    if (format.num_components == 1) {
        // Expand to a required amount of channels. Example: greyscale texture that is stored as single-channel.
        if (dstNumComponents == 4) {
            // r -> rrr1
            return _ConvertTexture<ComponentT>(textureData, format, dstNumComponents,
                [](ComponentT* dst, ComponentT* src) {
                    dst[0] = dst[1] = dst[2] = src[0];
                    dst[3] = WhiteColor<ComponentT>{}.value;
                }
            );
        } else {
            return _ConvertTexture<ComponentT>(textureData, format, dstNumComponents,
                [=](ComponentT* dst, ComponentT* src) {
                    for (size_t i = 0; i < dstNumComponents; ++i) {
                        dst[i] = src[0];
                    }
                }
            );
        }
    } else if (format.num_components == 2) {
        if (dstNumComponents == 4) {
            // rg -> rrrg
            return _ConvertTexture<ComponentT>(textureData, format, dstNumComponents,
                [](ComponentT* dst, ComponentT* src) {
                    dst[0] = dst[1] = dst[2] = src[0];
                    dst[3] = src[1];
                }
            );
        } else {
            // rg -> rrr
            return _ConvertTexture<ComponentT>(textureData, format, dstNumComponents,
                [](ComponentT* dst, ComponentT* src) {
                    dst[0] = dst[1] = dst[2] = src[0];
                }
            );
        }
    } else if (format.num_components == 3) {
        // rgb -> rgb1
        return _ConvertTexture<ComponentT>(textureData, format, dstNumComponents,
            [](ComponentT* dst, ComponentT* src) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = WhiteColor<ComponentT>{}.value;
            }
        );
    }

    return nullptr;
}

rpr::Image* CreateRprImage(rpr::Context* context, GlfUVTextureData* textureData, uint32_t numComponentsRequired) {
    rpr::ImageFormat format = {};

#if PXR_VERSION >= 2011
    auto hioFormat = textureData->GetHioFormat();
    GLenum glType = GlfGetGLType(hioFormat);
    GLenum glFormat = GlfGetGLFormat(hioFormat);
#else
    GLenum glType = textureData->GLType();
    GLenum glFormat = textureData->GLFormat();
#endif

    switch (glType) {
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
            TF_RUNTIME_ERROR("Unsupported pixel data GLtype: %#x", glType);
            return nullptr;
    }

    switch (glFormat) {
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
            TF_RUNTIME_ERROR("Unsupported pixel data GLformat: %#x", glFormat);
            return nullptr;
    }
    rpr::ImageDesc desc = GetRprImageDesc(format, textureData->ResizedWidth(), textureData->ResizedHeight());

    auto textureBuffer = textureData->GetRawBuffer();

    std::unique_ptr<uint8_t[]> convertedData;
    if (numComponentsRequired != 0 &&
        numComponentsRequired != format.num_components) {
        if (format.type == RPR_COMPONENT_TYPE_UINT8) {
            convertedData = ConvertTexture<uint8_t>(textureData, format, numComponentsRequired);
        } else if (format.type == RPR_COMPONENT_TYPE_FLOAT16) {
            convertedData = ConvertTexture<GfHalf>(textureData, format, numComponentsRequired);
        } else if (format.type == RPR_COMPONENT_TYPE_FLOAT32) {
            convertedData = ConvertTexture<float>(textureData, format, numComponentsRequired);
        }

        if (convertedData) {
            textureBuffer = convertedData.get();
            format.num_components = numComponentsRequired;
            desc = GetRprImageDesc(format, textureData->ResizedWidth(), textureData->ResizedHeight());
        }
    }

    rpr::Status status;
    auto rprImage = context->CreateImage(format, desc, textureBuffer, &status);
    if (!rprImage) {
        RPR_ERROR_CHECK(status, "Failed to create image from data", context);
        return nullptr;
    }

    return rprImage;
}

} // namespace anonymous

RprUsdCoreImage* RprUsdCoreImage::Create(rpr::Context* context, std::string const& path, uint32_t numComponentsRequired) {
    auto textureData = GlfUVTextureData::New(path, INT_MAX, 0, 0, 0, 0);
    if (!textureData || !textureData->Read(0, false)) {
        return nullptr;
    }

    return Create(context, {{0, textureData.operator->()}}, numComponentsRequired);
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
    std::vector<UDIMTile> const& tiles,
    uint32_t numComponentsRequired) {

    if (tiles.empty()) {
        return nullptr;
    }

    if (tiles.size() == 1 && tiles[0].id == 0) {
        // Single non-UDIM tile
        auto rprImage = CreateRprImage(context, tiles[0].textureData, numComponentsRequired);
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

            auto rprImage = CreateRprImage(context, tile.textureData, numComponentsRequired);
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

rpr::Status RprUsdCoreImage::SetColorSpace(const char* colorSpace) {
    return ForEachImage([colorSpace](rpr::Image* image) {
        // TODO: add C++ wrapper
        auto rprImageHandle = rpr::GetRprObject(image);
        return rprImageSetOcioColorspace(rprImageHandle, colorSpace);
    });
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
