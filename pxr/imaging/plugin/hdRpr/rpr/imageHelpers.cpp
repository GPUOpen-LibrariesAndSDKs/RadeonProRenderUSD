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

#include "imageHelpers.h"
#include "helpers.h"

#include "pxr/imaging/glf/glew.h"
#include "pxr/imaging/glf/uvTextureData.h"
#include "pxr/imaging/glf/image.h"
#include "pxr/base/arch/fileSystem.h"

#ifdef ENABLE_RAT
#include <IMG/IMG_File.h>
#include <PXL/PXL_Raster.h>
#endif

#include <cstring>
#include <vector>
#include <array>

namespace rpr {

namespace {

ImageDesc GetRprImageDesc(ImageFormat format, uint32_t width, uint32_t height, uint32_t depth = 1) {
    int bytesPerComponent = 1;
    if (format.type == RPR_COMPONENT_TYPE_FLOAT16) {
        bytesPerComponent = 2;
    } else if (format.type == RPR_COMPONENT_TYPE_FLOAT32) {
        bytesPerComponent = 4;
    }

    ImageDesc desc = {};
    desc.image_width = width;
    desc.image_height = height;
    desc.image_depth = depth;
    desc.image_row_pitch = width * format.num_components * bytesPerComponent;
    desc.image_slice_pitch = desc.image_row_pitch * height;

    return desc;
}

} // namespace anonymous

Image* CreateImage(Context* context, uint32_t width, uint32_t height, ImageFormat format, void const* data, rpr::Status* status) {
    return context->CreateImage(format, GetRprImageDesc(format, width, height), data, status);
}

Image* CreateImage(Context* context, char const* path, bool forceLinearSpace) {
    PXR_NAMESPACE_USING_DIRECTIVE

#ifdef ENABLE_RAT
    auto dot = strrchr(path, '.');
    if (dot && strcmp(dot, ".rat") == 0) {
        auto ratImage = std::unique_ptr<IMG_File>(IMG_File::open(path));
        if (!ratImage) {
            TF_RUNTIME_ERROR("Failed to load image %s", path);
        }

        UT_Array<PXL_Raster*> images;
        std::shared_ptr<void> deferImagesRelease(nullptr, [&images](...) {
            for (auto image : images) {
                delete image;
            }
        });
        if (!ratImage->readImages(images) ||
            images.isEmpty()) {
            TF_RUNTIME_ERROR("Failed to load image %s", path);
        }

        // XXX: use the only first image, find out what to do with other images
        auto image = images[0];

        rpr_image_format format = {};
        if (image->getPacking() == PACK_SINGLE) {
            format.num_components = 1;
        } else if (image->getPacking() == PACK_DUAL) {
            format.num_components = 2;
        } else if (image->getPacking() == PACK_RGB) {
            format.num_components = 3;
        } else if (image->getPacking() == PACK_RGBA) {
            format.num_components = 4;
        } else {
            TF_RUNTIME_ERROR("Failed to load image %s: unsupported RAT packing", path);
        }

        if (image->getFormat() == PXL_INT8) {
            format.type = RPR_COMPONENT_TYPE_UINT8;
        } else if (image->getFormat() == PXL_FLOAT16) {
            format.type = RPR_COMPONENT_TYPE_FLOAT16;
        } else if (image->getFormat() == PXL_FLOAT32) {
            format.type = RPR_COMPONENT_TYPE_FLOAT32;
        } else {
            TF_RUNTIME_ERROR("Failed to load image %s: unsupported RAT format", path);
        }

        ImageDesc desc = GetRprImageDesc(format, image->getXres(), image->getYres());
        if (desc.image_height < 1 ||
            desc.image_width < 1) {
            TF_RUNTIME_ERROR("Failed to load image %s: incorrect dimensions", path);
        }

        // RAT image is flipped in Y axis
        std::vector<uint8_t> flippedImage;
        flippedImage.reserve(image->getStride() * desc.image_height);
        for (int y = 0; y < desc.image_height; ++y) {
            auto srcData = reinterpret_cast<uint8_t*>(image->getPixels()) + image->getStride() * y;
            auto dstData = &flippedImage[image->getStride() * (desc.image_height - 1 - y)];
            std::memcpy(dstData, srcData, image->getStride());
        }

        rpr::Status status;
        auto rprImage = context->CreateImage(format, desc, flippedImage.data(), &status);
        if (!rprImage) {
            RPR_ERROR_CHECK(status, "Failed to create image from data", context);
            return nullptr;
        }

        if (!forceLinearSpace &&
            (image->getColorSpace() == PXL_CS_LINEAR ||
            image->getColorSpace() == PXL_CS_GAMMA2_2 || 
            image->getColorSpace() == PXL_CS_CUSTOM_GAMMA)) {
            RPR_ERROR_CHECK(rprImage->SetGamma(image->getColorSpaceGamma()), "Failed to set image gamma", context);
        }

        return rprImage;
    }
#endif

    if (GlfImage::IsSupportedImageFile(path)) {
        auto textureData = GlfUVTextureData::New(path, INT_MAX, 0, 0, 0, 0);
        if (textureData && textureData->Read(0, false)) {
            ImageFormat format = {};
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
                TF_RUNTIME_ERROR("Failed to create image %s. Unsupported pixel data GLtype: %#x", path, textureData->GLType());
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
                TF_RUNTIME_ERROR("Failed to create image %s. Unsupported pixel data GLformat: %#x", path, textureData->GLFormat());
            }
            ImageDesc desc = GetRprImageDesc(format, textureData->ResizedWidth(), textureData->ResizedHeight());

            rpr::Status status;
            auto rprImage = context->CreateImage(format, desc, textureData->GetRawBuffer(), &status);
            if (!rprImage) {
                RPR_ERROR_CHECK(status, "Failed to create image from data", context);
                return nullptr;
            }

            auto internalFormat = textureData->GLInternalFormat();
            if (!forceLinearSpace &&
                (internalFormat == GL_SRGB ||
                internalFormat == GL_SRGB8 ||
                internalFormat == GL_SRGB_ALPHA ||
                internalFormat == GL_SRGB8_ALPHA8)) {
                // XXX(RPR): sRGB formula is different from straight pow decoding, but it's the best we can do right now
                RPR_ERROR_CHECK(rprImage->SetGamma(2.2f), "Failed to set image gamma");
            }

            return rprImage;
        }
    }

    return context->CreateImageFromFile(path);
}

ImageFormat GetImageFormat(Image* image) {
    return GetInfo<ImageFormat>(image, RPR_IMAGE_FORMAT);
}

ImageDesc GetImageDesc(Image* image) {
    return GetInfo<ImageDesc>(image, RPR_IMAGE_DESC);
}

} // namespace rpr
