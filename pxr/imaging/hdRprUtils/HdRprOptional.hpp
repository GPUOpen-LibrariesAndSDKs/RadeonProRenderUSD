#pragma once

#include <stdexcept>
#include <iostream>

namespace utils
{

///
/// Lightweight implementation of optional until we would use C++17
///

template <typename T>
class optional
{
    T m_value;
    bool m_hasValue = false;

public:
    optional() : m_value() {}

    optional& operator=(const T& other)
    {
        m_value = other;
        m_hasValue = true;
        return *this;
    }

    bool has_value() const
    {
        return m_hasValue;
    }

    const T& value() const
    {
        if (!has_value()) {
            throw std::runtime_error("Optional does not have value");
        }

        return m_value;
    }
};

}
