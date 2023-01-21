#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string_view>

namespace Tele {

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
        // additional note: the delimiter can be checked for in a "vectored" fashion (a vector of a mere 32 bits)
        // ^ this would break constexpr-ness, most likely

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

}
