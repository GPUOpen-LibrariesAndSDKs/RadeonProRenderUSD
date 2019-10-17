#ifndef RPRCPP_EXCEPTION_H
#define RPRCPP_EXCEPTION_H

#include "pxr/pxr.h"
#include "pxr/base/arch/functionLite.h"

#include <RadeonProRender.h>
#include <stdexcept>
#include <cassert>
#include <string>

#define RPR_ERROR_CHECK_THROW(status, msg, ...) \
    do { \
        auto st = status; \
        if (st != RPR_SUCCESS) { \
            assert(false); \
            throw rpr::Error(msg, st, ##__VA_ARGS__); \
        } \
    } while(0);

#define RPR_ERROR_CHECK(status, msg, ...) \
    rpr::IsErrorCheck(__ARCH_FILE__, __ARCH_PRETTY_FUNCTION__, __LINE__, status, msg, ##__VA_ARGS__)

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

inline std::string ConstructErrorMessage(const char* messageOnFail, rpr_status errorStatus, rpr_context context) {
    if (errorStatus == RPR_SUCCESS) {
        return messageOnFail;
    }

    auto rprErrorString = [](const rpr_status s) -> std::string {
        switch (s)
        {
        case RPR_ERROR_INVALID_API_VERSION: return "invalid api version";
        case RPR_ERROR_INVALID_PARAMETER: return "invalid parameter";
        case RPR_ERROR_UNSUPPORTED: return "unsupported";
        case RPR_ERROR_INTERNAL_ERROR: return "internal error";
        case RPR_ERROR_INVALID_CONTEXT: return "invalid context";
        default:
            break;
        }

        return "error code - " + std::to_string(s);
    };

    if (context) {
        size_t lastErrorMessageSize = 0;
        auto status = rprContextGetInfo(context, RPR_CONTEXT_LAST_ERROR_MESSAGE, 0, nullptr, &lastErrorMessageSize);
        if (status == RPR_SUCCESS && lastErrorMessageSize > 1) {
            std::string message;

            auto lastErrorMessage = new char[lastErrorMessageSize];
            status = rprContextGetInfo(context, RPR_CONTEXT_LAST_ERROR_MESSAGE, lastErrorMessageSize, lastErrorMessage, nullptr);
            if (status == RPR_SUCCESS) {
                message = messageOnFail + std::string(": ") + lastErrorMessage;
            }
            delete[] lastErrorMessage;

            if (!message.empty()) {
                return message;
            }
        }
    }

    return messageOnFail + (": " + rprErrorString(errorStatus));
}

inline bool IsErrorCheck(char const* file, char const* function, size_t line, const rpr_status status, const std::string& messageOnFail, rpr_context context = nullptr) {
    if (RPR_SUCCESS == status) {
        return false;
    }

    auto errorMessage = ConstructErrorMessage(messageOnFail.c_str(), status, context);
    fprintf(stderr, "%s:%s:%zu - [RPR ERROR]: %s\n", file, function, line, errorMessage.c_str());
    return true;
}

class Error : public std::runtime_error {
public:
    Error(const char* messageOnFail, rpr_status errorStatus, rpr_context context = nullptr)
        : std::runtime_error(ConstructErrorMessage(messageOnFail, errorStatus, context)) {

    }

    Error(const char* errorMesssage)
        : std::runtime_error(errorMesssage) {

    }
};

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRCPP_EXCEPTION_H