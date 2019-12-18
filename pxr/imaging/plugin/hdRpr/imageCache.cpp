#include "imageCache.h"
#include "rprcpp/rprContext.h"
#include "pxr/imaging/glf/glew.h"
#include "pxr/imaging/glf/uvTextureData.h"
#include "pxr/imaging/glf/image.h"
#include "pxr/base/arch/fileSystem.h"

PXR_NAMESPACE_OPEN_SCOPE

ImageCache::ImageCache(rpr::Context* context)
    : m_context(context) {

}

std::shared_ptr<rpr::Image> ImageCache::GetImage(std::string const& path) {
    ImageMetadata md(path);

    auto it = m_cache.find(path);
    if (it != m_cache.end() && it->second.IsMetadataEqual(md)) {
        if (auto image = it->second.handle.lock()) {
            return image;
        }
    }

    auto image = CreateImage(path);
    if (image) {
        md.handle = image;
        m_cache.emplace(path, md);
    }
    return image;
}

std::shared_ptr<rpr::Image> ImageCache::CreateImage(std::string const& path) {
    try {
        if (!GlfImage::IsSupportedImageFile(path)) {
            return std::make_shared<rpr::Image>(m_context->GetHandle(), path.c_str());
        }

        auto textureData = GlfUVTextureData::New(path, INT_MAX, 0, 0, 0, 0);
        if (textureData && textureData->Read(0, false)) {
            rpr_image_format format = {};
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
                    TF_CODING_ERROR("Failed to create image %s. Unsupported pixel data GLtype: %#x", path.c_str(), textureData->GLType());
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
                    TF_CODING_ERROR("Failed to create image %s. Unsupported pixel data GLformat: %#x", path.c_str(), textureData->GLFormat());
                    return nullptr;
            }

            auto rprImage = std::make_shared<rpr::Image>(m_context->GetHandle(), textureData->ResizedWidth(), textureData->ResizedHeight(), format, textureData->GetRawBuffer());

            auto internalFormat = textureData->GLInternalFormat();
            if (internalFormat == GL_SRGB ||
                internalFormat == GL_SRGB8 ||
                internalFormat == GL_SRGB_ALPHA ||
                internalFormat == GL_SRGB8_ALPHA8) {
                // XXX(RPR): sRGB formula is different from straight pow decoding, but it's the best we can do right now
                RPR_ERROR_CHECK(rprImageSetGamma(rprImage->GetHandle(), 2.2f), "Failed to set image gamma");
            }

            return rprImage;
        } else {
            TF_RUNTIME_ERROR("Failed to load image %s: unsupported format", path.c_str());
        }
    } catch (rpr::Error const& error) {
        TF_RUNTIME_ERROR("Failed to read image %s: %s", path.c_str(), error.what());
    }

    return nullptr;
}

void ImageCache::RequireGarbageCollection() {
    m_garbageCollectionRequired = true;
}

void ImageCache::GarbageCollectIfNeeded() {
    if (!m_garbageCollectionRequired) {
        return;
    }

    auto it = m_cache.begin();
    while (it != m_cache.end()) {
        if (it->second.handle.expired()) {
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }

    m_garbageCollectionRequired = false;
}

ImageCache::ImageMetadata::ImageMetadata(std::string const& path) {
    double time;
    if (!ArchGetModificationTime(path.c_str(), &time)) {
        return;
    }

    int64_t size = ArchGetFileLength(path.c_str());
    if (size == -1) {
        return;
    }

    m_modificationTime = time;
    m_size = static_cast<size_t>(size);
}

bool ImageCache::ImageMetadata::IsMetadataEqual(ImageMetadata const& md) {
    return m_modificationTime == md.m_modificationTime &&
        m_size == md.m_size;
}

PXR_NAMESPACE_CLOSE_SCOPE
