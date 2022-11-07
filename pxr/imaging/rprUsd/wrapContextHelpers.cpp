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

#include "contextHelpers.h"
#include "boostIncludePath.h"

#include "pxr/base/tf/pyEnum.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/pyResultConversions.h"

#include BOOST_INCLUDE_PATH(python.hpp)
#include BOOST_INCLUDE_PATH(python/class.hpp)
#include BOOST_INCLUDE_PATH(python/def.hpp)
#include BOOST_INCLUDE_PATH(python/scope.hpp)

using namespace BOOST_NS::python;

PXR_NAMESPACE_USING_DIRECTIVE

TF_REGISTRY_FUNCTION(TfEnum) {
    TF_ADD_ENUM_NAME(kPluginInvalid);
    TF_ADD_ENUM_NAME(kPluginNorthstar);
    TF_ADD_ENUM_NAME(kPluginHybrid);
    TF_ADD_ENUM_NAME(kPluginHybridPro);
}

void wrapContextHelpers() {
    class_<RprUsdDevicesInfo::GPU>("GPUDeviceInfo")
        .def(init<int, std::string>())
        .def_readonly("index", &RprUsdDevicesInfo::GPU::index)
        .def_readonly("name", &RprUsdDevicesInfo::GPU::name)
        .def("__eq__", &RprUsdDevicesInfo::GPU::operator==)
        ;

    class_<RprUsdDevicesInfo::CPU>("CPUDeviceInfo")
        .def(init<int>())
        .def_readonly("numThreads", &RprUsdDevicesInfo::CPU::numThreads)
        .def("__eq__", &RprUsdDevicesInfo::CPU::operator==)
        ;

    class_<RprUsdDevicesInfo>("DevicesInfo")
        .add_property("isValid", +[](RprUsdDevicesInfo* thisPtr) { return thisPtr->IsValid(); })
        .add_property("cpu", +[](RprUsdDevicesInfo* thisPtr) { return thisPtr->cpu; })
        .add_property("gpus", +[](RprUsdDevicesInfo* thisPtr) { return Tf_PySequenceToListConverter<decltype(RprUsdDevicesInfo::gpus)>{}(thisPtr->gpus); })
        ;

    def("GetDevicesInfo", RprUsdGetDevicesInfo, arg("pluginType"));

    TfPyWrapEnum<RprUsdPluginType>();
}
