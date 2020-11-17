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

#ifndef PXR_IMAGING_RPR_USD_ERROR_H
#define PXR_IMAGING_RPR_USD_ERROR_H

#include "pxr/imaging/rprUsd/debugCodes.h"

#include "pxr/base/arch/functionLite.h"
#include "pxr/base/tf/stringUtils.h"

#include <RadeonProRender.hpp>
#include <stdexcept>
#include <cassert>
#include <string>

#define RPR_ERROR_CHECK_THROW(status, msg, ...) \
    do { \
        auto st = status; \
        if (st != RPR_SUCCESS) { \
            assert(false); \
            throw RprUsdError(st, msg, __ARCH_FILE__, __ARCH_FUNCTION__, __LINE__, ##__VA_ARGS__); \
        } \
    } while(0);

#define RPR_ERROR_CHECK(status, msg, ...) \
    RprUsdFailed(status, msg, __ARCH_FILE__, __ARCH_FUNCTION__, __LINE__, ##__VA_ARGS__)

#define RPR_GET_ERROR_MESSAGE(status, msg, ...) \
    RprUsdConstructErrorMessage(status, msg, __ARCH_FILE__, __ARCH_FUNCTION__, __LINE__, ##__VA_ARGS__)

#define RPR_THROW_ERROR_MSG(fmt, ...) \
    RprUsdThrowErrorMsg(__ARCH_FILE__, __ARCH_FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);

PXR_NAMESPACE_OPEN_SCOPE

inline std::string RprUsdConstructErrorMessage(rpr::Status errorStatus, std::string const& messageOnFail, char const* file, char const* function, size_t line, rpr::Context* context = nullptr) {
    auto rprErrorString = [errorStatus, context]() -> std::string {
        if (context) {
            size_t lastErrorMessageSize = 0;
            auto status = context->GetInfo(RPR_CONTEXT_LAST_ERROR_MESSAGE, 0, nullptr, &lastErrorMessageSize);
            if (status == RPR_SUCCESS && lastErrorMessageSize > 1) {
                std::string message(lastErrorMessageSize, '\0');
                status = context->GetInfo(RPR_CONTEXT_LAST_ERROR_MESSAGE, message.size(), &message[0], nullptr);
                if (status == RPR_SUCCESS) {
                    return message;
                }
            }
        }

        switch (errorStatus) {
            case RPR_ERROR_INVALID_API_VERSION: return "invalid api version";
            case RPR_ERROR_INVALID_PARAMETER: return "invalid parameter";
            case RPR_ERROR_UNSUPPORTED: return "unsupported";
            case RPR_ERROR_INTERNAL_ERROR: return "internal error";
            case RPR_ERROR_INVALID_CONTEXT: return "invalid context";
            default:
                break;
        }

        return "error code - " + std::to_string(errorStatus);
    };

    auto suffix = TfStringPrintf(" in %s at line %zu of %s", function, line, file);
#ifdef RPR_GIT_SHORT_HASH
    suffix += TfStringPrintf("(%s)", RPR_GIT_SHORT_HASH);
#endif // RPR_GIT_SHORT_HASH
    if (errorStatus == RPR_SUCCESS) {
        return TfStringPrintf("[RPR ERROR] %s%s", messageOnFail.c_str(), suffix.c_str());
    } else {
        auto errorStr = rprErrorString();
        return TfStringPrintf("[RPR ERROR] %s -- %s%s", messageOnFail.c_str(), errorStr.c_str(), suffix.c_str());
    }
}

inline bool RprUsdFailed(rpr::Status status, const char* messageOnFail, char const* file, char const* function, size_t line, rpr::Context* context = nullptr) {
    if (RPR_SUCCESS == status) {
        return false;
    }
    if ((status == RPR_ERROR_UNSUPPORTED || status == RPR_ERROR_UNIMPLEMENTED) && !TfDebug::IsEnabled(RPR_USD_DEBUG_CORE_UNSUPPORTED_ERROR)) {
        return true;
    }

    auto errorMessage = RprUsdConstructErrorMessage(status, messageOnFail, file, function, line, context);
    fprintf(stderr, "%s\n", errorMessage.c_str());
    return true;
}

class RprUsdError : public std::runtime_error {
public:
    RprUsdError(rpr::Status errorStatus, const char* messageOnFail, char const* file, char const* function, size_t line, rpr::Context* context = nullptr)
        : std::runtime_error(RprUsdConstructErrorMessage(errorStatus, messageOnFail, file, function, line, context)) {

    }

    RprUsdError(std::string const& errorMesssage)
        : std::runtime_error(errorMesssage) {

    }
};

inline void RprUsdThrowErrorMsg(char const* file, char const* function, size_t line, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto messageOnFail = TfVStringPrintf(fmt, ap);
    va_end(ap);
    throw RprUsdError(RprUsdConstructErrorMessage(RPR_SUCCESS, messageOnFail, file, function, line));
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_RPR_USD_ERROR_H
