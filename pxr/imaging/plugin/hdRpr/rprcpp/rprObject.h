#ifndef RPRCPP_OBJECT_H
#define RPRCPP_OBJECT_H

#include <RadeonProRender.h>

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

#endif // RPRCPP_OBJECT_H
