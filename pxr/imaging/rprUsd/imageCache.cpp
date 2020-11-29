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

double GetModificationTime(std::string const& path) {
    double modificationTime = 0.0;
    ArchGetModificationTime(path.c_str(), &modificationTime);
    return modificationTime;
}

RprUsdImageCache::RprUsdImageCache(rpr::Context* context)
    : m_context(context) {

}

std::shared_ptr<RprUsdCoreImage>
RprUsdImageCache::GetImage(
    std::string const& path,
    std::string const& colorspace,
    rpr::ImageWrapType wrapType,
    std::vector<RprUsdCoreImage::UDIMTile> const& tiles,
    uint32_t numComponentsRequired) {
    if (!wrapType) {
        wrapType = RPR_IMAGE_WRAP_TYPE_REPEAT;
    }

    CacheKey key = {};
    key.path = path;
    key.colorspace = colorspace;
    key.wrapType = wrapType;
    key.hash = GetHash(path) ^ GetHash(colorspace) ^ GetHash(wrapType);

    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        CacheValue& cacheValue = it->second;
        if (auto image = cacheValue.Lock(key)) {
            return image;
        } else {
            m_cache.erase(it);
            it = m_cache.end();
        }
    }

    auto coreImage = RprUsdCoreImage::Create(m_context, tiles, numComponentsRequired);
    if (!coreImage) {
        return nullptr;
    }

    if (RprUsdIsLeakCheckEnabled()) {
        coreImage->SetName(path.c_str());
    }

    float gamma = 1.0f;
    if (key.colorspace == "srgb") {
        gamma = 2.2f;
    } else if (key.colorspace.empty()) {
        // Figure out gamma from the internal format.
        // Assume that all tiles have the same colorspace
        //
        auto data = tiles[0].textureData;
#if PXR_VERSION >= 2011
        GLenum internalFormat = GlfGetGLInternalFormat(data->GetHioFormat());
#else
        GLenum internalFormat = data->GLInternalFormat();
#endif
        if (internalFormat == GL_SRGB ||
            internalFormat == GL_SRGB8 ||
            internalFormat == GL_SRGB_ALPHA ||
            internalFormat == GL_SRGB8_ALPHA8) {
            // XXX(RPR): sRGB formula is different from straight pow decoding, but it's the best we can do without OCIO
            gamma = 2.2f;
        } else {
            gamma = 1.0f;
        }
    }

    RPR_ERROR_CHECK(coreImage->SetGamma(gamma), "Failed to set image gamma");
    RPR_ERROR_CHECK(coreImage->SetWrap(key.wrapType), "Failed to set image wrap type");

    CacheValue cacheValue;
    if (tiles.size() != 1 || tiles[0].id != 0) {
        // UDIM tiles
        std::string formatString;
        if (!RprUsdGetUDIMFormatString(path, &formatString)) {
            return nullptr;
        }

        cacheValue.tileModificationTimes.reserve(tiles.size());
        for (auto& tile : tiles) {
            auto tilePath = TfStringPrintf(formatString.c_str(), tile.id);
            cacheValue.tileModificationTimes.emplace_back(tile.id, GetModificationTime(tilePath));
        }
    } else {
        cacheValue.tileModificationTimes.emplace_back(0, GetModificationTime(path));
    }

    std::shared_ptr<RprUsdCoreImage> cachedImage(coreImage,
        [this, key](RprUsdCoreImage* coreImage) {
            delete coreImage;
            m_cache.erase(key);
        }
    );
    cacheValue.handle = cachedImage;

    it = m_cache.emplace(std::move(key), std::move(cacheValue)).first;

    return cachedImage;
}

std::shared_ptr<RprUsdCoreImage> RprUsdImageCache::CacheValue::Lock(CacheKey const& key) const {
    auto image = handle.lock();
    if (!image) {
        return nullptr;
    }

    std::string udimFormatString;

    // Check if image files were not changed
    //
    for (auto& entry : tileModificationTimes) {
        double modificationTime = entry.second;
        if (modificationTime == 0.0) {
            // If the path points to a non-filesystem image (e.g. usdz embedded image)
            // we rely on the user of the Hydra to correctly reload all materials that use this image
            //
            continue;
        }

        double currentModificationTime;

        uint32_t tileId = entry.first;
        if (tileId == 0) {
            currentModificationTime = GetModificationTime(key.path);
        } else {
            if (udimFormatString.empty()) {
                RprUsdGetUDIMFormatString(key.path, &udimFormatString);
            }

            auto tilePath = TfStringPrintf(udimFormatString.c_str(), tileId);
            currentModificationTime = GetModificationTime(tilePath);
        }

        if (modificationTime != currentModificationTime) {
            return nullptr;
        }
    }

    return image;

}

PXR_NAMESPACE_CLOSE_SCOPE
