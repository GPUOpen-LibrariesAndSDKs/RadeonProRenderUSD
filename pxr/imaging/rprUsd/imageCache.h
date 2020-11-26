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

#include "pxr/imaging/rprUsd/coreImage.h"

#include <memory>
#include <string>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

class RprUsdImageCache {
public:
    RPRUSD_API
    RprUsdImageCache(rpr::Context* context);

    RPRUSD_API
    std::shared_ptr<RprUsdCoreImage> GetImage(
        std::string const& path,
        std::string const& colorspace,
        rpr::ImageWrapType wrapType,
        std::vector<RprUsdCoreImage::UDIMTile> const& data,
        uint32_t numComponentsRequired);

private:
    rpr::Context* m_context;

    struct CacheKey {
        std::string path;
        std::string colorspace;
        rpr::ImageWrapType wrapType;

        bool operator==(CacheKey const& rhs) const {
            return wrapType == rhs.wrapType && colorspace == rhs.colorspace && path == rhs.path;
        }

        size_t hash;
        struct Hash { size_t operator()(CacheKey const& key) const { return key.hash; }; };
    };

    struct CacheValue {
        std::vector<std::pair<uint32_t, double>> tileModificationTimes;
        std::weak_ptr<RprUsdCoreImage> handle;

        // Convenience wrapper over std::weak_ptr::lock that includes checking if image files are outdated
        std::shared_ptr<RprUsdCoreImage> Lock(CacheKey const& key) const;
    };

    std::unordered_map<CacheKey, CacheValue, CacheKey::Hash> m_cache;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_IMAGE_CACHE_H
