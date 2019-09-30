#ifndef RPRCPP_OBJECT_H
#define RPRCPP_OBJECT_H

#include <RadeonProRender.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

class Object {
public:
    Object() = default;
    Object(Object const&) = delete;
    Object& operator=(Object const&) = delete;

    void Delete() {
        if (m_rprObjectHandle) {
            rprObjectDelete(m_rprObjectHandle);
        }
        m_rprObjectHandle = nullptr;
    }

    ~Object() {
        Delete();
    }

protected:
    void* m_rprObjectHandle = nullptr;
};

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRCPP_OBJECT_H
