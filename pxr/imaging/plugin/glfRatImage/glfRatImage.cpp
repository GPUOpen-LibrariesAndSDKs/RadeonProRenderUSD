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
#include "pxr/imaging/glf/image.h"
#include "pxr/imaging/glf/utils.h"

#include <IMG/IMG_File.h>
#include <IMG/IMG_Stat.h>
#include <IMG/IMG_Plane.h>
#include <PXL/PXL_Raster.h>

PXR_NAMESPACE_OPEN_SCOPE

class Glf_RatImage : public GlfImage {
public:
    typedef GlfImage Base;

    Glf_RatImage();
    ~Glf_RatImage() override = default;

    std::string const& GetFilename() const override { return m_filename; }
    int GetWidth() const override { return m_width; }
    int GetHeight() const override { return m_height; }
    GLenum GetFormat() const override;
    GLenum GetType() const override { return m_outputType; }
    int GetBytesPerPixel() const override;
    int GetNumMipLevels() const override;

    bool IsColorSpaceSRGB() const override;

    bool GetMetadata(TfToken const& key, VtValue * value) const override { /* TODO */ return false; }
    bool GetSamplerMetadata(GLenum pname, VtValue * param) const override { /* TODO */ return false; }

    bool Read(StorageSpec const& storage) override;
    bool ReadCropped(int const cropTop,
                     int const cropBottom,
                     int const cropLeft,
                     int const cropRight,
                     StorageSpec const& storage) override;

    bool Write(StorageSpec const& storage,
               VtDictionary const& metadata) override { /* TODO */ return false; }

protected:
    bool _OpenForReading(std::string const& filename, int subimage,
                         int mip, bool suppressErrors) override;
    bool _OpenForWriting(std::string const& filename) override { /* TODO */ return false; }

private:
    bool _IsValidCrop(int cropTop, int cropBottom, int cropLeft, int cropRight);
    bool _CropAndResize(void const *sourceData, int const cropTop,
                   int const cropBottom,
                   int const cropLeft,
                   int const cropRight,
                   bool resizeNeeded,
                   StorageSpec const& storage);
        
    std::string m_filename;
    int m_width;
    int m_height;
    float m_gamma;
    
    //GL_UNSIGNED_BYTE, GL_FLOAT
    GLenum m_outputType; 
    
    int m_nchannels;
};

TF_REGISTRY_FUNCTION(TfType) {
    TfType t = TfType::Define<Glf_RatImage, TfType::Bases<GlfImage>>();
    t.SetFactory<GlfImageFactory<Glf_RatImage>>();
}

/// Returns the bpc (bits per channel) based on the GLType stored in storage
static int
_GetBytesPerChannelFromType(GLenum const& type) {
    switch(type) {
        case GL_UNSIGNED_BYTE:
            return 1;
        case GL_FLOAT:
            return 4;
        default:
            TF_CODING_ERROR("Unsupported type");
            return 4;
    }
}

static int
_GetNumElements(GLenum format) {
    switch (format) {
        case GL_DEPTH_COMPONENT:
        case GL_COLOR_INDEX:
        case GL_ALPHA:
        case GL_LUMINANCE:
        case GL_RED:
            return 1;
        case GL_LUMINANCE_ALPHA:
        case GL_RG:
            return 2;
        case GL_RGB:
            return 3;
        case GL_RGBA:
            return 4;
        default:
            TF_CODING_ERROR("Unsupported format");
            return 1;
    }
}

bool Glf_RatImage::_IsValidCrop(int cropTop, int cropBottom, int cropLeft, int cropRight) {
    int cropImageWidth = m_width - (cropLeft + cropRight);
    int cropImageHeight = m_height - (cropTop + cropBottom);
    return (cropTop >= 0 &&
            cropBottom >= 0 &&
            cropLeft >= 0 &&
            cropRight >= 0 &&
            cropImageWidth > 0 &&
            cropImageHeight > 0);
}

 Glf_RatImage::Glf_RatImage()
    : m_width(0)
    , m_height(0)
    , m_gamma(0.0f)
    , m_nchannels(0) {

}

GLenum Glf_RatImage::GetFormat() const {
    switch (m_nchannels) {
        case 1:
            return GL_RED;
        case 2:
            return GL_RG;
        case 3:
            return GL_RGB;
        case 4:
            return GL_RGBA;
        default:
            TF_CODING_ERROR("Unsupported numComponents");
            return 1;
    }
}

int Glf_RatImage::GetBytesPerPixel() const {
    return _GetBytesPerChannelFromType(m_outputType) * m_nchannels;
}

bool Glf_RatImage::IsColorSpaceSRGB() const {
    const float gamma_epsilon = 0.1f;

    // If we found gamma in the texture, use it to decide if we are sRGB
    bool isSRGB = ( fabs(m_gamma-0.45455f) < gamma_epsilon);
    if (isSRGB) {
        return true;
    }

    bool isLinear = ( fabs(m_gamma-1) < gamma_epsilon);
    if (isLinear) {
        return false;
    }

    if (m_gamma > 0) {
        TF_WARN("Unsupported gamma encoding in: %s", m_filename.c_str());
    }

    // Texture had no (recognized) gamma hint, make a reasonable guess
    return ((m_nchannels == 3  || m_nchannels == 4) && 
            GetType() == GL_UNSIGNED_BYTE);
}

