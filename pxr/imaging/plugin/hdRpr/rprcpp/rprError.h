#ifndef RPRCPP_EXCEPTION_H
#define RPRCPP_EXCEPTION_H

#include "debugCodes.h"

#include "pxr/base/arch/functionLite.h"
#include "pxr/base/tf/stringUtils.h"

#include <RadeonProRender.h>
#include <stdexcept>
#include <cassert>
#include <string>

#define RPR_ERROR_CHECK_THROW(status, msg, ...) \
    do { \
        auto st = status; \
        if (st != RPR_SUCCESS) { \
            assert(false); \
            throw rpr::Error(st, msg, __ARCH_FILE__, __ARCH_FUNCTION__, __LINE__, ##__VA_ARGS__); \
        } \
    } while(0);

#define RPR_ERROR_CHECK(status, msg, ...) \
    rpr::IsErrorCheck(status, msg, __ARCH_FILE__, __ARCH_FUNCTION__, __LINE__, ##__VA_ARGS__)

#define RPR_GET_ERROR_MESSAGE(status, msg, ...) \
    rpr::ConstructErrorMessage(status, msg, __ARCH_FILE__, __ARCH_FUNCTION__, __LINE__, ##__VA_ARGS__)

#define RPR_THROW_ERROR_MSG(fmt, ...) \
    rpr::ThrowErrorMsg(__ARCH_FILE__, __ARCH_FUNCTION__, __LINE__, fmt, ##__VA_ARGS__);

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

inline std::string ConstructErrorMessage(rpr_status errorStatus, std::string const& messageOnFail, char const* file, char const* function, size_t line, rpr_context context = nullptr) {
    auto rprErrorString = [errorStatus, context]() -> std::string {
        if (context) {
            size_t lastErrorMessageSize = 0;
            auto status = rprContextGetInfo(context, RPR_CONTEXT_LAST_ERROR_MESSAGE, 0, nullptr, &lastErrorMessageSize);
            if (status == RPR_SUCCESS && lastErrorMessageSize > 1) {
                std::string message(lastErrorMessageSize, '\0');
                status = rprContextGetInfo(context, RPR_CONTEXT_LAST_ERROR_MESSAGE, message.size(), &message[0], nullptr);
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

inline bool IsErrorCheck(const rpr_status status, const std::string& messageOnFail, char const* file, char const* function, size_t line, rpr_context context = nullptr) {
    if (RPR_SUCCESS == status) {
        return false;
    }
    if (status == RPR_ERROR_UNSUPPORTED && !TfDebug::IsEnabled(HD_RPR_DEBUG_CORE_UNSUPPORTED_ERROR)) {
        return true;
    }

    auto errorMessage = ConstructErrorMessage(status, messageOnFail.c_str(), file, function, line, context);
    fprintf(stderr, "%s\n", errorMessage.c_str());
    return true;
}

class Error : public std::runtime_error {
public:
    Error(rpr_status errorStatus, const char* messageOnFail, char const* file, char const* function, size_t line, rpr_context context = nullptr)
        : std::runtime_error(ConstructErrorMessage(errorStatus, messageOnFail, file, function, line, context)) {

    }

    Error(std::string const& errorMesssage)
        : std::runtime_error(errorMesssage) {

    }
};

inline void ThrowErrorMsg(char const* file, char const* function, size_t line, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto messageOnFail = TfVStringPrintf(fmt, ap);
    va_end(ap);
    throw Error(ConstructErrorMessage(RPR_SUCCESS, messageOnFail, file, function, line));
}

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRCPP_EXCEPTION_H