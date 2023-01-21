#pragma once

#include <fmt/core.h>

namespace Tele {

template<typename Char, typename Traits = std::char_traits<Char>> struct EscapedString {
    std::basic_string_view<Char, Traits> data = "";
};

template<typename Char, typename Traits>
EscapedString(std::basic_string_view<Char, Traits>) -> EscapedString<Char, Traits>;
EscapedString(const char*) -> EscapedString<char>;

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