int Glf_RatImage::GetNumMipLevels() const {
    return 1;
}

bool Glf_RatImage::_OpenForReading(
    std::string const& filename, int subimage,
    int mip, bool suppressErrors) {
    if (mip != 0 || subimage != 0) {
        /* TODO */
        return false;
    }

    m_filename = filename;

    auto imageFile = std::unique_ptr<IMG_File>(IMG_File::open(m_filename.c_str()));
    if (!imageFile) {
        return false;
    }

    auto& stat = imageFile->getStat();

    if (stat.getNumPlanes() < 1) {
        return false;
    }
    auto plane = stat.getPlane();

    m_width = stat.getXres();
    m_height = stat.getYres();
    if (m_width <= 0 || m_height <= 0) {
        return false;
    }

    m_nchannels = stat.getComponentCount();
    if (m_nchannels == 0) {
        return false;
    }

    m_gamma = plane->getColorSpaceGamma();

    if (plane->getDataType() == IMG_UCHAR) {
        m_outputType = GL_UNSIGNED_BYTE;
    } else if (plane->getDataType() == IMG_HALF) {
        m_outputType = GL_HALF_FLOAT;
    } else if (plane->getDataType() == IMG_FLOAT) {
        m_outputType = GL_FLOAT;
    } else {
        return false;
    }

    return true;
}

bool Glf_RatImage::Read(StorageSpec const& storage) {
    return ReadCropped(0, 0, 0, 0, storage);
}

bool Glf_RatImage::ReadCropped(
    int const cropTop,
    int const cropBottom,
    int const cropLeft,
    int const cropRight,
    StorageSpec const& storage) {
    if (cropTop || cropBottom || cropLeft || cropRight) {
        // TODO: implement cropping
        return false;
    }

    std::unique_ptr<IMG_File> imageFile(IMG_File::open(m_filename.c_str()));
    if (!imageFile) {
        return false;
    }

    UT_Array<PXL_Raster*> rasters;
    if (!imageFile->readImages(rasters) || rasters.isEmpty()) {
        return false;
    }

    // TODO: find out what to do with other images
    std::unique_ptr<PXL_Raster> raster(rasters[0]);
    if (rasters.size() > 1) {
        TF_WARN("Using only first raster from %s", m_filename.c_str());
        for (size_t i = 1; i < rasters.size(); ++i) {
            delete rasters[i];
        }
    }

    int numChannels;
    if (raster->getPacking() == PACK_SINGLE) {
        numChannels = 1;
    } else if (raster->getPacking() == PACK_DUAL) {
        numChannels = 2;
    } else if (raster->getPacking() == PACK_RGB) {
        numChannels = 3;
    } else if (raster->getPacking() == PACK_RGBA) {
        numChannels = 4;
    } else {
        TF_RUNTIME_ERROR("Failed to load image %s: unsupported RAT packing - %u", m_filename.c_str(), raster->getPacking());
        return false;
    }

    if (numChannels != _GetNumElements(storage.format)) {
        TF_RUNTIME_ERROR("Failed to load image %s: number of channels do not match - expected=%d, got=%d", m_filename.c_str(), m_nchannels, numChannels);
        return false;
    }

    GLenum format;
    int bytesPerChannel;
    if (raster->getFormat() == PXL_INT8) {
        format = GL_UNSIGNED_BYTE;
        bytesPerChannel = 1;
    } else if (raster->getFormat() == PXL_FLOAT16) {
        format = GL_HALF_FLOAT;
        bytesPerChannel = 2;
    } else if (raster->getFormat() == PXL_FLOAT32) {
        format = GL_FLOAT;
        bytesPerChannel = 4;
    } else {
        TF_RUNTIME_ERROR("Failed to load image %s: unsupported RAT format - %u", m_filename.c_str(), raster->getFormat());
        return false;
    }

    if (format != m_outputType) {
        TF_RUNTIME_ERROR("Failed to load image %s: format do not match - expected=%#x, got=%#x", m_filename.c_str(), m_outputType, format);
        return false;
    }

    if (raster->getXres() != storage.width ||
        raster->getYres() != storage.height) {
        TF_RUNTIME_ERROR("Failed to load image %s: resolution do not match - expected=%dx%d, got=%ldx%ld",
            m_filename.c_str(), m_width, m_height, raster->getXres(), raster->getYres());
        return false;
    }

    auto srcPixels = reinterpret_cast<uint8_t*>(raster->getPixels());
    auto srcStride = raster->getStride();
    auto dstPixels = reinterpret_cast<uint8_t*>(storage.data);
    auto dstStride = storage.width * numChannels * bytesPerChannel;
    for (int y = 0; y < storage.height; ++y) {
        int lineIndex = y;
        if (!storage.flipped) {
            // RAT image is flipped in Y axis so we flip the data when storage requires no flip
            lineIndex = storage.height - 1 - y;
        }

        auto dstData = dstPixels + dstStride * lineIndex;
        auto srcData = srcPixels + srcStride * y;

        std::memcpy(dstData, srcData, dstStride);
    }

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
