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
