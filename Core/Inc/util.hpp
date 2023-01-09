#pragma once

#include <algorithm>
#include <charconv>
#include <string_view>

#include <cmsis_os.h>

#include <p256.hpp>

#include <Stuff/Maths/Bit.hpp>


#define CCMstatic [[gnu::section(".ccmram")]] static

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

template<std::unsigned_integral T, size_t Extent = std::dynamic_extent>
constexpr std::string_view to_chars(std::span<T, Extent> container, std::span<char> output, std::endian repr_endian) {
    std::array<char, sizeof(T) * 2> buffer;

    if (buffer.size() * container.size() > output.size())
        return "";

    for (size_t i = 0; i < container.size(); i++) {
        size_t j = repr_endian == std::endian::big ? i : container.size() - i - 1;
        const auto v = container[j];

        const auto res = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v, 16);
        const auto it = std::copy_backward(buffer.data(), res.ptr, end(buffer));
        std::fill(begin(buffer), it, '0');

        std::copy(begin(buffer), end(buffer), begin(output) + i * sizeof(T) * 2);
    }

    return { begin(output), begin(output) + buffer.size() * container.size() };
}

template<std::unsigned_integral T, size_t Extent = std::dynamic_extent>
constexpr std::from_chars_result
from_chars(std::span<T, Extent> output, std::string_view input, std::endian repr_endian) {
    const size_t segment_size = sizeof(T) * 2;
    const size_t full_segment_count = input.size() / segment_size;
    const size_t excess = input.size() % segment_size;
    const size_t segment_count = full_segment_count + (excess != 0 ? 1 : 0);

    if (segment_count > output.size()) {
        return {
            .ptr = input.begin(),
            .ec = std::errc::value_too_large,
        };
    }

    for (size_t i = 0; i < segment_count; i++) {
        const size_t current_segment_size = i == full_segment_count ? excess : segment_size;
        const size_t current_segment_begin = input.size() - i * segment_size - current_segment_size;
        std::string_view segment = input.substr(current_segment_begin, current_segment_size);
        segment.remove_prefix(segment.find_first_not_of('0'));

        T temp;
        std::from_chars_result res = std::from_chars(segment.begin(), segment.end(), temp, 16);
        if (res.ec != std::errc()) {
            res.ptr = input.begin() + std::distance(input.begin(), res.ptr);
            return res;
        }

        output[i] = temp;
    }

    std::fill(output.begin() + segment_count, output.end(), 0);

    if (repr_endian != std::endian::little)
        std::reverse(output.begin(), output.end());

    return {
        .ptr = input.end(),
        .ec = std::errc(),
    };
}

template<typename T> [[gnu::always_inline]] inline void do_not_optimize(T&& value) {
#if defined(__clang__)
    asm volatile("" : "+r,m"(value) : : "memory");
#else
    asm volatile("" : "+m,r"(value) : : "memory");
#endif
}

P256::PrivateKey get_sk_from_config();

void start_led_tasks();

std::span<TaskStatus_t> get_tasks(std::span<TaskStatus_t> storage);

}
