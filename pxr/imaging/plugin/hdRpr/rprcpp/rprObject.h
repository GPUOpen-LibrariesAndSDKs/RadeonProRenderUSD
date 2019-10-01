#ifndef RPRCPP_OBJECT_H
#define RPRCPP_OBJECT_H

#include "pxr/pxr.h"

#include <RadeonProRender.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

class Object {
public:
    Object(Object const&) = delete;
    Object& operator=(Object const&) = delete;

    virtual ~Object() {
        Delete();
    }

protected:
    Object() = default;

    void Delete() {
        if (m_rprObjectHandle) {
            rprObjectDelete(m_rprObjectHandle);
        }
        m_rprObjectHandle = nullptr;
    }

protected:
    void* m_rprObjectHandle = nullptr;
};

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRCPP_OBJECT_H
