#pragma once

#include "core/util/Error.h"

#include <cassert>
#include <utility>
#include <variant>

namespace semplice {

// A value or an Error. No exceptions cross module boundaries (SPEC 3).
template <class T, class E = Error>
class [[nodiscard]] Result {
public:
    Result(T value) : m_data(std::in_place_index<0>, std::move(value)) {}
    Result(E error) : m_data(std::in_place_index<1>, std::move(error)) {}

    bool ok() const { return m_data.index() == 0; }
    explicit operator bool() const { return ok(); }

    const T& value() const&
    {
        assert(ok());
        return std::get<0>(m_data);
    }
    T& value() &
    {
        assert(ok());
        return std::get<0>(m_data);
    }
    T&& value() &&
    {
        assert(ok());
        return std::get<0>(std::move(m_data));
    }
    const E& error() const
    {
        assert(!ok());
        return std::get<1>(m_data);
    }
    T valueOr(T fallback) const { return ok() ? std::get<0>(m_data) : std::move(fallback); }

private:
    std::variant<T, E> m_data;
};

template <class E>
class [[nodiscard]] Result<void, E> {
public:
    Result() = default;
    Result(E error) : m_error(std::move(error)), m_failed(true) {}

    bool ok() const { return !m_failed; }
    explicit operator bool() const { return ok(); }
    const E& error() const
    {
        assert(m_failed);
        return m_error;
    }

private:
    E m_error{};
    bool m_failed = false;
};

}  // namespace semplice
