#pragma once

#include <span>
#include <string_view>

namespace Tele {

struct BufCharStream {
    constexpr BufCharStream(std::span<char> buffer)
        : m_original_buffer(buffer)
        , m_buffer(buffer) { }

    template<typename Char, typename Traits>
    constexpr BufCharStream& operator<<(std::basic_string_view<Char, Traits> v) {
        if (m_buffer.size() < v.size())
            return *this;

        std::copy(begin(v), end(v), begin(m_buffer));
        m_buffer = m_buffer.subspan(v.size());

        return *this;
    }

    constexpr BufCharStream& operator<<(const char* str) { return *this << std::string_view(str); }

    constexpr BufCharStream& operator<<(char v) {
        if (m_buffer.empty())
            return *this;

        m_buffer[0] = v;
        m_buffer = m_buffer.subspan(1);

        return *this;
    }

    constexpr operator std::string_view() const { return { begin(m_original_buffer), begin(m_buffer) }; }

private:
    std::span<char> m_original_buffer;
    std::span<char> m_buffer;
};

template<typename Container> struct PushBackStream {
    constexpr PushBackStream(Container& container)
        : m_container(container) { }

    template<typename Char, typename Traits>
    constexpr PushBackStream& operator<<(std::basic_string_view<Char, Traits> v) {
        m_container.reserve(m_container.size() + v.size());
        std::copy(begin(v), end(v), back_inserter(m_container));
        return *this;
    }

    constexpr PushBackStream& operator<<(char v) {
        m_container.push_back(v);
        return *this;
    }

private:
    Container& m_container;
};

}
