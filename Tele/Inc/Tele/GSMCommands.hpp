#pragma once

#include <array>
#include <span>
#include <string_view>
#include <variant>

#include <fmt/core.h>
#include <tl/expected.hpp>

namespace Tele::GSM {

enum class CFUNType : int {
    Minimum = 0,
    // default
    Full = 1,
    DisableTxRxCircuits = 4,
};

enum class BaudRate : int {
    AutoBaud = 0,
    BPS1k2 = 1200,
    BPS2k4 = 2400,
    BPS4k8 = 4800,
    BPS9k6 = 9600,
    BPS19k2 = 19200,
    BPS38k4 = 38400,
    BPS57k6 = 57600,
    BPS115k2 = 115200,
    BPS230k4 = 230400,
    BPS460k8 = 460800,
};

enum class ErrorVerbosity : int {
    DisableMEE = 0,
    MEECode = 1,
    MEEString = 2,
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
struct Error;
struct Ready;
struct CFUN;
struct CPIN;
struct BearerParameters;
struct CallReady;
struct SMSReady;
struct GPRSStatus;
struct PositionAndTime;
struct HTTPResponseReady;
struct HTTPResponse;
struct HTTPReadyForData;

struct ResetChallenge;
struct ResetFailure;
struct ResetSuccess;

using reply_type = std::variant<
  PeriodicMessage, Okay, Error, Ready, CFUN, CPIN, BearerParameters, CallReady, SMSReady, GPRSStatus, PositionAndTime,
  HTTPResponseReady, HTTPResponse, HTTPReadyForData, ResetChallenge, ResetFailure, ResetSuccess>;

tl::expected<reply_type, std::string_view> parse_reply(std::string_view line);

};

namespace Command {

struct AT;
struct SetBaud;
struct SetErrorVerbosity;
struct SaveToNVRAM;
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
struct HTTPTerm;
struct HTTPSetBearer;
struct HTTPSetURL;
struct HTTPSetUA;
struct HTTPContentType;
struct HTTPMakeRequest;
struct HTTPRead;
struct HTTPData;

using command_type = std::variant<
  AT, SetBaud, SetErrorVerbosity, SaveToNVRAM, Echo, CFUN, SetBearerParameter, QueryBearerParameters, OpenBearer,
  CloseBearer, AttachToGPRS, QueryGPRS, DetachFromGPRS, QueryPositionAndTime, HTTPInit, HTTPTerm, HTTPSetBearer,
  HTTPSetURL, HTTPSetUA, HTTPMakeRequest, HTTPRead, HTTPContentType, HTTPData>;

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

struct Error {
    using solicit_type = solicit_type_always;
    inline static constexpr const char* name = "ERROR";
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

struct HTTPResponse {
    using solicit_type = Command::HTTPRead;
    inline static constexpr const char* name = "HTTPREAD";

    size_t body_size;
};

struct HTTPReadyForData {
    using solicit_type = Command::HTTPData;
    inline static constexpr const char* name = "HTTPDATA(DOWNLOAD)";
};

// custom ones

struct ResetChallenge {
    using solicit_type = Command::HTTPRead;
    inline static constexpr const char* name = "CST_RESET_CHALLENGE";

    std::array<uint8_t, 32> challenge;
};

struct ResetFailure {
    using solicit_type = Command::HTTPRead;
    inline static constexpr const char* name = "CST_RESET_FAIL";

    int code;
};

struct ResetSuccess {
    using solicit_type = Command::HTTPRead;
    inline static constexpr const char* name = "CST_RESET_SUCC";

    std::array<uint32_t, 4> prng_vector;
};

};

namespace Command {

struct AT {
    inline static constexpr const char* name = "AT";

    static std::string_view format() { return "AT"; }
};

struct SetErrorVerbosity {
    inline static constexpr const char* name = "CMEE";

    ErrorVerbosity verbosity = ErrorVerbosity::DisableMEE;

    static std::string format() { return "AT"; }
};

struct SaveToNVRAM {
    inline static constexpr const char* name = "AT&W";
};

struct SetBaud {
    inline static constexpr const char* name = "IPR";
    BaudRate baud_rate = BaudRate::AutoBaud;
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

struct HTTPTerm {
    inline static constexpr const char* name = "HTTPTERM";
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

struct HTTPContentType {
    inline static constexpr const char* name = "HTTPARA(CONTENT)";

