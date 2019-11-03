#ifndef HDRPR_HOUDINI_OPENVDB_H
#define HDRPR_HOUDINI_OPENVDB_H

#include "pxr/pxr.h"

#include <openvdb/openvdb.h>

PXR_NAMESPACE_OPEN_SCOPE

class HoudiniOpenvdbLoader {
public:
    static HoudiniOpenvdbLoader const& Instance() {
        static HoudiniOpenvdbLoader instance;
        return instance;
    }

    ~HoudiniOpenvdbLoader();

    openvdb::GridBase const* GetGrid(const char* filepath, const char* name) const;

private:
    HoudiniOpenvdbLoader();

private:
    void* m_sopVolLibHandle = nullptr;

    typedef void* (*sopVdbGetterFunction)(const char* filepath, const char* name);
    sopVdbGetterFunction m_vdbGetter = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_HOUDINI_OPENVDB_H
