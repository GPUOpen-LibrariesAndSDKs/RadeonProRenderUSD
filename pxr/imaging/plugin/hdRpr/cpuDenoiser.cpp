// SPDX-License-Identifier: Apache-2.0
#include "cpuDenoiser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <vector>

#ifdef HDRPR_HAS_OIDN
#include <OpenImageDenoise/oidn.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

struct HdRprCpuDenoiser::Impl {
#ifdef HDRPR_HAS_OIDN
    OIDNDevice device = nullptr;
    OIDNFilter filter = nullptr;
    bool hasAlbedo = false;
    bool hasNormal = false;
    std::vector<float> color;
    std::vector<float> albedo;
    std::vector<float> normal;

    ~Impl() {
        if (filter) oidnReleaseFilter(filter);
        if (device) oidnReleaseDevice(device);
    }

    bool Check(std::string* error) {
        const char* message = nullptr;
        if (oidnGetDeviceError(device, &message) == OIDN_ERROR_NONE) return true;
        if (error) *error = message ? message : "Open Image Denoise error";
        return false;
    }

    bool Init(std::string* error) {
        if (filter) return true;
        if (!device) {
            device = oidnNewDevice(OIDN_DEVICE_TYPE_CPU);
            if (!device) {
                if (error) *error = "Cannot create Open Image Denoise CPU device";
                return false;
            }
            oidnCommitDevice(device);
            if (!Check(error)) {
                oidnReleaseDevice(device);
                device = nullptr;
                return false;
            }
        }
        filter = oidnNewFilter(device, "RT");
        if (!Check(error) || !filter) {
            if (filter) oidnReleaseFilter(filter);
            filter = nullptr;
            if (error && error->empty()) *error = "Cannot create Open Image Denoise RT filter";
            return false;
        }
        oidnSetFilterBool(filter, "hdr", true);
        oidnSetFilterBool(filter, "srgb", false);
        oidnSetFilterBool(filter, "cleanAux", false);
        if (!Check(error)) {
            oidnReleaseFilter(filter);
            filter = nullptr;
            return false;
        }
        return true;
    }

    void SetImage(const char* name, const float* pixels, size_t width, size_t height) {
        oidnSetSharedFilterImage(filter, name, const_cast<float*>(pixels),
                                OIDN_FORMAT_FLOAT3, width, height, 0,
                                4 * sizeof(float), width * 4 * sizeof(float));
    }
#endif
};

HdRprCpuDenoiser::HdRprCpuDenoiser() : m_impl(new Impl) {}
HdRprCpuDenoiser::~HdRprCpuDenoiser() = default;

bool HdRprCpuDenoiser::IsAvailable() {
#ifdef HDRPR_HAS_OIDN
    return true;
#else
    return false;
#endif
}

bool HdRprCpuDenoiser::Execute(const float* color, const float* albedo,
                             const float* normal, float* output,
                             size_t width, size_t height, std::string* error) {
    if (error) error->clear();
#ifdef HDRPR_HAS_OIDN
    if (!color || !output || !width || !height ||
        width > std::numeric_limits<size_t>::max() / height / 4 / sizeof(float)) {
        if (error) *error = "Invalid denoising buffer dimensions";
        return false;
    }
    const size_t count = width * height * 4;
    std::memcpy(output, color, count * sizeof(float));
    try {
        if (!m_impl->Init(error)) return false;
        m_impl->color.assign(color, color + count);
        // Keep the original render buffer untouched. HDR values above one are
        // retained; only negative reconstruction undershoot is clamped for OIDN.
        for (size_t i = 0; i < count; i += 4) {
            for (size_t channel = 0; channel < 3; ++channel) {
                if (!std::isfinite(color[i + channel])) {
                    if (error) *error = "Non-finite color in denoising input";
                    return false;
                }
                m_impl->color[i + channel] = std::max(0.0f, color[i + channel]);
            }
        }
        m_impl->SetImage("color", m_impl->color.data(), width, height);
        m_impl->SetImage("output", output, width, height);
        if (albedo) {
            m_impl->albedo.resize(count);
            for (size_t i = 0; i < count; i += 4) {
                for (size_t channel = 0; channel < 3; ++channel) {
                    const float value = albedo[i + channel];
                    m_impl->albedo[i + channel] = std::isfinite(value)
                        ? std::max(0.0f, std::min(1.0f, value)) : 0.0f;
                }
            }
            m_impl->SetImage("albedo", m_impl->albedo.data(), width, height);
        } else if (m_impl->hasAlbedo) {
            oidnUnsetFilterImage(m_impl->filter, "albedo");
        }
        // OIDN supports color alone, color+albedo, or color+albedo+normal.
        const bool useNormal = albedo && normal;
        if (useNormal) {
            m_impl->normal.resize(count);
            for (size_t i = 0; i < count; i += 4) {
                const bool background = normal[i] == 0.0f &&
                    normal[i + 1] == 0.0f && normal[i + 2] == 0.0f;
                for (size_t channel = 0; channel < 3; ++channel) {
                    const float value = normal[i + channel];
                    m_impl->normal[i + channel] = !background && std::isfinite(value)
                        ? 2.0f * std::max(0.0f, std::min(1.0f, value)) - 1.0f : 0.0f;
                }
            }
            m_impl->SetImage("normal", m_impl->normal.data(), width, height);
        } else if (m_impl->hasNormal) {
            oidnUnsetFilterImage(m_impl->filter, "normal");
        }
        m_impl->hasAlbedo = albedo != nullptr;
        m_impl->hasNormal = useNormal;
        oidnCommitFilter(m_impl->filter);
        if (!m_impl->Check(error)) return false;
        oidnExecuteFilter(m_impl->filter);
        if (!m_impl->Check(error)) return false;
        for (size_t i = 0; i < count; i += 4) {
            output[i + 3] = color[i + 3];
            for (size_t channel = 0; channel < 3; ++channel) {
                if (!std::isfinite(output[i + channel])) {
                    if (error) *error = "Non-finite denoising output";
                    return false;
                }
            }
        }
        return true;
    } catch (std::exception const& e) {
        if (error) *error = e.what();
        return false;
    }
#else
    (void)color; (void)albedo; (void)normal; (void)output; (void)width; (void)height;
    if (error) *error = "Open Image Denoise 2 was not available at build time";
    return false;
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE
