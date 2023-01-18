#pragma once

#include <algorithm>
#include <charconv>
#include <functional>
#include <optional>
#include <string_view>

#include <fmt/core.h>

#include <Stuff/Maths/Bit.hpp>
#include <Stuff/Util/Hacks/Try.hpp>

#include <cmsis_os.h>
#include <p256.hpp>

#define CCMstatic [[gnu::section(".ccmram")]] static

namespace Tele {

enum class ResetCause {
    Unknown,
    LowPower,
    WindowWatchdog,
    IndependentWatchdog,
    // NVIC_SystemReset();
    Software,
    PowerOnPowerDown,
    ExternalResetPin,
    Brownout,
};

ResetCause get_reset_cause();

constexpr std::string_view enum_name(ResetCause cause) {
    switch (cause) {
    case ResetCause::LowPower: return "low power";
    case ResetCause::WindowWatchdog: return "window watchdog";
    case ResetCause::IndependentWatchdog: return "independent watchdog";
    case ResetCause::Software: return "software";
    case ResetCause::PowerOnPowerDown: return "poweron/poweroff";
    case ResetCause::ExternalResetPin: return "external reset pin";
    case ResetCause::Brownout: return "brownout";
    default: return "unknown";
    }
}

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

template<typename Char, typename Traits = std::char_traits<Char>> struct EscapedString {
    std::basic_string_view<Char, Traits> data = "";
};

template<typename Char, typename Traits>
EscapedString(std::basic_string_view<Char, Traits>) -> EscapedString<Char, Traits>;
EscapedString(const char*) -> EscapedString<char>;

template<typename T> [[gnu::always_inline]] inline void do_not_optimize(T&& value) {
#if defined(__clang__)
    asm volatile("" : "+r,m"(value) : : "memory");
#else
    asm volatile("" : "+m,r"(value) : : "memory");
#endif
}

template<typename T, typename Fn> constexpr size_t in_chunks(std::span<T> span, size_t chunk_sz, Fn&& fn) {
    if (chunk_sz == 0)
        return 0;

    size_t executions = 0;

    while (++executions, !span.empty()) {
        size_t sz = std::min(chunk_sz, span.size());
        size_t real_sz = std::invoke(fn, std::span<T>(span.subspan(0, sz)));
        span = span.subspan(real_sz);
    }

    return executions;
}

template<typename Callback> struct DelimitedReader {
    constexpr DelimitedReader(Callback const& callback, std::span<char> buffer, std::string_view delimiter)
        : m_callback(callback)
        , m_buffer(buffer)
        , m_delimiter(delimiter) { }

    constexpr void add_char(char c) {
        if (add_char_impl(c)) {
            try_finish();
            return;
        }

        if (try_finish()) {
            return add_char(c);
        }

        // what
    }

    constexpr void add_chars(std::string_view chars) {
        for (char c : chars) {
            add_char(c);
        }
    }

private:
    Callback m_callback;
    std::span<char> m_buffer;
    std::string_view m_delimiter;

    bool m_overflown = false;
    size_t m_delimiter_match_sz = 0;
    size_t m_buffer_usage = 0;

    constexpr bool ready() const { return m_delimiter_match_sz == m_delimiter.size(); }

    constexpr bool try_finish() {
        if (!ready())
            return false;

        std::invoke(m_callback, std::string_view(data(m_buffer), m_buffer_usage), m_overflown);

        m_overflown = false;
        m_delimiter_match_sz = 0;
        m_buffer_usage = 0;
        return true;
    }

    // returns true if a char was added
    // if false and ready(), the character should be retried after the callback is called
    // if false and not ready(), there's an overflow, m_overflown will be updated accordingly
    constexpr bool add_char_impl(char c) {
        // TODO: We can use memcpy etc. if we know that there's no delimiter. Check to see if that is more performant.

        if (ready()) [[unlikely]] /* ready() should be checked */
            return false;

        if (m_delimiter[m_delimiter_match_sz] == c) {
            ++m_delimiter_match_sz;
            return true;
        }

        if (m_buffer_usage >= m_buffer.size()) {
            m_overflown = true;
            return false;
        }

        m_buffer[m_buffer_usage++] = c;

        return true;
    }
};

P256::PrivateKey get_sk_from_config();

std::span<TaskStatus_t> get_tasks(std::span<TaskStatus_t> storage);

bool parse_ip(std::string_view ip, bool& ipv4, std::span<uint8_t> out);

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

template<typename Char, typename Traits> struct fmt::formatter<Tele::EscapedString<Char, Traits>> {
    template<typename ParseContext> constexpr auto parse(ParseContext& ctx) {
        auto it = ctx.begin();

        return it;
    }

    template<typename FormatContext> auto format(Tele::EscapedString<Char, Traits> const& string, FormatContext& ctx) {
        auto it = ctx.out();

        for (char c : string.data) {
            char simple_escape = '\0';

            switch (c) {
            case '\a': simple_escape = 'a'; break;
            case '\b': simple_escape = 'b'; break;
            case '\t': simple_escape = 't'; break;
            case '\n': simple_escape = 'n'; break;
            case '\v': simple_escape = 'v'; break;
            case '\f': simple_escape = 'f'; break;
            case '\r': simple_escape = 'r'; break;
            case '\"': simple_escape = '"'; break;
            case '\'': simple_escape = '\''; break;
            case '\\': simple_escape = '\\'; break;
            case '\0': simple_escape = '0'; break;
            default: break;
            }

            if (simple_escape != '\0') {
                *it++ = '\\';
                *it++ = simple_escape;
                continue;
            }

            // is c printable?
            if ('~' >= c && c >= ' ') [[likely]] {
                *it++ = c;
                continue;
            }

            uint8_t byte = static_cast<uint8_t>(c);
            it = fmt::format_to(it, "\\0{:02o}", byte);
        }

        return it;
    }
};
