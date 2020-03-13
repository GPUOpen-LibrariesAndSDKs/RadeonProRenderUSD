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

#ifndef RIFCPP_IMAGE_H
#define RIFCPP_IMAGE_H

#include "rifObject.h"
#include "pxr/imaging/hd/types.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

class Image : public Object {
public:
    static rif_image_desc GetDesc(uint32_t width, uint32_t height, HdFormat format);

    explicit Image(rif_image imageHandle);

    rif_image GetHandle() { return static_cast<rif_image>(m_rifObjectHandle); }
};

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE

#endif //  RIFCPP_IMAGE_H
