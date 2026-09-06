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

#include "config.h"
#include "boostIncludePath.h"

#include RPR_BOOST_PYTHON_INCLUDE(python.hpp)
#include RPR_BOOST_PYTHON_INCLUDE(python/class.hpp)
#include RPR_BOOST_PYTHON_INCLUDE(python/def.hpp)
#include RPR_BOOST_PYTHON_INCLUDE(python/scope.hpp)

using namespace RPR_BOOST_PYTHON_NS;

PXR_NAMESPACE_USING_DIRECTIVE

void wrapConfig() {

#define CONFIG_SETTER(setter, type) \
    .def(#setter, +[](type value) { \
        RprUsdConfig* config; \
        auto configLock = RprUsdConfig::GetInstance(&config); \
        config->setter(value); \
    }) \
    .staticmethod(#setter)

#define CONFIG_GETTER(getter) \
    .def(#getter, +[]() { \
        RprUsdConfig* config; \
        auto configLock = RprUsdConfig::GetInstance(&config); \
        return config->getter(); \
    }) \
    .staticmethod(#getter)

    scope s = class_<RprUsdConfig, RPR_BOOST_NONCOPYABLE>("Config", no_init)
        CONFIG_SETTER(SetRestartWarning, bool)
        CONFIG_SETTER(SetTextureCacheDir, std::string)
        CONFIG_SETTER(SetKernelCacheDir, std::string)
        CONFIG_GETTER(IsRestartWarningEnabled)
        CONFIG_GETTER(GetTextureCacheDir)
        CONFIG_GETTER(GetKernelCacheDir)
        CONFIG_GETTER(GetDeviceConfigurationFilepath)
    ;
}