    std::string_view content_type;
};

struct HTTPMakeRequest {
    inline static constexpr const char* name = "HTTPACTION";

    HTTPRequestType request_type = HTTPRequestType::GET;
};

struct HTTPRead {
    inline static constexpr const char* name = "HTTPREAD";
};

/// IMPORTANT NOTICE: the data `data` is pointing to *must* stay valid until an OK is received. NEVER EVER send this
/// without waiting for a reply.
struct HTTPData {
    inline static constexpr const char* name = "HTTPDATA";

    std::span<const char> data;
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

// clang-format off
FORMATTER_FACTORY(Tele::GSM::Command::CFUN, "AT+CFUN={}{}", static_cast<int>(v.fun_type), v.reset_before ? ",1" : "");
FORMATTER_FACTORY(Tele::GSM::Command::AT, "AT");
FORMATTER_FACTORY(Tele::GSM::Command::SetBaud, "AT+IPR={}", static_cast<int>(v.baud_rate));
FORMATTER_FACTORY(Tele::GSM::Command::SaveToNVRAM, "AT&W");
FORMATTER_FACTORY(Tele::GSM::Command::SetErrorVerbosity, "AT+CMEE={}", static_cast<int>(v.verbosity));
FORMATTER_FACTORY(Tele::GSM::Command::Echo, "ATE{}", v.on ? "1" : "0");
FORMATTER_FACTORY(Tele::GSM::Command::SetBearerParameter, "AT+SAPBR=3,{},\"{}\",\"{}\"", static_cast<int>(v.profile), v.tag, v.value);
FORMATTER_FACTORY(Tele::GSM::Command::QueryBearerParameters, "AT+SAPBR=2,{}", static_cast<int>(v.profile));
FORMATTER_FACTORY(Tele::GSM::Command::OpenBearer, "AT+SAPBR=1,{}", static_cast<int>(v.profile));
FORMATTER_FACTORY(Tele::GSM::Command::CloseBearer, "AT+SAPBR=0,{}", static_cast<int>(v.profile));
FORMATTER_FACTORY(Tele::GSM::Command::AttachToGPRS, "AT+CGATT=1");
FORMATTER_FACTORY(Tele::GSM::Command::QueryGPRS, "AT+CGATT?");
FORMATTER_FACTORY(Tele::GSM::Command::DetachFromGPRS, "AT+CGATT=0");
FORMATTER_FACTORY(Tele::GSM::Command::QueryPositionAndTime, "AT+CIPGSMLOC=1,{}", static_cast<int>(v.profile));

FORMATTER_FACTORY(Tele::GSM::Command::HTTPInit, "AT+HTTPINIT");
FORMATTER_FACTORY(Tele::GSM::Command::HTTPTerm, "AT+HTTPTERM");
FORMATTER_FACTORY(Tele::GSM::Command::HTTPSetBearer, "AT+HTTPPARA=\"CID\",{}", static_cast<int>(v.profile));
FORMATTER_FACTORY(Tele::GSM::Command::HTTPSetURL, "AT+HTTPPARA=\"URL\",\"{}\"", v.url);
FORMATTER_FACTORY(Tele::GSM::Command::HTTPSetUA, "AT+HTTPPARA=\"UA\",\"{}\"", v.user_agent);
FORMATTER_FACTORY(Tele::GSM::Command::HTTPContentType, "AT+HTTPPARA=\"CONTENT\",\"{}\"", v.content_type);
FORMATTER_FACTORY(Tele::GSM::Command::HTTPMakeRequest, "AT+HTTPACTION={}", static_cast<int>(v.request_type));
FORMATTER_FACTORY(Tele::GSM::Command::HTTPRead, "AT+HTTPREAD");
FORMATTER_FACTORY(Tele::GSM::Command::HTTPData, "AT+HTTPDATA={},{}", v.data.size(), std::max(1000uz, std::min(120000uz, v.data.size() * 10 / 9600)));
// clang-format on

#undef FORMATTER_FACTORY
