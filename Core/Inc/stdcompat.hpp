#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

extern "C" void getentropy(void *buffer, size_t length);

/*using __int128_t [[gnu::mode(TI)]] = int;
using __uint128_t [[gnu::mode(TI)]] = unsigned;*/

/*struct __uint128_t {
    constexpr __uint128_t() = default;

    constexpr __uint128_t(uint64_t v) {
        m_data[15] = (v >> 0) & 0xFF;
        m_data[14] = (v >> 8) & 0xFF;
        m_data[13] = (v >> 16) & 0xFF;
        m_data[12] = (v >> 24) & 0xFF;
        m_data[11] = (v >> 32) & 0xFF;
        m_data[10] = (v >> 40) & 0xFF;
        m_data[9] = (v >> 48) & 0xFF;
        m_data[8] = (v >> 56) & 0xFF;
    }

    constexpr operator std::span<uint8_t>() {
        return m_data;
    }

    constexpr operator std::span<const uint8_t>() const {
        return m_data;
    }

    operator std::string_view() const {
        return { reinterpret_cast<const char*>(m_data), 16 };
    }

private:
    uint8_t m_data[16] {0};
};*/

// i tried, ok?
using __uint128_t = uint64_t;
using __int128_t = int64_t;

namespace Tele {

void set_time(int32_t timestamp);
int32_t get_time();

}
