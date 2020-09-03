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
#include "pxr/imaging/rprUsd/imageCache.h"
#include "pxr/imaging/rprUsd/coreImage.h"
#include "pxr/imaging/rprUsd/helpers.h"
#include "pxr/imaging/rprUsd/util.h"
#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/tf/diagnostic.h"

PXR_NAMESPACE_OPEN_SCOPE

template <typename T>
size_t GetHash(T const& value) {
    return std::hash<T>{}(value);
}

bool RprUsdImageCache::InitCacheKey(
    std::string const& path,
    std::vector<RprUsdCoreImage::UDIMTile> const& tiles,
    CacheKey* key) {

    key->path = path;
    key->hash = GetHash(path);

    auto processTile = [key](std::string const& path) {
        double modificationTime;
        int64_t sizeInt = ArchGetFileLength(path.c_str());
        if (sizeInt == -1 || !ArchGetModificationTime(path.c_str(), &modificationTime)) {
            // If the path points to a non-filesystem image (e.g. usdz embedded image)
            // we rely on the user of the Hydra to correctly reload all materials that use this image
            return;
        }

        key->tiles.emplace_back(size_t(sizeInt), modificationTime);
        key->hash ^= size_t(sizeInt) ^ GetHash(modificationTime);
    };

    if (tiles.size() != 1 || tiles[0].id != 0) {
        // UDIM tiles
        std::string formatString;
        if (!RprUsdGetUDIMFormatString(path, &formatString)) {
            return false;
        }

        for (auto& tile : tiles) {
            processTile(TfStringPrintf(formatString.c_str(), tile.id));
        }
    } else {
        processTile(path);
    }

    return true;
}

bool RprUsdImageCache::InitCacheEntryDesc(
    GlfUVTextureData* data,
    rpr::ImageWrapType wrapType,
    bool forceLinearSpace,
    CacheEntry::Desc* desc) {
    if (!data) return false;

    auto internalFormat = data->GLInternalFormat();
    if (!forceLinearSpace &&
        (internalFormat == GL_SRGB ||
        internalFormat == GL_SRGB8 ||
        internalFormat == GL_SRGB_ALPHA ||
        internalFormat == GL_SRGB8_ALPHA8)) {
        // XXX(RPR): sRGB formula is different from straight pow decoding, but it's the best we can do right now
        desc->gamma = 2.2f;
    } else {
        desc->gamma = 1.0f;
    }

    if (!wrapType) {
        wrapType = RPR_IMAGE_WRAP_TYPE_REPEAT;
    }
    desc->wrapType = wrapType;

    return true;
}

RprUsdImageCache::RprUsdImageCache(rpr::Context* context)
    : m_context(context) {

}

std::shared_ptr<RprUsdCoreImage>
RprUsdImageCache::GetImage(
    std::string const& path,
    bool forceLinearSpace,
    rpr::ImageWrapType wrapType,
    std::vector<RprUsdCoreImage::UDIMTile> const& tiles) {
    if (tiles.empty()) {
        return nullptr;
    }

    CacheKey key;
    CacheEntry::Desc desc;
    if (!InitCacheKey(path, tiles, &key) ||
        !InitCacheEntryDesc(tiles[0].textureData, wrapType, forceLinearSpace, &desc)) {
        return nullptr;
    }

    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        auto& entries = it->second;
        for (size_t i = 0; i < entries.size(); ++i) {
            auto& entry = entries[i];
            if (entry.desc == desc) {
                if (auto image = entry.handle.lock()) {
                    return image;
                } else {
                    TF_CODING_ERROR("Expired cache entry was not removed");

                    if (i + 1 != entries.size()) {
                        std::swap(entry, entries.back());
                    }
                    entries.pop_back();

                    break;
                }
            }
        }
    }

    auto coreImage = RprUsdCoreImage::Create(m_context, tiles);
    if (!coreImage) {
        return nullptr;
    }

    if (RprUsdIsLeakCheckEnabled()) {
        coreImage->SetName(path.c_str());
    }

    RPR_ERROR_CHECK(coreImage->SetGamma(desc.gamma), "Failed to set image gamma");
    RPR_ERROR_CHECK(coreImage->SetWrap(desc.wrapType), "Failed to set image wrap type");

    if (it == m_cache.end()) {
        it = m_cache.emplace(key, CacheValue()).first;
    }

    std::shared_ptr<RprUsdCoreImage> cachedImage(coreImage,
        [this, key, desc](RprUsdCoreImage* coreImage) {
            delete coreImage;
            PopCacheEntry(key, desc);
        }
    );

    CacheEntry cacheEntry;
    cacheEntry.desc = desc;
    cacheEntry.handle = cachedImage;
    it->second.push_back(std::move(cacheEntry));

    return cachedImage;
}

void RprUsdImageCache::PopCacheEntry(
    CacheKey const& key,
    CacheEntry::Desc const& desc) {

    auto it = m_cache.find(key);
    if (it == m_cache.end()) {
        TF_CODING_ERROR("Failed to release cache entry: no entry with such key - path=%s", key.path.c_str());
        return;
    }

    auto& entries = it->second;

    for (size_t i = 0; i < entries.size(); ++i) {
        auto& entry = entries[i];
        if (entry.desc == desc) {
            if (entries.size() == 1) {
                // if this is the latest entry, remove whole key-value pair
                m_cache.erase(it);
            } else {
                // avoid removing from the middle by swapping with the end element
                if (i + 1 != entries.size()) {
                    std::swap(entry, entries.back());
                }
                entries.pop_back();
            }
            return;
        }
    }

    TF_CODING_ERROR("Failed to release cache entry: no entry with such desc - path=%s, gamma=%g, wrapType=%u",
        key.path.c_str(), desc.gamma, desc.wrapType);
}

bool RprUsdImageCache::CacheKey::Equal::operator()(CacheKey const& lhs, CacheKey const& rhs) const {
    return lhs.tiles == rhs.tiles &&
           lhs.path == rhs.path;
}

bool RprUsdImageCache::CacheEntry::Desc::operator==(Desc const& rhs) const {
    constexpr static float kGammaEpsilon = 0.1f;
    return wrapType == rhs.wrapType &&
           std::abs(gamma - rhs.gamma) < kGammaEpsilon;
}

PXR_NAMESPACE_CLOSE_SCOPE
