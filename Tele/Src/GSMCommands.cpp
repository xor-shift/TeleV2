#include <Tele/GSMCommands.hpp>

#include <chrono>

#include <scn/scn.h>
#include <scn/tuple_return.h>

#include <Tele/CharConv.hpp>

namespace GSM::Reply {

tl::expected<reply_type, std::string_view> parse_reply(std::string_view line) {
#define TRY_PARSE(_type, _name, _str_name) \
    _type _name                            \
      = TRYX(Tele::from_chars<float>(_str_name.to_view()).map_error([](auto) { return "error parsing " #_name; }))

    if (line.empty())
        return tl::unexpected { "empty line" };

    line = line.substr(line.find_first_not_of("\r\n"));

    if (line.starts_with("OK")) {
        return Okay {};
    }

    if (line.starts_with("ERRO")) {
        return Error {};
    }

    if (line.starts_with("RDY")) {
        return Ready {};
    }

    if (line.starts_with("+CFUN")) {
        // TODO: parse the result
        return CFUN { .fun_type = CFUNType::Full };
    }

    if (line.starts_with("+CPIN")) {
        // TODO: parse the result
        return CPIN { .status = CPINStatus::Ready };
    }

    if (line.starts_with("Call R")) {
        return CallReady {};
    }

    if (line.starts_with("SMS R")) {
        return SMSReady {};
    }

    if (line.starts_with("DOWNLOAD")) {
        return Reply::HTTPReadyForData {};
    }

    if (line.starts_with("+CGATT: ")) {
        switch (line.back()) {
        case '0': return GPRSStatus { false };
        case '1': return GPRSStatus { true };
        default: return tl::unexpected { "bad gprs status" };
        }
    }

    if (auto [res, cid] = scn::scan_tuple<char>(line, "+SAPBR {}: DEACT"); res) {
        BearerParameters reply {
            .status = BearerStatus::Closed,
            .ipv4 = true,
            .ip_address = { { 1, 2, 3, 4 } },
        };

        switch (cid) {
        case '1': reply.profile = BearerProfile::Profile0; break;
        case '2': reply.profile = BearerProfile::Profile1; break;
        case '3': reply.profile = BearerProfile::Profile2; break;
        default: return tl::unexpected { "bad bearer profile" };
        }

        return reply;
    }

    if (auto [res, cid, status, address] = scn::scan_tuple<char, char, scn::string_view>(line, "+SAPBR: {},{},{}");
        res) {
        BearerParameters reply {
            .ipv4 = true,
            .ip_address = { { 1, 2, 3, 4 } },
        };

        switch (cid) {
        case '1': reply.profile = BearerProfile::Profile0; break;
        case '2': reply.profile = BearerProfile::Profile1; break;
        case '3': reply.profile = BearerProfile::Profile2; break;
        default: return tl::unexpected { "bad bearer profile" };
        }

        switch (status) {
        case '0': reply.status = BearerStatus::Connecting; break;
        case '1': reply.status = BearerStatus::Connected; break;
        case '2': reply.status = BearerStatus::Closing; break;
        case '3': reply.status = BearerStatus::Closed; break;
        default: return tl::unexpected { "bad bearer status" };
        }

        return reply;
    }

    if (auto [res, code, longitude, latitude, year, month, day, hour, minute, second]
        = scn::scan_tuple<int, float, float, int32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>(
          line, "+CIPGSMLOC: {},{},{},{}/{}/{},{}:{}:{}"
        );
        res) {
        using days = std::chrono::duration<int, std::ratio_multiply<std::ratio<24>, std::chrono::hours::period>>;
        using weeks = std::chrono::duration<int, std::ratio_multiply<std::ratio<7>, days::period>>;
        using years = std::chrono::duration<int, std::ratio_multiply<std::ratio<146097, 400>, days::period>>;
        using months = std::chrono::duration<int, std::ratio_divide<years::period, std::ratio<12>>>;

        // https://github.com/HowardHinnant/date/blob/22ceabf205d8d678710a43154da5a06b701c5830/include/date/date.h#L2973
        auto ymd_to_days = [](uint16_t y_, uint8_t m_, uint8_t d_) -> days {
            auto const y = static_cast<int32_t>(y_) - (m_ <= 2); // 2 -> February
            auto const m = static_cast<uint32_t>(m_);
            auto const d = static_cast<uint32_t>(d_);
            auto const era = (y >= 0 ? y : y - 399) / 400;
            auto const yoe = static_cast<uint32_t>(y - era * 400);            // [0, 399]
            auto const doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1; // [0, 365]
            auto const doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
            return days { era * 146097 + static_cast<int32_t>(doe) - 719468 };
        };

        std::chrono::time_point<std::chrono::system_clock, days> sys_days { ymd_to_days(year, month, day) };
        std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> sys_seconds { sys_days };
        int32_t unix_time = static_cast<int32_t>(sys_seconds.time_since_epoch().count());
        unix_time += hour * 3600;
        unix_time += minute * 60;
        unix_time += second;

        return PositionAndTime {
            .status_code = code,
            .unix_time = unix_time,
            .longitude = longitude,
            .latitude = latitude,
        };
    }

    if (auto [res, method, status_code, data_length]
        = scn::scan_tuple<char, int, size_t>(line, "+HTTPACTION: {},{},{}");
        res) {

        HTTPResponseReady ret {
            .code = status_code,
            .body_length = data_length,
        };

        switch (method) {
        case '0': ret.method = HTTPRequestType::GET; break;
        case '1': ret.method = HTTPRequestType::POST; break;
        case '2': ret.method = HTTPRequestType::HEAD; break;
        default: return tl::unexpected { "bad http method" };
        }

        return ret;
    }

    if (auto [res, data_length] = scn::scan_tuple<size_t>(line, "+HTTPREAD: {}"); res) {
        return Reply::HTTPResponse {
            .body_size = data_length,
        };
    }

    if (auto [res, challenge] = scn::scan_tuple<scn::string_view>(line, "+CST_RESET_CHALLENGE {}"); res) {
        if (challenge.size() != 64) {
            return tl::unexpected { "bad challenge length" };
        }

        std::array<uint8_t, 32> bigint;
        auto conv_res = Tele::from_chars<uint8_t>({ bigint }, { challenge }, std::endian::big);
        if (conv_res.ec != std::errc()) {
            return tl::unexpected { "bad challenge integer" };
        }

        return Reply::ResetChallenge {
            .challenge = bigint,
        };
    }

    if (auto [res, code] = scn::scan_tuple<int>(line, "+CST_RESET_FAIL {}"); res) {
        return ResetFailure { code };
    }

    if (auto [res, rng_vector_str] = scn::scan_tuple<scn::string_view>(line, "+CST_RESET_SUCC {}"); res) {
        if (rng_vector_str.size() != 32) {
            return tl::unexpected { "bad pRNG vector length" };
        }

        std::array<uint32_t, 4> prng_vector;
        auto conv_res = Tele::from_chars<uint32_t>({ prng_vector }, { rng_vector_str }, std::endian::big);
        if (conv_res.ec != std::errc()) {
            return tl::unexpected { "bad pRNG vector" };
        }

        return ResetSuccess { prng_vector };
    }

    return tl::unexpected { "line did not match any known replies" };

#undef TRY_PARSE
}

}