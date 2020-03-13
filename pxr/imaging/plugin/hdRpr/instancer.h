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

#ifndef HDRPR_INSTANCER_H
#define HDRPR_INSTANCER_H

#include "pxr/imaging/hd/instancer.h"

#include "pxr/base/vt/array.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/matrix4d.h"

#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;

class HdRprInstancer : public HdInstancer {
public:
    HdRprInstancer(
        HdSceneDelegate* delegate,
        SdfPath const& id,
        SdfPath const& parentInstancerId) :
        HdInstancer(delegate, id, parentInstancerId) {
    }

    VtMatrix4dArray ComputeTransforms(SdfPath const& prototypeId);

private:
    void Sync();

    VtMatrix4dArray m_transform;
    VtVec3fArray m_translate;
    VtVec4fArray m_rotate;
    VtVec3fArray m_scale;

    std::mutex m_syncMutex;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_INSTANCER_H
