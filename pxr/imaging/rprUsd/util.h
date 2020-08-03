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

#ifndef RPRUSD_UTIL_H
#define RPRUSD_UTIL_H

#include "pxr/pxr.h"

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

bool RprUsdGetUDIMFormatString(std::string const& filepath, std::string* out_formatString);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_UTIL_H
