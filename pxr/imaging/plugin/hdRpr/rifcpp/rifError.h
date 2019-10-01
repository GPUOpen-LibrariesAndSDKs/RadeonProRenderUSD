#ifndef RIFCPP_EXCEPTION_H
#define RIFCPP_EXCEPTION_H

#include <RadeonImageFilters.h>
#include <pxr/base/arch/functionLite.h>
#include <stdexcept>
#include <string>

#define RIF_ERROR_CHECK_THROW(status, msg) \
    do { \
        auto st = status; \
        if (st != RIF_SUCCESS) { \
            assert(false); \
            throw rif::Error(msg, st); \
        } \
    } while(0);

#define RIF_ERROR_CHECK(status, msg) \
    rif::IsErrorCheck(__ARCH_FILE__, __ARCH_PRETTY_FUNCTION__, __LINE__, status, msg)

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

inline std::string ConstructErrorMessage(const char* messageOnFail, rif_int errorStatus) {
    if (errorStatus == RIF_SUCCESS) {
        return messageOnFail;
    }

    auto rifErrorString = [](const rif_int s) -> std::string {
        switch (s) {
        case RIF_ERROR_INVALID_API_VERSION: return "invalid api version";
        case RIF_ERROR_INVALID_PARAMETER: return "invalid parameter";
        case RIF_ERROR_UNSUPPORTED: return "unsupported";
        case RIF_ERROR_INTERNAL_ERROR: return "internal error";
        case RIF_ERROR_INVALID_CONTEXT: return "invalid context";
        default:
            break;
        }

        return "error code - " + std::to_string(s);
    };

    return messageOnFail + (": " + rifErrorString(errorStatus));
}

inline bool IsErrorCheck(char const* file, char const* function, size_t line, const rif_int status, const std::string& messageOnFail) {
    if (RIF_SUCCESS == status) {
        return false;
    }

    auto errorMessage = ConstructErrorMessage(messageOnFail.c_str(), status);
    fprintf(stderr, "%s:%s:%zu - [RIF ERROR]: %s\n", file, function, line, errorMessage.c_str());
    return true;
}

class Error : public std::runtime_error {
public:
    Error(const char* messageOnFail, rif_int errorStatus)
        : std::runtime_error(ConstructErrorMessage(messageOnFail, errorStatus)) {

    }
};

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RIFCPP_EXCEPTION_H