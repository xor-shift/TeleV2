#pragma once

#include <array>
#include <string_view>
#include <variant>

#include <fmt/core.h>
#include <tl/expected.hpp>

namespace GSM {

enum class CFUNType : int {
    Minimum = 0,
    // default
    Full = 1,
    DisableTxRxCircuits = 4,
};

enum class CPINStatus {
    Ready,
    AwaitingPIN,
    AwaitingPIN2,
    AwaitingPINPH,
    AwaitingPUK,
    AwaitingPUK2,
    AwaitingPUKPH,

};

enum class BearerProfile : int {
    Profile0 = 1,
    Profile1 = 2,
    Profile2 = 3,
};

enum class BearerStatus : int {
    Connecting = 0,
    Connected = 1,
    Closing = 2,
    Closed = 3,
};

enum class HTTPRequestType : int {
    GET = 0,
    POST = 1,
    HEAD = 2,
};

using solicit_type_never = std::integral_constant<int, 0>;
using solicit_type_always = std::integral_constant<int, 1>;

namespace Reply {

struct PeriodicMessage;
struct Okay;
struct Ready;
struct CFUN;
struct CPIN;
struct BearerParameters;
struct CallReady;
struct SMSReady;
struct GPRSStatus;
struct PositionAndTime;
struct HTTPResponseReady;

using reply_type = std::variant<
  PeriodicMessage, Okay, Ready, CFUN, CPIN, BearerParameters, CallReady, SMSReady, GPRSStatus, PositionAndTime,
  HTTPResponseReady>;

tl::expected<reply_type, std::string_view> parse_reply(std::string_view line);

};

namespace Command {

struct AT;
struct Echo;
struct CFUN;
struct SetBearerParameter;
struct QueryBearerParameters;
struct OpenBearer;
struct CloseBearer;
struct AttachToGPRS;
struct QueryGPRS;
struct DetachFromGPRS;
struct QueryPositionAndTime;
struct HTTPInit;
struct HTTPSetBearer;
struct HTTPSetURL;
struct HTTPSetUA;
struct HTTPMakeRequest;
struct HTTPRead;

using command_type = std::variant<
  AT, Echo, CFUN, SetBearerParameter, QueryBearerParameters, OpenBearer, CloseBearer, AttachToGPRS, QueryGPRS,
  DetachFromGPRS, QueryPositionAndTime, HTTPInit, HTTPSetBearer, HTTPSetURL, HTTPSetUA, HTTPMakeRequest, HTTPRead>;


};

namespace Reply {

// this message is sent every 500ms for timeouts to fail etc.
struct PeriodicMessage {
    using solicit_type = solicit_type_never;
    inline static constexpr const char* name = "[Periodic Message]";

    uint32_t time;
};

struct Okay {
    using solicit_type = solicit_type_always;
    inline static constexpr const char* name = "OK";
};

struct Ready {
    using solicit_type = solicit_type_never;
    inline static constexpr const char* name = "RDY";
};

struct CFUN {
    using solicit_type = Command::CFUN;
    inline static constexpr const char* name = "CFUN";

    CFUNType fun_type = CFUNType::Full;
};

struct CPIN {
    using solicit_type = solicit_type_never;
    inline static constexpr const char* name = "CPIN";

    CPINStatus status;
};

struct BearerParameters {
    using solicit_type = Command::QueryBearerParameters;
    inline static constexpr const char* name = "SAPBR(2)";

    BearerProfile profile;
    BearerStatus status;
    bool ipv4;
    std::array<uint8_t, 16> ip_address;
};

struct CallReady {
    using solicit_type = solicit_type_never;

    inline static constexpr const char* name = "Call Ready";
};

struct SMSReady {
    using solicit_type = solicit_type_never;

    inline static constexpr const char* name = "SMS Ready";
};

struct GPRSStatus {
    using solicit_type = Command::QueryGPRS;
    inline static constexpr const char* name = "CGATT(?)";

    bool attached = false;
};

struct PositionAndTime {
    using solicit_type = Command::QueryPositionAndTime;
    inline static constexpr const char* name = "CIPGSMLOC";

    int status_code;
    int32_t unix_time;
    float longitude;
    float latitude;
};

struct HTTPResponseReady {
    using solicit_type = solicit_type_never;
    inline static constexpr const char* name = "HTTPACTION";

    HTTPRequestType method;
    int code;
    size_t body_length;
};

};

namespace Command {

struct AT {
    inline static constexpr const char* name = "AT";
};

struct Echo {
    inline static constexpr const char* name = "ATE";

    bool on = true;
};

struct CFUN {
    inline static constexpr const char* name = "CFUN";

    CFUNType fun_type = CFUNType::Full;
    // will reset the modem before setting the mode
    bool reset_before = false;
};

struct SetBearerParameter {
    inline static constexpr const char* name = "SAPBR(3)";

