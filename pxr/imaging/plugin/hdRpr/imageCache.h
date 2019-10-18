#ifndef HDRPR_IMAGE_CACHE_H
#define HDRPR_IMAGE_CACHE_H

#include "pxr/pxr.h"
#include "rprcpp/rprImage.h"

#include <memory>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

class Context;

} // namespace rpr

class ImageCache {
public:
    ImageCache(rpr::Context* context);

    std::shared_ptr<rpr::Image> GetImage(std::string const& path);

private:
    std::shared_ptr<rpr::Image> CreateImage(std::string const& path);

private:
    rpr::Context* m_context;
    std::unordered_map<std::string, std::weak_ptr<rpr::Image>> m_cache;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_IMAGE_CACHE_H
