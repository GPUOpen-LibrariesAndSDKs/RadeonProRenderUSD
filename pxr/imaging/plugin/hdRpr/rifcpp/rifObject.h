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

#ifndef RIFCPP_OBJECT_H
#define RIFCPP_OBJECT_H

#include "pxr/pxr.h"

#include <RadeonImageFilters.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

class Context;

class Object {
public:
    Object(void* objectHandle)
        : m_rifObjectHandle(objectHandle) {

    }

    Object() = default;
    Object(Object const&) = delete;
    Object& operator=(Object const&) = delete;

    void Delete() {
        if (m_rifObjectHandle) {
            rifObjectDelete(m_rifObjectHandle);
        }
        m_rifObjectHandle = nullptr;
    }

    ~Object() {
        Delete();
    }

protected:
    void* m_rifObjectHandle = nullptr;
};

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RIFCPP_OBJECT_H
