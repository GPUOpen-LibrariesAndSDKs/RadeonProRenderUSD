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

#include "util.h"

#include "pxr/base/tf/staticTokens.h"
#include "pxr/imaging/glf/utils.h"

#include <cstring>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    RprUsdUDIMTags,
    ((arnoldTag, "<UDIM>"))
    ((sidefxTag, "%(UDIM)d"))
);

bool RprUsdGetUDIMFormatString(std::string const& filepath, std::string* out_formatString) {
    for (auto& udimTag : RprUsdUDIMTags->allTokens) {
        auto idx = filepath.rfind(udimTag.GetString());
        if (idx != std::string::npos) {
            *out_formatString = filepath;
            out_formatString->replace(idx, udimTag.size(), "%i");
            return true;
        }
    }

    return false;
}

bool RprUsdInitGLApi() {
#if PXR_VERSION >= 2102
    return GarchGLApiLoad();
#else
    return GlfGlewInit();
#endif
}

#if PXR_VERSION >= 2011

static const RprUsdTextureData::GLMetadata g_GLMetadata[HioFormatCount] =
{
    // glFormat, glType,        glInternatFormat  // HioFormat
    {GL_RED,  GL_UNSIGNED_BYTE, GL_R8          }, // UNorm8
    {GL_RG,   GL_UNSIGNED_BYTE, GL_RG8         }, // UNorm8Vec2
    {GL_RGB,  GL_UNSIGNED_BYTE, GL_RGB8        }, // UNorm8Vec3
    {GL_RGBA, GL_UNSIGNED_BYTE, GL_RGBA8       }, // UNorm8Vec4

    {GL_RED,  GL_BYTE,          GL_R8_SNORM    }, // SNorm8
    {GL_RG,   GL_BYTE,          GL_RG8_SNORM   }, // SNorm8Vec2
    {GL_RGB,  GL_BYTE,          GL_RGB8_SNORM  }, // SNorm8Vec3
    {GL_RGBA, GL_BYTE,          GL_RGBA8_SNORM }, // SNorm8Vec4

    {GL_RED,  GL_HALF_FLOAT,    GL_R16F        }, // Float16
    {GL_RG,   GL_HALF_FLOAT,    GL_RG16F       }, // Float16Vec2
    {GL_RGB,  GL_HALF_FLOAT,    GL_RGB16F      }, // Float16Vec3
    {GL_RGBA, GL_HALF_FLOAT,    GL_RGBA16F     }, // Float16Vec4

    {GL_RED,  GL_FLOAT,         GL_R32F        }, // Float32
    {GL_RG,   GL_FLOAT,         GL_RG32F       }, // Float32Vec2
    {GL_RGB,  GL_FLOAT,         GL_RGB32F      }, // Float32Vec3
    {GL_RGBA, GL_FLOAT,         GL_RGBA32F     }, // Float32Vec4

    {GL_RED,  GL_DOUBLE,        GL_RED         }, // Double64
    {GL_RG,   GL_DOUBLE,        GL_RG          }, // Double64Vec2
    {GL_RGB,  GL_DOUBLE,        GL_RGB         }, // Double64Vec3
    {GL_RGBA, GL_DOUBLE,        GL_RGBA        }, // Double64Vec4

    {GL_RED,  GL_UNSIGNED_SHORT,GL_R16UI       }, // UInt16
    {GL_RG,   GL_UNSIGNED_SHORT,GL_RG16UI      }, // UInt16Vec2
    {GL_RGB,  GL_UNSIGNED_SHORT,GL_RGB16UI     }, // UInt16Vec3
    {GL_RGBA, GL_UNSIGNED_SHORT,GL_RGBA16UI    }, // UInt16Vec4

    {GL_RED,  GL_SHORT,         GL_R16I        }, // Int16
    {GL_RG,   GL_SHORT,         GL_RG16I       }, // Int16Vec2
    {GL_RGB,  GL_SHORT,         GL_RGB16I      }, // Int16Vec3
    {GL_RGBA, GL_SHORT,         GL_RGBA16I     }, // Int16Vec4

    {GL_RED,  GL_UNSIGNED_INT,  GL_R32UI       }, // UInt32
    {GL_RG,   GL_UNSIGNED_INT,  GL_RG32UI      }, // UInt32Vec2
    {GL_RGB,  GL_UNSIGNED_INT,  GL_RGB32UI     }, // UInt32Vec3
    {GL_RGBA, GL_UNSIGNED_INT,  GL_RGBA32UI    }, // UInt32Vec4

    {GL_RED,  GL_INT,           GL_R32I        }, // Int32
    {GL_RG,   GL_INT,           GL_RG32I       }, // Int32Vec2
    {GL_RGB,  GL_INT,           GL_RGB32I      }, // Int32Vec3
    {GL_RGBA, GL_INT,           GL_RGBA32I     }, // Int32Vec4

    {GL_NONE, GL_NONE, GL_NONE }, // UNorm8srgb - not supported by OpenGL
    {GL_NONE, GL_NONE, GL_NONE }, // UNorm8Vec2srgb - not supported by OpenGL
    {GL_RGB,  GL_UNSIGNED_BYTE, GL_SRGB8,       },  // UNorm8Vec3srgb
    {GL_RGBA, GL_UNSIGNED_BYTE, GL_SRGB8_ALPHA8 }, // UNorm8Vec4sRGB

    {GL_RGB,  GL_FLOAT,
              GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT   }, // BC6FloatVec3
    {GL_RGB,  GL_FLOAT,
              GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT }, // BC6UFloatVec3
    {GL_RGBA, GL_UNSIGNED_BYTE,
              GL_COMPRESSED_RGBA_BPTC_UNORM         }, // BC7UNorm8Vec4
    {GL_RGBA, GL_UNSIGNED_BYTE,
              GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM   }, // BC7UNorm8Vec4srgb
    {GL_RGBA, GL_UNSIGNED_BYTE,
              GL_COMPRESSED_RGBA_S3TC_DXT1_EXT       }, // BC1UNorm8Vec4
    {GL_RGBA, GL_UNSIGNED_BYTE,
              GL_COMPRESSED_RGBA_S3TC_DXT5_EXT      }, // BC3UNorm8Vec4
};

