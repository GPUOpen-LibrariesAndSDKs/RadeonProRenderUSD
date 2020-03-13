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

#include "openvdb.h"

#include "pxr/base/arch/library.h"
#include "pxr/base/tf/diagnostic.h"

#ifdef BUILD_AS_HOUDINI_PLUGIN
#include <GT/GT_PrimVDB.h>
#endif

#ifdef WIN32
#include <Windows.h>
#define GETSYM(handle, name) GetProcAddress((HMODULE)handle, name)
#else
#include <dlfcn.h>
#define GETSYM(handle, name) dlsym(handle, name)
#endif

PXR_NAMESPACE_OPEN_SCOPE

HoudiniOpenvdbLoader::~HoudiniOpenvdbLoader() {
    if (m_sopVolLibHandle) {
        ArchLibraryClose(m_sopVolLibHandle);
    }
}

#ifdef BUILD_AS_HOUDINI_PLUGIN

openvdb::GridBase const* HoudiniOpenvdbLoader::GetGrid(const char* filepath, const char* name) const {
    if (!m_vdbGetter) {
        return nullptr;
    }
    auto vdbPrim = reinterpret_cast<GT_PrimVDB*>((*m_vdbGetter)(filepath, name));
    return vdbPrim ? vdbPrim->getGrid() : nullptr;
}

HoudiniOpenvdbLoader::HoudiniOpenvdbLoader() {
    if (auto hfs = std::getenv("HFS")) {
        auto sopVdbLibPath = hfs + std::string("/houdini/dso/USD_SopVol") + ARCH_LIBRARY_SUFFIX;
        m_sopVolLibHandle = ArchLibraryOpen(sopVdbLibPath, ARCH_LIBRARY_LAZY);
        if (m_sopVolLibHandle) {
            m_vdbGetter = (sopVdbGetterFunction)GETSYM(m_sopVolLibHandle, "SOPgetVDBVolumePrimitive");
            if (!m_vdbGetter) {
                TF_RUNTIME_ERROR("USD_SopVol missing required symbol: SOPgetVDBVolumePrimitive");
            }
        } else {
            auto err = ArchLibraryError();
            if (err.empty()) {
                err = "unknown reason";
            }
            TF_RUNTIME_ERROR("Failed to load USD_SopVol library: %s", err.c_str());
        }
    }
}

#else

openvdb::GridBase const* HoudiniOpenvdbLoader::GetGrid(const char* filepath, const char* name) const {
    return nullptr;
}

HoudiniOpenvdbLoader::HoudiniOpenvdbLoader() = default;

#endif // BUILD_AS_HOUDINI_PLUGIN

PXR_NAMESPACE_CLOSE_SCOPE
