#pragma once

#include <array>
#include <atomic>
#include <variant>

#include <fmt/core.h>

#include <cmsis_os.h>
#include <queue.h>

#include <UARTTasks.hpp>

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

namespace Reply {

// this message is sent every 500ms for timeouts to fail etc.
struct PeriodicMessage {
    inline static constexpr const char* name = "[Periodic Message]";
    uint32_t time;
};

struct Okay {
    inline static constexpr const char* name = "OK";
};

struct Ready {
    inline static constexpr const char* name = "RDY";
};

struct CFUN {
    inline static constexpr const char* name = "CFUN";
    CFUNType fun_type = CFUNType::Full;
};

struct CPIN {
    inline static constexpr const char* name = "CPIN";
    CPINStatus status;
};

struct BearerParameters {
    inline static constexpr const char* name = "SAPBR(2)";

    BearerProfile profile;
    BearerStatus status;
    bool ipv4;
    std::array<uint8_t, 16> ip_address;
};

struct CallReady {
    inline static constexpr const char* name = "Call Ready";
};

struct SMSReady {
    inline static constexpr const char* name = "SMS Ready";
};

struct GPRSStatus {
    inline static constexpr const char* name = "CGATT(?)";

    bool attached = false;
};

struct PositionAndTime {
    inline static constexpr const char* name = "CIPGSMLOC";

    int status_code;
    int32_t unix_time;
    float longitude;
    float latitude;
};

struct HTTPResponseReady {
    inline static constexpr const char* name = "HTTPACTION";

    HTTPRequestType method;
    int code;
    size_t body_length;
};

using reply_type = std::variant<
  PeriodicMessage, Okay, Ready, CFUN, CPIN, BearerParameters, CallReady, SMSReady, GPRSStatus, PositionAndTime,
  HTTPResponseReady>;

tl::expected<reply_type, std::string_view> parse_reply(std::string_view line);

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

using command_type = std::variant<
  AT, Echo, CFUN, SetBearerParameter, QueryBearerParameters, OpenBearer, CloseBearer, AttachToGPRS, QueryGPRS,
  DetachFromGPRS, QueryPositionAndTime, HTTPInit, HTTPSetBearer, HTTPSetURL, HTTPSetUA, HTTPMakeRequest, HTTPRead>;

};

struct Coordinator;

struct Module {
    virtual ~Module() = default;

    virtual void registered(Coordinator* coordinator) { m_coordinator = coordinator; }

    virtual void incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) {
        //
    }

protected:
    Coordinator* m_coordinator = nullptr;
};

struct TimerModule
    : Module
    , Tele::StaticTask<4096> {
    // REMINDER TO SELF: this one needs a lot of stack space because of forge_reply works.
    // when we forge a reply, all modules get executed by the forger itself.

    virtual ~TimerModule() override = default;

    void incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) final override;

protected:
    [[noreturn]] void operator()() final override;

private:
    std::atomic_bool m_timer_cleared = true;
};

struct LoggerModule final : Module {
    virtual ~LoggerModule() override = default;

    void registered(Coordinator* coordinator) final override;

    void incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) final override;
};

struct MainModule : Module {
    virtual ~MainModule() override = default;

    void incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) final override;

private:
    uint32_t m_timer = 0;

    bool m_ready = false;
    bool m_functional = false;
    bool m_have_sim = false;
    bool m_call_ready = false;
    bool m_sms_ready = false;

    bool m_bearer_open = false;
    bool m_gprs_open = false;

    bool m_requested_bearer = false;
    bool m_requested_gprs = false;
    bool m_requested_http = false;
    bool m_requested_reset = false;

    void periodic_callback();

    void reset_state();

    /*enum class ConnectionStage {
        NoSIM = 0,
        HaveSIM,
        SetBearerSent,
        BearerSet,
        OpenBearerSent,
        BearerOpen,
        OpenGPRSSent,
        GPRSOpen,
    } m_connection_stage = ConnectionStage::NoSIM;*/
};

struct Coordinator : Tele::StaticTask<8192> {
    static constexpr size_t k_queue_size = 32;

    struct CommandElement {
        uint32_t order = 0;
        Module* who = nullptr;
        Command::command_type command = Command::AT {};
    };

    struct DataElement {
        inline static constexpr size_t size = sizeof(CommandElement) - sizeof(size_t);

        size_t sz;
        std::array<char, size> data {};
    };

    static_assert(sizeof(CommandElement) == sizeof(DataElement));

    using queue_elem_type = std::variant<CommandElement, DataElement>;

    constexpr Coordinator(UART_HandleTypeDef& huart, TransmitTask& transmit_task) noexcept
        : m_huart(huart)
        , m_transmit_task(transmit_task) { }

    Coordinator(Coordinator const&) = delete;
    Coordinator(Coordinator&) = delete;

    void begin_rx();

    void isr_rx_event(uint16_t start_idx, uint16_t end_idx);

    void create(const char* name) override;

    size_t register_module(Module* module);

    uint32_t send_command(Module* who, Command::command_type&& command, bool in_isr);

    // do NOT call from ISRs
    void send_command_now(Command::command_type const& command);

    void forge_reply(Module* who, Reply::reply_type&& reply);

protected:
    [[noreturn]] void operator()() override;

private:
    UART_HandleTypeDef& m_huart;
    TransmitTask& m_transmit_task;

    std::vector<Module*> m_registered_modules {};
    std::atomic_uint32_t next_command_order = 0;

    std::array<char, 1024> m_line_buffer {};

    std::array<uint8_t, 32> m_uart_rx_buffer;

    std::array<uint8_t, k_queue_size * sizeof(queue_elem_type)> m_queue_storage;
    StaticQueue_t m_static_queue;
    QueueHandle_t m_queue_handle = nullptr;
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
