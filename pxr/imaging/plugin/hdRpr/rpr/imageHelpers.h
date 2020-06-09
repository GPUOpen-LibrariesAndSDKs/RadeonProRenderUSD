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

#ifndef HDRPR_CORE_IMAGE_HELPERS_H
#define HDRPR_CORE_IMAGE_HELPERS_H

#include <RadeonProRender.hpp>

#include <vector>

namespace rpr {

class CoreImage {
public:
    static CoreImage* Create(Context* context, char const* path, bool forceLinearSpace = false);
    static CoreImage* Create(Context* context, uint32_t width, uint32_t height, ImageFormat format, void const* data, rpr::Status* status = nullptr);

    ~CoreImage();

    rpr::Image* GetRootImage() { return m_rootImage; }
    ImageFormat GetFormat(CoreImage const& image);
    ImageDesc GetDesc(CoreImage const& image);
    Status GetInfo(ImageInfo imageInfo, size_t size, void* data, size_t* size_ret);

    Status SetWrap(ImageWrapType type);
    Status SetGamma(float gamma);
    Status SetMipmapEnabled(bool enabled);
    Status SetFilter(ImageFilterType type);

private:
    CoreImage(Image* rootImage = nullptr) : m_rootImage(rootImage) {};
    Image* GetBaseImage();

    template <typename F>
    Status ForEachImage(F f);

private:
    Image* m_rootImage = nullptr;
    std::vector<Image*> m_subImages;
};

} // namespace rpr

#endif // HDRPR_CORE_IMAGE_HELPERS_H
