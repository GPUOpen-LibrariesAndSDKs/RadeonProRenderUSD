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

#include "pxr/imaging/rprUsd/imageCache.h"
#include "pxr/imaging/rprUsd/coreImage.h"
#include "pxr/imaging/rprUsd/helpers.h"
#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/tf/diagnostic.h"

PXR_NAMESPACE_OPEN_SCOPE

RprUsdImageCache::RprUsdImageCache(rpr::Context* context)
    : m_context(context) {

}

std::shared_ptr<RprUsdImageCache::CachedImage>
RprUsdImageCache::GetImage(
    std::string const& path, bool forceLinearSpace) {
    ImageMetadata md(path);

    auto cacheKey = path;

    static const char* kForceLinearSpaceCacheKeySuffix = "?l";
    if (forceLinearSpace) {
        cacheKey += kForceLinearSpaceCacheKeySuffix;
    }

    auto it = m_cache.find(cacheKey);
    if (it != m_cache.end() && it->second.IsMetadataEqual(md)) {
        if (auto image = it->second.handle.lock()) {
            return image;
        }
    }

    auto coreImage = std::unique_ptr<RprUsdCoreImage>(RprUsdCoreImage::Create(m_context, path.c_str(), forceLinearSpace));
    if (!coreImage) {
        return nullptr;
    }

    auto cachedImage = std::make_shared<CachedImage>(this, std::move(coreImage));

    md.handle = cachedImage;
    auto status = m_cache.emplace(cacheKey, md);
    cachedImage->SetCachePath(&status.first->first);

    {
        auto gammaFromFile = RprUsdGetInfo<float>(cachedImage->Get(), RPR_IMAGE_GAMMA_FROM_FILE);
        if (std::abs(gammaFromFile - 1.0f) < 0.01f) {
            // Image is in linear space, we can cache the same image for both variants of forceLinearSpace
            if (forceLinearSpace) {
                status = m_cache.emplace(path, md);
            } else {
                status = m_cache.emplace(path + kForceLinearSpaceCacheKeySuffix, md);
            }
            cachedImage->SetCachePath(&status.first->first);
        }
    }

    return cachedImage;
}

void RprUsdImageCache::RemoveCacheEntry(std::string const& cachePath) {
    m_cache.erase(cachePath);
}

RprUsdImageCache::ImageMetadata::ImageMetadata(std::string const& path) {
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

bool RprUsdImageCache::ImageMetadata::IsMetadataEqual(ImageMetadata const& md) {
    return m_modificationTime == md.m_modificationTime &&
        m_size == md.m_size;
}

RprUsdImageCache::CachedImage::CachedImage(
    RprUsdImageCache* imageCache,
    std::unique_ptr<RprUsdCoreImage> coreImage)
    : m_imageCache(imageCache)
    , m_coreImage(std::move(coreImage)) {

}

RprUsdImageCache::CachedImage::~CachedImage() {
    for (auto path : m_cachePaths) {
        if (path) {
            m_imageCache->RemoveCacheEntry(*path);
        }
    }
}

void RprUsdImageCache::CachedImage::SetCachePath(std::string const* path) {
    if (!m_cachePaths[0]) {
        m_cachePaths[0] = path;
    } else if (!m_cachePaths[1]) {
        m_cachePaths[1] = path;
    } else {
        TF_CODING_ERROR("Unexpected amount of cache paths");
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
