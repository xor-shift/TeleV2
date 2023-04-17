#include <Tele/NMEA.hpp>

#include <charconv>

#include <Stuff/Util/Hacks/Try.hpp>

#include <stdcompat.hpp>

namespace Tele::NMEA {

static constexpr uint8_t checksum(std::string_view str) {
    uint8_t sum = 0;

    for (char c : str) {
        sum ^= static_cast<uint8_t>(c);
    }

    return sum;
}

static constexpr std::optional<uint8_t> extract_checksum(std::string_view substr) {
    if (substr.size() != 2)
        return 0;

    auto char_to_hex = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9')
            return c - '0';

        if (c >= 'a' && c <= 'f')
            return 10 + c - 'a';

        if (c >= 'A' && c <= 'F')
            return 10 + c - 'A';

        return 255;
    };

    uint8_t lhs = char_to_hex(substr[0]);
    uint8_t rhs = char_to_hex(substr[1]);

    if (lhs == 255 || rhs == 255)
        return std::nullopt;

    return (lhs << 4) | rhs;
}

tl::expected<RawMessage, std::string_view> parse_raw_message(std::string_view str) {
    if (str.size() < 6)
        return tl::unexpected { "string too short" };

    // if (!str.ends_with("\r\n"))
    //     return tl::unexpected { "expected \"\\r\\n\" at the end of the line" };

    if (!str.starts_with('$'))
        return tl::unexpected { "expected '$' at the start of the line" };

    if (str[str.size() - 3] != '*')
        return tl::unexpected { "expected a checksum delimiter ('*')" };

    std::string_view contents = str.substr(1, str.size() - std::string_view("$*00").size());
    uint8_t content_checksum = checksum(contents);
    uint8_t given_checksum = ({
        auto res = extract_checksum(str.substr(str.size() - 2, 2));
        if (!res)
            return tl::unexpected { "bad checksum" };

        *res;
    });

    if (content_checksum != given_checksum)
        return tl::unexpected { "checksum mismatch" };

    RawMessage ret {
        .full_message = str,
        .talker = Talker::Unknown,
        .message_type = "",
        .csv = contents,
    };

    std::string_view first_segment = TRY_OR_RET(tl::unexpected { "could not find any csv sements" }, ret.consume_csv());

    if (first_segment.size() < 3) {
        return tl::unexpected { "cannot discern the talker and/or the message type" };
    }

    if (auto talker_str = first_segment.substr(0, 2); talker_str == "GP") {
        ret.talker = Talker::GPS;
    } else if (talker_str == "GA") {
        ret.talker = Talker::Galileo;
    } else if (talker_str == "GL") {
        ret.talker = Talker::GLONASS;
    } else if (talker_str == "BD" || talker_str == "GB") {
        ret.talker = Talker::Beidou;
    } else {
        ret.talker = Talker::Unknown;
    }

    ret.message_type = first_segment.substr(2);

    return ret;
}

tl::expected<int32_t, std::string_view> parse_time(std::string_view time_str) {
    if (time_str.size() < 6)
        return tl::unexpected { "time string too short" };

    auto char_to_decimal = [](char c) -> int { return c - '0'; };

    auto parse_segment = [&](std::string_view segment) -> std::optional<int> {
        int res = 0;

        for (char c : segment) {
            auto v = char_to_decimal(c);
            if (v < 0 || v > 9)
                return std::nullopt;

            res *= 10;
            res += v;
        }

        return res;
    };

    auto hh = TRY_OR_RET(tl::unexpected { "bad hour string" }, parse_segment(time_str.substr(0, 2)));
    auto mm = TRY_OR_RET(tl::unexpected { "bad minute string" }, parse_segment(time_str.substr(2, 2)));
    auto ss = TRY_OR_RET(tl::unexpected { "bad second string" }, parse_segment(time_str.substr(4, 2)));

    return ss + mm * 60 + hh * 3600;
}

