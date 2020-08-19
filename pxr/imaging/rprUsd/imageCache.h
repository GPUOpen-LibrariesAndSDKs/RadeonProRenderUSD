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
        bool forceLinearSpace,
        rpr::ImageWrapType wrapType,
        std::vector<RprUsdCoreImage::UDIMTile> const& data);

private:
    rpr::Context* m_context;

    struct CacheKey {
        std::string path;
        struct TileMetadata {
            size_t size;
            double modificationTime;

            TileMetadata(size_t size, double modificationTime)
                : size(size), modificationTime(modificationTime) {
            }
            bool operator==(TileMetadata const& rhs) const {
                return size == rhs.size && modificationTime == rhs.modificationTime;
            }
        };
        std::vector<TileMetadata> tiles;

        size_t hash;

        struct Equal { bool operator()(CacheKey const& lhs, CacheKey const& rhs) const; };
        struct Hash { size_t operator()(CacheKey const& key) const { return key.hash; }; };
    };
    bool InitCacheKey(std::string const& path, std::vector<RprUsdCoreImage::UDIMTile> const&, CacheKey*);

    struct CacheEntry {
        struct Desc {
            float gamma;
            rpr::ImageWrapType wrapType;

            bool operator==(Desc const& rhs) const;
        };
        Desc desc;
        std::weak_ptr<RprUsdCoreImage> handle;
    };
    bool InitCacheEntryDesc(GlfUVTextureData*, rpr::ImageWrapType, bool forceLinearSpace, CacheEntry::Desc*);

    using CacheValue = std::vector<CacheEntry>;
    std::unordered_map<CacheKey, CacheValue, CacheKey::Hash, CacheKey::Equal> m_cache;

private:
    void PopCacheEntry(CacheKey const& key, CacheEntry::Desc const& desc);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_IMAGE_CACHE_H