#endif // PXR_VERSION >= 2011

#if PXR_VERSION >= 2105

std::shared_ptr<RprUsdTextureData> RprUsdTextureData::New(std::string const& filepath) {
    auto ret = std::make_unique<RprUsdTextureData>();
    auto hioImage = HioImage::OpenForReading(filepath);
    if (!hioImage) {
        return nullptr;
    }

    ret->_hioStorageSpec.width = hioImage->GetWidth();
    ret->_hioStorageSpec.height = hioImage->GetHeight();
    ret->_hioStorageSpec.depth = 1;
    ret->_hioStorageSpec.format = hioImage->GetFormat();
    ret->_hioStorageSpec.flipped = false;

    size_t dataSize = ret->_hioStorageSpec.width * ret->_hioStorageSpec.height * HioGetDataSizeOfFormat(ret->_hioStorageSpec.format);
    ret->_data = std::make_unique<uint8_t[]>(dataSize);
    ret->_hioStorageSpec.data = ret->_data.get();

    if (!hioImage->Read(ret->_hioStorageSpec)) {
        return nullptr;
    }

    return ret;
}

uint8_t* RprUsdTextureData::GetData() const {
    return _data.get();
}

int RprUsdTextureData::GetWidth() const {
    return _hioStorageSpec.width;
}

int RprUsdTextureData::GetHeight() const {
    return _hioStorageSpec.height;
}

RprUsdTextureData::GLMetadata RprUsdTextureData::GetGLMetadata() const {
    return g_GLMetadata[_hioStorageSpec.format];
}

#else // PXR_VERSION < 2105

std::shared_ptr<RprUsdTextureData> RprUsdTextureData::New(std::string const& filepath) {
    auto ret = std::make_unique<RprUsdTextureData>();

    ret->_uvTextureData = GlfUVTextureData::New(filepath, INT_MAX, 0, 0, 0, 0);
    if (!ret->_uvTextureData || !ret->_uvTextureData->Read(0, false)) {
        return nullptr;
    }

    return ret;
}

uint8_t* RprUsdTextureData::GetData() const {
    return _uvTextureData->GetRawBuffer();
}

int RprUsdTextureData::GetWidth() const {
    return _uvTextureData->ResizedWidth();
}

int RprUsdTextureData::GetHeight() const {
    return _uvTextureData->ResizedHeight();
}

RprUsdTextureData::GLMetadata RprUsdTextureData::GetGLMetadata() const {
#if PXR_VERSION >= 2011

# if PXR_VERSION >= 2102
    HioFormat hioFormat = _uvTextureData->GetFormat();
# else // PXR_VERSION < 2102
    HioFormat hioFormat = _uvTextureData->GetHioFormat();
# endif // PXR_VERSION >= 2102
    return g_GLMetadata[hioFormat];

#else // PXR_VERSION < 2011
    RprUsdTextureData::GLMetadata ret;
    ret.internalFormat = _uvTextureData->GLInternalFormat();
    ret.glType = _uvTextureData->GLType();
    ret.glFormat = _uvTextureData->GLFormat();
    return ret;
#endif // PXR_VERSION >= 2011
}

#endif // PXR_VERSION >= 2105

PXR_NAMESPACE_CLOSE_SCOPE
