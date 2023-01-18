#include "util.hpp"

#include <algorithm>
#include <random>
#include <utility>

#include "cmsis_os.h"
#include "main.h"

#include <StaticTask.hpp>

#include "secrets.hpp"
#include "util.hpp"

namespace Tele {

ResetCause get_reset_cause() {
#define CAUSE_FACTORY(_flg, _res) else if (__HAL_RCC_GET_FLAG(_flg) != 0u) cause = _res
    ResetCause cause = ResetCause::Unknown;

    if (false) {} // NOLINT(readability-simplify-boolean-expr)
    CAUSE_FACTORY(RCC_FLAG_LPWRRST, ResetCause::LowPower);
    CAUSE_FACTORY(RCC_FLAG_WWDGRST, ResetCause::WindowWatchdog);
    CAUSE_FACTORY(RCC_FLAG_IWDGRST, ResetCause::IndependentWatchdog);
    CAUSE_FACTORY(RCC_FLAG_SFTRST, ResetCause::Software);
    CAUSE_FACTORY(RCC_FLAG_PORRST, ResetCause::PowerOnPowerDown);
    CAUSE_FACTORY(RCC_FLAG_PINRST, ResetCause::ExternalResetPin);
    CAUSE_FACTORY(RCC_FLAG_BORRST, ResetCause::Brownout);

    return cause;
}

P256::PrivateKey get_sk_from_config() {
    P256::PrivateKey sk;
    Tele::from_chars(std::span(sk.d), Tele::Config::sk_text, std::endian::little);

    if (!sk.compute_pk())
        Error_Handler();

    if (Tele::Config::pkx_text.empty() || Tele::Config::pky_text.empty())
        return sk;

    std::array<uint32_t, 8> expected_pk_x;
    Tele::from_chars(std::span(expected_pk_x), Tele::Config::pkx_text, std::endian::little);
    std::array<uint32_t, 8> expected_pk_y;
    Tele::from_chars(std::span(expected_pk_y), Tele::Config::pky_text, std::endian::little);

    if (sk.pk.x != expected_pk_x)
        Error_Handler();

    if (sk.pk.y != expected_pk_y)
        Error_Handler();

    return sk;
}

static bool parse_ipv4_whole(std::span<uint8_t> out, std::string_view str) {
    return false;
}

static bool parse_ipv4_decimated(std::span<uint8_t> out, std::string_view str) {
    std::array<std::string_view, 4> segments {{"", "", "", ""}};

    for (std::string_view::size_type prev = 0; auto& segment : segments) {
        std::string_view::size_type next = str.find('.', prev);

        segment = str.substr(prev, next - prev);
        prev = next + 1;

        if (next == std::string_view::npos)
            break;
    }

    if (std::find(begin(segments), end(segments), "") != end(segments))
        return false;

    for (size_t i = 0; i < 4; i++) {
        std::string_view segment_str = segments[i];
        uint8_t segment;

        std::from_chars_result res;

        if (segment_str.starts_with("0x")) {
            res = std::from_chars(begin(segment_str) + 2, end(segment_str), segment, 16);
        } else if (segment_str.starts_with("0")) {
            res = std::from_chars(begin(segment_str), end(segment_str), segment, 8);
        } else {
            res = std::from_chars(begin(segment_str), end(segment_str), segment, 10);
        }

        if (res.ec != std::errc())
            return false;

        out[i] = segment;
    }

    return true;
}

static bool parse_ipv4(std::span<uint8_t> out, std::string_view str) {
    if (out.size() < 4)
        return false;

    if (str.contains('.')) [[likely]] {
        return parse_ipv4_decimated(out, str);
    }

    return parse_ipv4_whole(out, str);
}

static bool parse_ipv6(std::span<uint8_t> out, std::string_view str) {
    if (out.size() < 16)
        return false;

    //

    return false;
}

bool parse_ip(std::string_view ip, bool& ipv4, std::span<uint8_t> out) {
    if (parse_ipv4(out, ip)) {
        ipv4 = true;
        return true;
    }

    if (parse_ipv6(out, ip)) {
        ipv4 = false;
        return true;
    }

    return false;
}

}
