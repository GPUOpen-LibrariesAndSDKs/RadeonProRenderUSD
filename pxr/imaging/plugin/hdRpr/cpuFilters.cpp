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

#include "cpuFilters.h"
#include "pxr/base/work/loops.h"

PXR_NAMESPACE_OPEN_SCOPE

void CpuRemapFilter(float* src, float* dest, size_t length, float srcLo, float srcHi, float dstLo, float dstHi) {
    WorkParallelForN(length,
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            dest[i] = ((src[i] - srcLo) / (srcHi - srcLo)) * (dstHi - dstLo) + dstLo;
        }});
}

void CpuVec4toVec3Filter(GfVec4f* src, GfVec3f* dest, size_t numPixels) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            dest[i][0] = src[i][0];
            dest[i][1] = src[i][1];
            dest[i][2] = src[i][2];
        }
    });
}

void CpuVec4toFloatFilter(GfVec4f* src, float* dest, size_t numPixels) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            dest[i] = src[i][0];
        }
    });
}

void CpuVec4toInt32Filter(GfVec4f* src, int32_t* dest, size_t numPixels) {
    char* destAsChar = (char*)dest;
    float* srcAsFloat = (float*)src;
    WorkParallelForN(numPixels * sizeof(int32_t),   // output as char, input as GfVec4f
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            if (i % 4 == 3)
            {
                destAsChar[i] = 0;
            }
            else
            {
                destAsChar[i] = (char)(srcAsFloat[i] * 255 + 0.5f);
            }
        }
    });

    int32_t* destAsInt = (int32_t*)dest;
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            destAsInt[i] -= 1;
        }
    });
}

void CpuFloatToInt32Filter(float* src, int32_t* dest, size_t length) {
    // RPR store integer ID values to RGB images using such formula:
    // c[i].x = i;
    // c[i].y = i/256;
    // c[i].z = i/(256*256);
    // i.e. saving little endian int24 to uchar3
    // That's why we interpret the value as int and filling the alpha channel with zeros
    int32_t* srcAsInt = (int32_t*)src;
    WorkParallelForN(length,
        [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            dest[i] = (srcAsInt[i] & 0xFFFFFF) - 1;
        }
    });
}

void CpuNdcFilter(GfVec4f* src, GfVec4f* dest, size_t numPixels, const GfMatrix4f& viewProjectionMatrix) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            float norm = std::max(src[i][3], 1.0f);
            GfVec4f pos(src[i][0] / norm, src[i][1] / norm, src[i][2] / norm, 1.0f);
            GfVec4f posResult = viewProjectionMatrix * pos;
            float depth = posResult[2] / posResult[3];
            dest[i][0] = depth;
            dest[i][1] = depth;
            dest[i][2] = depth;
            dest[i][3] = 1.0f;
        }
    });
}

void CpuOpacityFilter(GfVec4f* opacity, GfVec4f* srcdest, size_t numPixels) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            float op = opacity[i][0];
            srcdest[i][0] *= op;
            srcdest[i][1] *= op;
            srcdest[i][2] *= op;
            srcdest[i][3] = op;
        }
    });
}

void CpuOpacityMaskFilter(GfVec4f* opacity, GfVec4f* srcdest, size_t numPixels) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            if (opacity[i][0] == 0.0f) {
                srcdest[i][0] = 1.0f;
                srcdest[i][1] = 1.0f;
                srcdest[i][1] = 1.0f;
                srcdest[i][1] = 1.0f;
            }
        }
    });
}

void CpuFillMaskFilter(GfVec4f* srcdest, size_t numPixels) {
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            unsigned int idDecoded = (unsigned int)(srcdest[i][0] * 256) + (unsigned int)(srcdest[i][1] * 256 * 256) + (unsigned int)(srcdest[i][2] * 256 * 256 * 256);
            if (idDecoded) {
                unsigned int v0 = 0x123;
                unsigned int v1 = idDecoded;
                unsigned int s0 = 0;
                const unsigned int N = 4;
                for (unsigned int n = 0; n < N; n++) {
                    s0 += 0x9e3779b9;
                    v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
                    v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
                }
                srcdest[i][0] = (v0 & 0xFFFF) / (float)(0xFFFF);
                srcdest[i][1] = (v0 >> 16) / (float)(0xFFFF);
                srcdest[i][2] = (v1 & 0xFFFF) / (float)(0xFFFF);
                srcdest[i][3] = 1.0f;
            }
            else {
                srcdest[i][0] = srcdest[i][1] = srcdest[i][2] = srcdest[i][3] = 0;
            }
        }
    });
}

void CpuResampleNearest(GfVec4f* src, size_t srcWidth, size_t srcHeight, GfVec4f* dest, size_t destWidth, size_t destHeight) {
    if (destWidth <= 1 || destHeight <= 1) {
        return;
    }

    float xratio = 1.0f * (srcWidth - 1.0f) / (destWidth - 1.0f);
    float yratio = 1.0f * (srcHeight - 1.0f) / (destHeight - 1.0f);

    WorkParallelForN(destHeight,
        [&](size_t begin, size_t end) {
        for (int y = begin; y < end; ++y) {
            for (int x = 0; x < destWidth; ++x) {
                int cx = xratio * x;
                int cy = yratio * y;
                dest[(y * destWidth + x)][0] = src[(cy * srcWidth + cx)][0];
                dest[(y * destWidth + x)][1] = src[(cy * srcWidth + cx)][1];
                dest[(y * destWidth + x)][2] = src[(cy * srcWidth + cx)][2];
                dest[(y * destWidth + x)][3] = src[(cy * srcWidth + cx)][3];
            }
        }});
}

void CpuGammaCorrection(GfVec4f* srcdest, size_t numPixels, float gamma) {
    if (gamma == 0) {
        return;
    }
    float _1_g = 1 / gamma;
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            srcdest[i][0] = powf(srcdest[i][0], _1_g);
            srcdest[i][1] = powf(srcdest[i][1], _1_g);
            srcdest[i][2] = powf(srcdest[i][2], _1_g);
            // skiping alpha
        }
    });
}

void CpuTonemap(GfVec4f* srcdest, size_t numPixels, float gamma, float exposureTime, float sensitivity, float fstop) {
    if (gamma == 0 || fstop == 0) {
        return;
    }
    float h = (0.65f * 21.61f * sensitivity * exposureTime) / (fstop * fstop);
    float _1_g = 1 / gamma;
    WorkParallelForN(numPixels,
        [&](size_t begin, size_t end) {
        for (int i = begin; i < end; ++i) {
            srcdest[i][0] = powf(srcdest[i][0] * h, _1_g);
            srcdest[i][1] = powf(srcdest[i][1] * h, _1_g);
            srcdest[i][2] = powf(srcdest[i][2] * h, _1_g);
            // skiping alpha
        }
    });
}

PXR_NAMESPACE_CLOSE_SCOPE