    BearerProfile profile = BearerProfile::Profile0;
    std::string_view tag;
    std::string_view value;
};

struct QueryBearerParameters {
    inline static constexpr const char* name = "SAPBR(2)";

    BearerProfile profile = BearerProfile::Profile0;
};

struct OpenBearer {
    inline static constexpr const char* name = "SAPBR(1)";

    BearerProfile profile = BearerProfile::Profile0;
};

struct CloseBearer {
    inline static constexpr const char* name = "SAPBR(0)";

    BearerProfile profile = BearerProfile::Profile0;
};

struct AttachToGPRS {
    inline static constexpr const char* name = "CGATT(1)";
};

struct QueryGPRS {
    inline static constexpr const char* name = "CGATT(?)";
};

struct DetachFromGPRS {
    inline static constexpr const char* name = "CGATT(0)";
};

struct QueryPositionAndTime {
    inline static constexpr const char* name = "CIPGSMLOC";

    BearerProfile profile = BearerProfile::Profile0;
};

struct HTTPInit {
    inline static constexpr const char* name = "HTTPINIT";
};

struct HTTPSetBearer {
    inline static constexpr const char* name = "HTTPARA(CID)";

    BearerProfile profile = BearerProfile::Profile0;
};

struct HTTPSetURL {
    inline static constexpr const char* name = "HTTPARA(URL)";

    // must be a compile time constant
    std::string_view url;
};

struct HTTPSetUA {
    inline static constexpr const char* name = "HTTPARA(UA)";

    // must be a compile time constant
    std::string_view user_agent = "https://github.com/xor-shift/TeleV2";
};

struct HTTPMakeRequest {
    inline static constexpr const char* name = "HTTPACTION";

    HTTPRequestType request_type = HTTPRequestType::GET;
};

struct HTTPRead {
    inline static constexpr const char* name = "HTTPREAD";
};

};

}

#define FORMATTER_FACTORY(_type, ...)                                                                          \
    template<> struct fmt::formatter<_type> {                                                                  \
        template<typename ParseContext> constexpr auto parse(ParseContext& ctx) { return ctx.begin(); }        \
        template<typename FormatContext> constexpr auto format([[maybe_unused]] _type v, FormatContext& ctx) { \
            return fmt::format_to(ctx.out(), __VA_ARGS__);                                                     \
        }                                                                                                      \
    }

FORMATTER_FACTORY(GSM::Command::CFUN, "AT+CFUN={}{}", static_cast<int>(v.fun_type), v.reset_before ? ",1" : "");
FORMATTER_FACTORY(GSM::Command::AT, "AT");
FORMATTER_FACTORY(GSM::Command::Echo, "ATE{}", v.on ? "1" : "0");
FORMATTER_FACTORY(
  GSM::Command::SetBearerParameter, "AT+SAPBR=3,{},\"{}\",\"{}\"", static_cast<int>(v.profile), v.tag, v.value
);
FORMATTER_FACTORY(GSM::Command::QueryBearerParameters, "AT+SAPBR=2,{}", static_cast<int>(v.profile));
FORMATTER_FACTORY(GSM::Command::OpenBearer, "AT+SAPBR=1,{}", static_cast<int>(v.profile));
FORMATTER_FACTORY(GSM::Command::CloseBearer, "AT+SAPBR=0,{}", static_cast<int>(v.profile));
FORMATTER_FACTORY(GSM::Command::AttachToGPRS, "AT+CGATT=1");
FORMATTER_FACTORY(GSM::Command::QueryGPRS, "AT+CGATT?");
FORMATTER_FACTORY(GSM::Command::DetachFromGPRS, "AT+CGATT=0");
FORMATTER_FACTORY(GSM::Command::QueryPositionAndTime, "AT+CIPGSMLOC=1,{}", static_cast<int>(v.profile));

FORMATTER_FACTORY(GSM::Command::HTTPInit, "AT+HTTPINIT");
FORMATTER_FACTORY(GSM::Command::HTTPSetBearer, "AT+HTTPPARA=\"CID\",{}", static_cast<int>(v.profile));
FORMATTER_FACTORY(GSM::Command::HTTPSetURL, "AT+HTTPPARA=\"URL\",{}", v.url);
FORMATTER_FACTORY(GSM::Command::HTTPSetUA, "AT+HTTPPARA=\"UA\",{}", v.user_agent);
FORMATTER_FACTORY(GSM::Command::HTTPMakeRequest, "AT+HTTPACTION={}", static_cast<int>(v.request_type));
FORMATTER_FACTORY(GSM::Command::HTTPRead, "AT+HTTPREAD");

#undef FORMATTER_FACTORY