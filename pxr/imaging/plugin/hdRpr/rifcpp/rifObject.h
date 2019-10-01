#ifndef RIFCPP_OBJECT_H
#define RIFCPP_OBJECT_H

#include <RadeonImageFilters.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

class Object {
public:
    Object() = default;
    Object(Object const&) = delete;
    Object& operator=(Object const&) = delete;

    void Delete() {
        if (m_rifObjectHandle) {
            rifObjectDelete(m_rifObjectHandle);
        }
        m_rifObjectHandle = nullptr;
    }

    ~Object() {
        Delete();
    }

protected:
    void* m_rifObjectHandle = nullptr;
};

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RIFCPP_OBJECT_H
