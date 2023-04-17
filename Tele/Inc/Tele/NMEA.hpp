#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include <tl/expected.hpp>

namespace Tele::NMEA {

enum class Talker {
    Unknown,
    Beidou,
    Galileo,
    GPS,
    GLONASS,
};

struct RawMessage {
    // max message length is 82 characters
    // we're being conservative and assuming that the entire message can be
    // formed entirely of the comma separated value list (which would allow
    // for 42 data elements)
    inline static constexpr size_t max_data_count = 42;

    std::string_view full_message;

    Talker talker;
    std::string_view message_type;

    std::string_view csv;

    std::optional<std::string_view> consume_csv() {
        if (csv.empty())
            return std::nullopt;

        auto pos = csv.find(',');
        std::string_view ret = csv.substr(0, pos);

        if (pos != std::string_view::npos) {
            csv = csv.substr(pos + 1);
        } else {
            csv = "";
        }

        return ret;
    }
};

enum class PositionFixIndicator { Invalid = 0, GPSSPS = 1, DiffGPSSPS = 2, DeadReckoning = 6 };

struct GGAMessage {
    int32_t unix_time;
    float latitude;
    float longitude;

    PositionFixIndicator position_fix_indicator;
    int num_satellites;
    float msl_altitude;
    float hdop;
};

using message_type = std::variant<GGAMessage>;

tl::expected<message_type, std::string_view> parse_line(std::string_view str);

}
