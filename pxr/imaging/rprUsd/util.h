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

#include "pxr/imaging/rprUsd/api.h"

#if PXR_VERSION >= 2102
#include "pxr/imaging/garch/glApi.h"
#else
#include "pxr/imaging/glf/glew.h"
#endif

#include "pxr/imaging/glf/uvTextureData.h"

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

RPRUSD_API
bool RprUsdGetUDIMFormatString(std::string const& filepath, std::string* out_formatString);

RPRUSD_API
bool RprUsdInitGLApi();

struct RprUsdGlfTextureMetadata {
    GLenum glType;
    GLenum glFormat;
    GLenum internalFormat;
};

RPRUSD_API
RprUsdGlfTextureMetadata RprUsdGetGlfTextureMetadata(GlfUVTextureData* uvTextureData);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRUSD_UTIL_H
