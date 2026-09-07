// SPDX-License-Identifier: Apache-2.0
#ifndef HDRPR_CPU_DENOISER_H
#define HDRPR_CPU_DENOISER_H

#include "pxr/pxr.h"

#include <cstddef>
#include <memory>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

// Optional, spatial-only Open Image Denoise support. The implementation always
// chooses the CPU so rendering on a different GPU does not change this backend.
class HdRprCpuDenoiser {
public:
    HdRprCpuDenoiser();
    ~HdRprCpuDenoiser();

    static bool IsAvailable();

    // All buffers contain width * height float4 pixels. Only RGB is filtered;
    // output alpha is copied from color. RPR normal guides are encoded in [0,1].
    // Guides are optional, and are treated as noisy (cleanAux=false).
    bool Execute(const float* color, const float* albedo, const float* normal,
                 float* output, size_t width, size_t height, std::string* error);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
