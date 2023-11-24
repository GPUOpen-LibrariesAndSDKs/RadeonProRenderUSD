/************************************************************************
Copyright 2023 Advanced Micro Devices, Inc
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

#ifndef HDRPR_CPU_FILTERS_H
#define HDRPR_CPU_FILTERS_H

#include "pxr/base/gf/matrix4f.h"

PXR_NAMESPACE_OPEN_SCOPE

void CpuRemapFilter(float* src, float* dest, size_t length, float srcLo, float srcHi, float dstLo, float dstHi);
void CpuVec4toVec3Filter(GfVec4f* src, GfVec3f* dest, size_t numPixels);
void CpuVec4toFloatFilter(GfVec4f* src, float* dest, size_t numPixels);
void CpuVec4toInt32Filter(GfVec4f* src, int32_t* dest, size_t numPixels);
void CpuFloatToInt32Filter(float* src, int32_t* dest, size_t length);
void CpuNdcFilter(GfVec4f* src, GfVec4f* dest, size_t numPixels, const GfMatrix4f& viewProjectionMatrix);
void CpuOpacityFilter(GfVec4f* opacity, GfVec4f* srcdest, size_t numPixels);
void CpuOpacityMaskFilter(GfVec4f* opacity, GfVec4f* srcdest, size_t numPixels);
void CpuFillMaskFilter(GfVec4f* srcdest, size_t numPixels);
void CpuResampleNearest(GfVec4f* src, size_t srcWidth, size_t srcHeight, GfVec4f* dest, size_t destWidth, size_t destHeight);
void CpuGammaCorrection(GfVec4f* srcdest, size_t numPixels, float gamma);
void CpuTonemap(GfVec4f* srcdest, size_t numPixels, float gamma, float exposureTime, float sensitivity, float fstop);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_CPU_FILTERS_H
