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

#ifndef RPRUSD_IMAGE_CACHE_H
#define RPRUSD_IMAGE_CACHE_H

#include "pxr/imaging/rprUsd/api.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace rpr { class Context; }

PXR_NAMESPACE_OPEN_SCOPE

class RprUsdCoreImage;

class RprUsdImageCache {
public:
    RPRUSD_API
    RprUsdImageCache(rpr::Context* context);

    class CachedImage {
    public:
        CachedImage(RprUsdImageCache*, std::unique_ptr<RprUsdCoreImage>);
        ~CachedImage();

        RPRUSD_API
        RprUsdCoreImage* Get() const { return m_coreImage.get(); }

        RPRUSD_API
        RprUsdCoreImage* operator->() const { return Get(); }

        void SetCachePath(std::string const* path);

    private:
        RprUsdImageCache* m_imageCache;
        std::string const* m_cachePaths[2] = {};
        std::unique_ptr<RprUsdCoreImage> m_coreImage;
    };

    RPRUSD_API
    std::shared_ptr<CachedImage> GetImage(std::string const& path, bool forceLinearSpace = false);

private:
    void RemoveCacheEntry(std::string const& cachePath);

    class ImageMetadata {
    public:
        ImageMetadata() = default;
        ImageMetadata(std::string const& path);

        bool IsMetadataEqual(ImageMetadata const& md);

    public:
        std::weak_ptr<CachedImage> handle;

    private:
        size_t m_size = 0u;
        double m_modificationTime = 0.0;
    };

private:
    rpr::Context* m_context;
    std::unordered_map<std::string, ImageMetadata> m_cache;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_IMAGE_CACHE_H
