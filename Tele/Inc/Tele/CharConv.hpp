#pragma once

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <span>
#include <string_view>

#include <tl/expected.hpp>

namespace Tele {

template<std::unsigned_integral T, size_t Extent = std::dynamic_extent>
static std::string_view to_chars(std::span<T, Extent> container, std::span<char> output, std::endian repr_endian) {
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
static std::from_chars_result from_chars(std::span<T, Extent> output, std::string_view input, std::endian repr_endian) {
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
        segment.remove_prefix(std::min(segment.find_first_not_of('0'), segment.size()));
        if (segment.empty())
            segment = "0";

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

template<std::integral T> static tl::expected<T, std::errc> from_chars(std::string_view str, int base = 10) {
    T v;

    std::from_chars_result res = std::from_chars(begin(str), end(str), v, base);
    if (res.ec != std::errc())
        return tl::unexpected { res.ec };

    return v;
}

template<std::floating_point T>
static tl::expected<T, std::errc>
from_chars(std::string_view str, std::chars_format format = std::chars_format::general) {
    T v;

    std::from_chars_result res = std::from_chars(begin(str), end(str), v, format);
    if (res.ec != std::errc())
        return tl::unexpected { res.ec };

    return v;
}

}
