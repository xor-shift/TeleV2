#pragma once

#include <cstring>
#include <span>
#include <string_view>
#include <utility>

enum class BufferStringPolicy {
    TruncateRight = 0,
    TruncateLeft = 1,
    Fail = 2,
};

template<size_t Capacity = 128, BufferStringPolicy Policy = BufferStringPolicy::TruncateRight> struct BufferString {
    constexpr BufferString() = default;

    template<BufferStringPolicy OtherPolicy>
    constexpr BufferString(BufferString<Capacity, OtherPolicy> const& other) {
        std::copy(other.m_storage, other.m_storage + other.m_size, m_storage);
        m_size = other.m_size;
    }

    template<size_t OtherCapacity, BufferStringPolicy OtherPolicy>
    constexpr BufferString(BufferString<OtherCapacity, OtherPolicy> const& other) {
        *this = std::span(other.m_storage, other.m_storage + other.m_size);
    }

    template<BufferStringPolicy OtherPolicy>
    constexpr BufferString(BufferString<Capacity, OtherPolicy>&& other) {
        *this = static_cast<BufferString<Capacity, OtherPolicy> const&>(other);
        other.m_size = 0;
    }


    template<size_t OtherCapacity, BufferStringPolicy OtherPolicy>
    constexpr BufferString(BufferString<OtherCapacity, OtherPolicy>&& other) {
        *this = static_cast<BufferString<OtherCapacity, OtherPolicy> const&>(other);
        other.m_size = 0;
    }

    constexpr BufferString(const char* str) { *this = str; }

    constexpr BufferString& operator=(std::span<const char> buffer) {
        size_t sz = buffer.size();
        const char* str = buffer.data();

        if (sz <= Capacity) {
            std::copy(str, str + sz, m_storage);
            m_size = sz;
            return *this;
        }

        const char* start;
        const char* end;

        if constexpr (Policy == BufferStringPolicy::TruncateRight) {
            start = str;
            end = str + Capacity;
        } else if constexpr (Policy == BufferStringPolicy::TruncateLeft) {
            start = str + sz - Capacity;
            end = str + sz;
        } else if constexpr (Policy == BufferStringPolicy::Fail) {
            static_assert("A BufferStringPolicy of Fail is not yet supported");
        } else {
            std::unreachable();
        }

        std::copy(start, end, m_storage);

        return *this;
    }

    template<typename Traits = std::char_traits<char>>
    constexpr BufferString operator=(std::basic_string_view<char, Traits> str) {
        return *this = std::span(begin(str), end(str));
    }

    constexpr BufferString operator=(const char* str) {
        size_t sz = 0uz;
        if consteval {
            for (const char* it = str; *it != '\0'; it++, sz++) { }
        } else {
            sz = std::strlen(str);
        }

        return *this = std::span(str, str + sz);
    }

    constexpr std::string_view str() const { return { m_storage, m_storage + m_size }; }
    constexpr operator std::string_view() const { return str(); }

private:
    char m_storage[Capacity];
    size_t m_size = 0;
};