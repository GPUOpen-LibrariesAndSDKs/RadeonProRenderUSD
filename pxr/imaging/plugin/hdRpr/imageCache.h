#ifndef HDRPR_IMAGE_CACHE_H
#define HDRPR_IMAGE_CACHE_H

#include "pxr/pxr.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace rpr {

class Context;
class Image;

} // namespace rpr

PXR_NAMESPACE_OPEN_SCOPE

class ImageCache {
public:
    ImageCache(rpr::Context* context);

    std::shared_ptr<rpr::Image> GetImage(std::string const& path);

    void RequireGarbageCollection();
    void GarbageCollectIfNeeded();

    rpr::Context* GetContext() { return m_context; }

private:
    std::shared_ptr<rpr::Image> CreateImage(std::string const& path);

    class ImageMetadata {
    public:
        ImageMetadata() = default;
        ImageMetadata(std::string const& path);

        bool IsMetadataEqual(ImageMetadata const& md);

    public:
        std::weak_ptr<rpr::Image> handle;

    private:
        size_t m_size = 0u;
        double m_modificationTime = 0.0;
    };

private:
    rpr::Context* m_context;
    std::unordered_map<std::string, ImageMetadata> m_cache;
    bool m_garbageCollectionRequired = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_IMAGE_CACHE_H
