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

#include "renderDelegate.h"

#include "pxr/imaging/hd/instancer.h"
#include "pxr/imaging/hd/timeSampleArray.h"

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
        SdfPath const& id
        HDRPR_INSTANCER_ID_ARG_DECL) :
        HdInstancer(delegate, id HDRPR_INSTANCER_ID_ARG) {
    }

    HdTimeSampleArray<VtMatrix4dArray, 2> SampleInstanceTransforms(SdfPath const& prototypeId);

    void Sync(HdSceneDelegate *sceneDelegate,
              HdRenderParam   *renderParam,
              HdDirtyBits     *dirtyBits) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_INSTANCER_H
