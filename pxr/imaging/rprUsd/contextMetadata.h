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

#ifndef PXR_IMAGING_RPR_USD_CONTEXT_METADATA_H
#define PXR_IMAGING_RPR_USD_CONTEXT_METADATA_H

#include <map>

#include "pxr/pxr.h"
#include "pxr/imaging/rprUsd/api.h"

#include <RadeonProRender.hpp>

#include <string>
#include <vector>


PXR_NAMESPACE_OPEN_SCOPE

enum RprUsdPluginType {
    kPluginInvalid = -1,
    kPluginNorthstar,
    kPluginHybrid,
    kPluginHybridPro,
    kPluginsCount
};

struct RprUsdDevicesInfo {
    struct CPU {
	    int numThreads;
	    CPU(int numThreads = 0) : numThreads(numThreads) {}
	    bool operator==(CPU const& rhs) { return numThreads == rhs.numThreads; }
    };
    CPU cpu;

    struct GPU {
        int index;
        std::string name;
        GPU(int index = -1, std::string name = {}) : index(index), name(name) {}
        bool operator==(GPU const& rhs) { return index == rhs.index && name == rhs.name; }
    };
    std::vector<GPU> gpus;

    bool IsValid() const {
        return cpu.numThreads > 0 || !gpus.empty();
    }
};

struct RprUsdContextMetadata {
    RprUsdPluginType pluginType = kPluginInvalid;
    bool isGlInteropEnabled = false;
    bool useOpenCL = false;
    void* interopInfo = nullptr;
    rpr::CreationFlags creationFlags = 0;
    // additional info about hardware actually used in render context creation
    RprUsdDevicesInfo devicesActuallyUsed;
    std::map<std::uint64_t, std::uint32_t> additionalIntProperties;
};

RPRUSD_API
bool RprUsdIsGpuUsed(RprUsdContextMetadata const& contextMetadata);

inline bool RprUsdIsHybrid(RprUsdPluginType pluginType) {
    return pluginType == kPluginHybrid || pluginType == kPluginHybridPro;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_RPR_USD_CONTEXT_METADATA_H
