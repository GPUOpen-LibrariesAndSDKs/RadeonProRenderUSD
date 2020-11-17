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

#ifndef PXR_IMAGING_RPR_USD_API_H
#define PXR_IMAGING_RPR_USD_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define RPRUSD_API
#   define RPRUSD_API_TEMPLATE_CLASS(...)
#   define RPRUSD_API_TEMPLATE_STRUCT(...)
#   define RPRUSD_LOCAL
#else
#   if defined(RPRUSD_EXPORTS)
#       define RPRUSD_API ARCH_EXPORT
#       define RPRUSD_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define RPRUSD_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define RPRUSD_API ARCH_IMPORT
#       define RPRUSD_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define RPRUSD_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define RPRUSD_LOCAL ARCH_HIDDEN
#endif

#endif // PXR_IMAGING_RPR_USD_API_H
