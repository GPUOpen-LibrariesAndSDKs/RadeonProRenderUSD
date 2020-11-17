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

#ifndef RIFCPP_CONTEXT_H
#define RIFCPP_CONTEXT_H

#include "rifImage.h"

#include <string>
#include <memory>

namespace rpr { class Context; }

PXR_NAMESPACE_OPEN_SCOPE

struct RprUsdContextMetadata;
class HdRprApiFramebuffer;

namespace rif {

class Context {
public:
    static std::unique_ptr<Context> Create(rpr::Context* rprContext, RprUsdContextMetadata const& rprContextMetadata, std::string const& modelPath);

    virtual ~Context();

    rif_image_filter CreateImageFilter(rif_image_filter_type type);

    std::unique_ptr<Image> CreateImage(rif_image_desc const& desc);
    virtual std::unique_ptr<Image> CreateImage(HdRprApiFramebuffer* rprFrameBuffer) = 0;

    void AttachFilter(rif_image_filter filter, rif_image inputImage, rif_image outputImage);
    void DetachFilter(rif_image_filter filter);

    virtual void UpdateInputImage(HdRprApiFramebuffer* rprFrameBuffer, rif_image image);

    void ExecuteCommandQueue();

    std::string const& GetModelPath() const { return m_modelPath; };

protected:
    Context(std::string const& modelPath);

protected:
    rif_context m_context = nullptr;
    rif_command_queue m_commandQueue = nullptr;

private:
    int m_numAttachedFilters = 0;
    std::string m_modelPath;
};

PXR_NAMESPACE_CLOSE_SCOPE

} // namespace rif

#endif // RIFCPP_CONTEXT_H
