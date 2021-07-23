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

#ifndef PXR_IMAGING_RPR_USD_HELPERS_H
#define PXR_IMAGING_RPR_USD_HELPERS_H

#include "pxr/imaging/rprUsd/error.h"

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

template <typename T, typename U, typename R>
T RprUsdGetInfo(U* object, R info) {
    T value = {};
    RPR_ERROR_CHECK_THROW(object->GetInfo(info, sizeof(value), &value, nullptr), "Failed to get object info");
    return value;
}

template <typename T>
struct Buffer {
    std::unique_ptr<T[]> data;
    size_t size;

    explicit operator bool() const { return data && size; }
};

template <typename T, typename GetInfoFunc>
Buffer<T> RprUsdGetListInfo(GetInfoFunc&& getInfoFunc) {
    size_t size = 0;
    RPR_ERROR_CHECK_THROW(getInfoFunc(sizeof(size), nullptr, &size), "Failed to get object info");

    if (size <= 1) {
        return {};
    }

    size_t numElements = size / sizeof(T);
    auto buffer = std::make_unique<T[]>(numElements);
    RPR_ERROR_CHECK_THROW(getInfoFunc(size, buffer.get(), nullptr), "Failed to get object info");

    return {std::move(buffer), numElements};
}

template <typename T, typename U, typename R>
Buffer<T> RprUsdGetListInfo(U* object, R info) {
    return RprUsdGetListInfo<T>([object, info](size_t size, void* data, size_t* size_ret) { return object->GetInfo(info, size, data, size_ret); });
}

template <typename U, typename R>
std::string RprUsdGetStringInfo(U* object, R info) {
    if (auto strBuffer = RprUsdGetListInfo<char>(object, info)) {
        // discard null-terminator
        --strBuffer.size;

        return std::string(strBuffer.data.get(), strBuffer.size);
    }
    return {};
}

template<typename Wrapper>
Wrapper* RprUsdGetRprObject(typename rpr::RprApiTypeOf<Wrapper>::value rprApiObject) {
    const void* customPtr = nullptr;
    if (rprObjectGetCustomPointer(rprApiObject, &customPtr) != RPR_SUCCESS) {
        return nullptr;
    }
    assert(dynamic_cast<Wrapper const*>(static_cast<rpr::ContextObject const*>(customPtr)));
    return const_cast<Wrapper*>(static_cast<Wrapper const*>(customPtr));
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_RPR_USD_HELPERS_H
