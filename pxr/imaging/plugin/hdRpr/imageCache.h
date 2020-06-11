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

#ifndef HDRPR_IMAGE_CACHE_H
#define HDRPR_IMAGE_CACHE_H

#include "pxr/pxr.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace rpr {

class Context;
class CoreImage;

} // namespace rpr

PXR_NAMESPACE_OPEN_SCOPE

class ImageCache {
public:
    ImageCache(rpr::Context* context);

    std::shared_ptr<rpr::CoreImage> GetImage(std::string const& path, bool forceLinearSpace = false);

    void RequireGarbageCollection();
    void GarbageCollectIfNeeded();

    rpr::Context* GetContext() { return m_context; }

private:
    class ImageMetadata {
    public:
        ImageMetadata() = default;
        ImageMetadata(std::string const& path);

        bool IsMetadataEqual(ImageMetadata const& md);

    public:
        std::weak_ptr<rpr::CoreImage> handle;

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