std::optional<float> parse_degrees(std::string_view str) {
    auto decimal_pos = str.find('.');
    if (decimal_pos == std::string_view::npos || decimal_pos < 2 || decimal_pos == str.size())
        return std::nullopt;

    std::string_view degree_str = str.substr(0, decimal_pos - 2);
    std::string_view min_whole_str = str.substr(decimal_pos - 2, 2);
    std::string_view min_frac_str = str.substr(decimal_pos + 1);

    int degrees;
    if (auto res = std::from_chars(degree_str.begin(), degree_str.end(), degrees); res.ec != std::errc())
        return std::nullopt;

    int minutes_whole;
    if (auto res = std::from_chars(min_whole_str.begin(), min_whole_str.end(), minutes_whole); res.ec != std::errc())
        return std::nullopt;

    int minutes_fraction;
    if (auto res = std::from_chars(min_frac_str.begin(), min_frac_str.end(), minutes_fraction); res.ec != std::errc())
        return std::nullopt;

    float minutes = static_cast<float>(minutes_whole) + static_cast<float>(minutes_fraction) / 100.f;
    return static_cast<float>(degrees) + minutes / 60.f;
}

tl::expected<message_type, std::string_view> parse_line(std::string_view str) {
    RawMessage raw_message = TRYX(parse_raw_message(str));

    if (raw_message.message_type == "GGA") {
        std::string_view time_str = TRY_OR_RET(tl::unexpected { "missing time field" }, raw_message.consume_csv());
        std::string_view latitude_str = TRY_OR_RET(tl::unexpected { "missing latitude field" }, raw_message.consume_csv());
        std::string_view ns_str = TRY_OR_RET(tl::unexpected { "missing N/S indicator field" }, raw_message.consume_csv());
        std::string_view longitude_str = TRY_OR_RET(tl::unexpected { "missing longitude field" }, raw_message.consume_csv());
        std::string_view ew_str = TRY_OR_RET(tl::unexpected { "missing E/W indicator field" }, raw_message.consume_csv());
        std::string_view indicator_str = TRY_OR_RET(tl::unexpected { "missing gps connection indicator field" }, raw_message.consume_csv());
        std::string_view num_sat_str = TRY_OR_RET(tl::unexpected { "missing satellite count field" }, raw_message.consume_csv());
        std::string_view hdop_str = TRY_OR_RET(tl::unexpected { "missing HDOP field" }, raw_message.consume_csv());
        std::string_view msl_altitude_str
          = TRY_OR_RET(tl::unexpected { "missing MSL altitude field" }, raw_message.consume_csv());
        std::ignore = TRY_OR_RET(tl::unexpected { "missing unit (first) field" }, raw_message.consume_csv());
        std::string_view geoidal_sep_str
          = TRY_OR_RET(tl::unexpected { "missing geoidal separation field" }, raw_message.consume_csv());
        std::ignore = TRY_OR_RET(tl::unexpected { "missing unit (second) field" }, raw_message.consume_csv());
        //std::string_view dgps_age_str = TRY_OR_RET(tl::unexpected { "missing DGPS age field" }, raw_message.consume_csv());
        //std::string_view dgps_ref_str = TRY_OR_RET(tl::unexpected { "missing DGPS reference field" }, raw_message.consume_csv());

        int32_t current_time = Tele::get_time();
        int32_t base_time = current_time - current_time % 86400;
        int32_t time = base_time + TRY_OR_RET(tl::unexpected { "" }, parse_time(time_str));
        set_time(time);

        int num_satellites;
        float msl_altitude;
        float hdop;

        if (auto res = std::from_chars(num_sat_str.begin(), num_sat_str.end(), num_satellites); res.ec != std::errc())
            return tl::unexpected { "" };

        if (auto res = std::from_chars(hdop_str.begin(), hdop_str.end(), msl_altitude); res.ec != std::errc())
            return tl::unexpected { "" };

        if (auto res = std::from_chars(hdop_str.begin(), hdop_str.end(), hdop); res.ec != std::errc())
            return tl::unexpected { "" };

        return GGAMessage {
            .unix_time = time,
            .latitude = TRY_OR_RET(tl::unexpected { "bad latitude string" }, parse_degrees(latitude_str)),
            .longitude = TRY_OR_RET(tl::unexpected { "bad longitude string" }, parse_degrees(longitude_str)),

            .position_fix_indicator = PositionFixIndicator::GPSSPS,
            .num_satellites = num_satellites,
            .msl_altitude = msl_altitude,
            .hdop = hdop,
        };
    }

    return tl::unexpected { "WIP" };
}

}
