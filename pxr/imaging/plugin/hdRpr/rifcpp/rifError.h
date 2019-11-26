#ifndef RIFCPP_EXCEPTION_H
#define RIFCPP_EXCEPTION_H

#include "pxr/base/arch/functionLite.h"
#include "pxr/base/tf/stringUtils.h"

#include <RadeonImageFilters.h>
#include <stdexcept>
#include <string>

#define RIF_ERROR_CHECK_THROW(status, msg) \
    do { \
        auto st = status; \
        if (st != RIF_SUCCESS) { \
            assert(false); \
            throw rif::Error(st, msg, __ARCH_FILE__, __ARCH_FUNCTION__, __LINE__); \
        } \
    } while(0);

#define RIF_ERROR_CHECK(status, msg) \
    rif::IsErrorCheck(status, msg, __ARCH_FILE__, __ARCH_FUNCTION__, __LINE__)

#define RIF_GET_ERROR_MESSAGE(status, msg) \
    rif::ConstructErrorMessage(status, msg, __ARCH_FILE__, __ARCH_FUNCTION__, __LINE__)

#define RIF_THROW_ERROR_MSG(msg) \
    throw rif::Error(RIF_GET_ERROR_MESSAGE(RIF_SUCCESS, msg));

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

inline std::string ConstructErrorMessage(rif_int errorStatus, std::string const& messageOnFail, char const* file, char const* function, size_t line) {
    auto rifErrorString = [errorStatus]() -> std::string {
        switch (errorStatus) {
            case RIF_ERROR_INVALID_API_VERSION: return "invalid api version";
            case RIF_ERROR_INVALID_PARAMETER: return "invalid parameter";
            case RIF_ERROR_UNSUPPORTED: return "unsupported";
            case RIF_ERROR_INTERNAL_ERROR: return "internal error";
            case RIF_ERROR_INVALID_CONTEXT: return "invalid context";
            default:
                break;
        }

        return "error code - " + std::to_string(errorStatus);
    };

    auto suffix = TfStringPrintf(" in %s at line %zu of %s", function, line, file);
#ifdef RPR_GIT_SHORT_HASH
    suffix += TfStringPrintf("(%s)", RPR_GIT_SHORT_HASH);
#endif // RPR_GIT_SHORT_HASH
    if (errorStatus == RIF_SUCCESS) {
        return TfStringPrintf("[RIF ERROR] %s%s", messageOnFail.c_str(), suffix.c_str());
    } else {
        auto errorStr = rifErrorString();
        return TfStringPrintf("[RIF ERROR] %s -- %s%s", messageOnFail.c_str(), errorStr.c_str(), suffix.c_str());
    }
}

inline bool IsErrorCheck(const rif_int status, const std::string& messageOnFail, char const* file, char const* function, size_t line) {
    if (RIF_SUCCESS == status) {
        return false;
    }

    auto errorMessage = ConstructErrorMessage(status, messageOnFail.c_str(), file, function, line);
    fprintf(stderr, "%s\n", errorMessage.c_str());
    return true;
}

class Error : public std::runtime_error {
public:
    Error(rif_int errorStatus, const char* messageOnFail, char const* file, char const* function, size_t line)
        : std::runtime_error(ConstructErrorMessage(errorStatus, messageOnFail, file, function, line)) {

    }

    Error(std::string const& errorMesssage)
        : std::runtime_error(errorMesssage) {

    }
};

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RIFCPP_EXCEPTION_H