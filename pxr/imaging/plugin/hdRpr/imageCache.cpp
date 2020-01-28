#include "imageCache.h"
#include "rpr/imageHelpers.h"

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

    auto image = std::shared_ptr<rpr::Image>(rpr::CreateImage(m_context, path.c_str()));
    if (image) {
        md.handle = image;
        m_cache.emplace(path, md);
    }
    return image;
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
