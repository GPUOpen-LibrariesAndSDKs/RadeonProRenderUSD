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
