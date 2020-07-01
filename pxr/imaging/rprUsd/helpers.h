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

PXR_NAMESPACE_OPEN_SCOPE

template <typename T, typename U, typename R>
T RprUsdGetInfo(U* object, R info) {
    T value = {};
    size_t dummy;
    RPR_ERROR_CHECK_THROW(object->GetInfo(info, sizeof(value), &value, &dummy), "Failed to get object info");
    return value;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_RPR_USD_HELPERS_H
