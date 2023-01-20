#include <GSM.hpp>

#include <chrono>
#include <deque>

#include <scn/scn.h>
#include <scn/tuple_return.h>

#include <Stuff/Util/Visitor.hpp>

#include <cmsis_os.h>

#include <Globals.hpp>
#include <secrets.hpp>
#include <stdcompat.hpp>
#include <util.hpp>

namespace GSM {

namespace Reply {

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

        Tele::set_time(unix_time);

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

    return tl::unexpected { "line did not match any known replies" };

#undef TRY_PARSE
}

}

[[noreturn]] void TimerModule::operator()() {
    for (;;) {
        vTaskDelay(500);

        if (!m_timer_cleared || m_coordinator == nullptr)
            continue;

        m_timer_cleared = false;
        m_coordinator->forge_reply(this, Reply::PeriodicMessage { .time = 500 });
    }
}

void TimerModule::incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) {
    if (std::holds_alternative<Reply::PeriodicMessage>(reply))
        m_timer_cleared = true;
}

void LoggerModule::registered(GSM::Coordinator* coordinator) {
    Module::registered(coordinator);

    Log::debug("GSM Logger", "Registered to the coordinator at {}", static_cast<void*>(coordinator));
}

void LoggerModule::incoming_reply(Coordinator&, Reply::reply_type const& reply) {
    Stf::MultiVisitor visitor {
        [](Reply::PeriodicMessage const&) { /*Log::trace("GSM Logger", "Received a periodic message");*/ },
        [](auto const& reply) { Log::debug("GSM Logger", "Received a \"{}\" reply", reply.name); }
    };

    std::visit(visitor, reply);
}

bool MainModule::initialize() {
    bool was_open = true;
    for (size_t i = 0; !m_ready; i++) {
        vTaskDelay(100);
        if (i >= 50) {
            was_open = false;
            break;
        }
    }

    Log::info("GSM Main Module", "the module was{} open", was_open ? "" : "n't");

    // std::ignore = m_coordinator->send_command_async(this, Command::AT {});
    // std::ignore = m_coordinator->send_command_async(this, Command::SetErrorVerbosity { ErrorVerbosity::MEEString });
    if (!was_open) {
        std::ignore = m_coordinator->send_command_async(this, Command::CFUN { CFUNType::Full, true });
    }

    for (size_t i = 0; !m_sms_ready || !m_call_ready; i++) {
        vTaskDelay(100);
        if (i >= 150) // 15 secs
            return false;
    }

    m_coordinator->send_command_async(this, Command::SetBearerParameter { BearerProfile::Profile0, "Contype", "GPRS" });
    m_coordinator->send_command_async(this, Command::SetBearerParameter { BearerProfile::Profile0, "APN", "internet" });
    m_coordinator->send_command_async(this, Command::OpenBearer { BearerProfile::Profile0 });

    for (size_t i = 0;; i++) {
        vTaskDelay(100);
        if (i >= 150) // 15 secs
            return false;

        Command::QueryBearerParameters cmd { BearerProfile::Profile0 };
        Reply::BearerParameters reply;

        if (!extract_single_reply(reply, m_coordinator->send_command_async(this, cmd)))
            continue;

        if (reply.status != BearerStatus::Connected || reply.profile != BearerProfile::Profile0)
            continue;

        break;
    }

    m_coordinator->send_command_async(this, Command::AttachToGPRS {});

    for (size_t i = 0;; i++) {
        vTaskDelay(100);
        if (i >= 150) // 15 secs
            return false;

        Command::QueryGPRS cmd {};
        Reply::GPRSStatus reply;

        if (!extract_single_reply(reply, m_coordinator->send_command_async(this, cmd)))
            continue;

        if (!reply.attached)
            continue;

        break;
    }

    Reply::ResetChallenge challenge;
    std::tie(std::ignore, challenge, std::ignore) = TRY_OR_RET(
      false, extract_replies_from_range<Reply::HTTPResponse, Reply::ResetChallenge, Reply::Okay>(
               TRY_OR_RET(false, http_request(Tele::Config::Endpoints::reset_request, HTTPRequestType::GET))
             )
    );

    auto signature = P256::sign(Tele::g_privkey, data(challenge.challenge));
    std::array<char, 128> challenge_response_buffer;
    std::span<char> challenge_response_r { begin(challenge_response_buffer), begin(challenge_response_buffer) + 64 };
    std::span<char> challenge_response_s { begin(challenge_response_buffer) + 64, end(challenge_response_buffer) };
    std::ignore = Tele::to_chars<uint32_t>({ signature.r }, challenge_response_r, std::endian::big);
    std::ignore = Tele::to_chars<uint32_t>({ signature.s }, challenge_response_s, std::endian::big);

    std::ignore = http_request(
      Tele::Config::Endpoints::reset_request, HTTPRequestType::POST, "text/plain",
      { begin(challenge_response_buffer), end(challenge_response_buffer) }
    );

    return true;
}

int MainModule::main() {
    if (!initialize()) {
        return 1;
    }

    for (;;) {
        vTaskDelay(1000);
    }

    return -1;
}

std::optional<Reply::HTTPResponseReady> MainModule::wait_for_http(BaseType_t timeout_decisecs) {
    std::optional<Reply::HTTPResponseReady> ret = std::nullopt;

    for (size_t i = 0; i < timeout_decisecs; i++) {
        if (!m_last_http_response) {
            vTaskDelay(100);
            continue;
        }

        std::swap(ret, m_last_http_response);
        break;
    }

    return ret;
}

std::optional<std::vector<Reply::reply_type>> MainModule::http_request(
  std::string_view url, HTTPRequestType method, std::string_view content_type, std::string_view content
) {
    m_coordinator->send_command_async(this, Command::HTTPInit {});
    m_coordinator->send_command_async(this, Command::HTTPSetBearer { BearerProfile::Profile0 });
    m_coordinator->send_command_async(this, Command::HTTPSetUA { "https://github.com/xor-shift/TeleV2" });
    m_coordinator->send_command_async(this, Command::HTTPSetURL { url });

    if (content_type != "") {
        m_coordinator->send_command_async(this, Command::HTTPContentType { content_type });
        m_coordinator->send_command_async(this, Command::HTTPData { .data = { begin(content), end(content) } });
    }

    m_coordinator->send_command_async(this, Command::HTTPMakeRequest { method });

    TRYX(wait_for_http());

    auto ret = m_coordinator->send_command_async(this, Command::HTTPRead {});

    m_coordinator->send_command_async(this, Command::HTTPTerm {});

    return ret;
}

[[noreturn]] void MainModule::operator()() {
    if (m_coordinator == nullptr)
        Error_Handler();

    static const TickType_t s_fail_wait = 2500;

    for (size_t retries = 0;; retries++) {
        int res = main();
        Log::warn("GSM Main", "the main procedure failed with code {}", res);
        Log::warn("GSM Main", "waiting {} ticks before restarting for retry no #{}", s_fail_wait, retries + 1);
        vTaskDelay(s_fail_wait);

        m_ready = false;
        m_functional = false;
        m_have_sim = false;
        m_call_ready = false;
        m_sms_ready = false;
        m_bearer_open = false;
        m_gprs_open = false;
    }
}

void MainModule::incoming_reply(GSM::Coordinator&, Reply::reply_type const& reply) {
    Stf::MultiVisitor visitor {
        [this](Reply::Ready const&) { m_ready = true; },
        [this](Reply::CFUN const&) { m_functional = true; },
        [this](Reply::CPIN const&) { m_have_sim = true; },
        [this](Reply::SMSReady const&) { m_sms_ready = true; },
        [this](Reply::CallReady const&) { m_call_ready = true; },
        [this](Reply::HTTPResponseReady const& reply) {
            // FIXME: race
            // this function is called from another thread, we could be reading m_last_http_response while it is being
            // written into
            // make ABSOLUTELY SURE that there are no more than one requests being made at a time, if that is even
            // possible
            // TODO: add an unsolicited reply queue to all modules and signal unsolicited replies using that.
            // (and never call any function of the modules from within the coordinator's thread)
            m_last_http_response = reply;
        },
        [](auto) {},
    };

    std::visit(visitor, reply);
}

}

// coordinator
namespace GSM {

void Coordinator::begin_rx() {
    for (;;) {
        HAL_StatusTypeDef res
          = HAL_UARTEx_ReceiveToIdle_DMA(&m_huart, m_uart_rx_buffer.data(), m_uart_rx_buffer.size());
        if (res == HAL_OK)
            break;
    }
}

void Coordinator::isr_rx_event(uint16_t start_idx, uint16_t end_idx) {
    std::span<uint8_t> rx_buffer { m_uart_rx_buffer };
    rx_buffer = rx_buffer.subspan(start_idx, end_idx - start_idx);

    Tele::in_chunks<uint8_t>(rx_buffer, DataElement::size, [this](std::span<uint8_t> chunk) {
        DataElement data_elem {};
        data_elem.sz = std::min(DataElement::size, size(chunk));

        std::copy_n(data(chunk), data_elem.sz, data(data_elem.data));

        queue_elem_type elem = data_elem;

        BaseType_t higher_prio_task_awoken = pdFALSE;
        xQueueSendFromISR(m_queue_handle, &elem, &higher_prio_task_awoken);
        portYIELD_FROM_ISR(higher_prio_task_awoken);

        return data_elem.sz;
    });
}

void Coordinator::create(const char* name) {
    m_queue_handle = xQueueCreateStatic(k_queue_size, sizeof(queue_elem_type), data(m_queue_storage), &m_static_queue);

    if (m_queue_handle == nullptr)
        Error_Handler();

    StaticTask::create(name);
}

size_t Coordinator::register_module(Module* module) {
    module->registered(this);
    size_t ret = m_registered_modules.size();
    m_registered_modules.push_back(module);
    return ret;
}

uint32_t Coordinator::send_command(Module* who, Command::command_type&& command, bool in_isr) {
    uint32_t order = next_command_order++;

    CommandElement cmd_elem {
        .order = order,
        .who = who,
        .command = command,
    };

    queue_elem_type elem = cmd_elem;

    if (in_isr) {
        BaseType_t higher_prio_task_awoken = pdFALSE;
        xQueueSendFromISR(m_queue_handle, &elem, &higher_prio_task_awoken);
        portYIELD_FROM_ISR(higher_prio_task_awoken);
    } else {
        xQueueSend(m_queue_handle, &elem, portMAX_DELAY);
    }

    return order;
}

std::vector<Reply::reply_type> Coordinator::send_command_async(Module* who, Command::command_type&& command) {
    std::vector<Reply::reply_type> container {};
    uint32_t order = next_command_order++;

    SemaphoreHandle_t sema = xSemaphoreCreateBinary();

    queue_elem_type elem = CommandElement {
        .order = order,
        .who = who,

        .reply_container = &container,
        .sema = sema,

        .command = command,
    };

    xQueueSend(m_queue_handle, &elem, portMAX_DELAY);
    // FIXME: race!
    // realistically, the sim800l will not respond before a context switch back to this task happens
    // i will leave this comment as-is even if i have to eat my words
    if (xSemaphoreTake(sema, portMAX_DELAY) != pdTRUE) {
        Error_Handler();
    }

    return container;
}

void Coordinator::send_command_now(Command::command_type const& command) {
    std::visit(
      [this]<typename T>(T const& cmd) {
          Log::debug("GSM Coordinator", "sending a \"{}\" command", cmd.name);

          auto str = fmt::format("{}\r\n", cmd);
          StreamBufferHandle_t& handle = m_transmit_task.stream();
          // Log::debug("GSM Coordinator", "sending line: {}", str);

          Tele::in_chunks(std::span(begin(str), end(str)), 16, [&handle](std::span<const char> chunk) {
              size_t sent = xStreamBufferSend(handle, data(chunk), size(chunk), portMAX_DELAY);
              return sent;
          });
      },
      command
    );
}

void Coordinator::forge_reply(Module* who, Reply::reply_type&& reply) {
    for (Module* module : m_registered_modules) {
        // if (module == who)
        //     continue;
        module->incoming_reply(*this, reply);
    }
}

void Coordinator::fullfill_command(CommandElement&& cmd, std::span<Reply::reply_type> replies) {
    for (auto const& reply : replies) {
        cmd.who->incoming_reply(*this, reply);
    }
}

struct CoordinatorQueueHelper {
    Coordinator& coordinator;

    std::vector<Reply::reply_type> reply_buffer {};
    std::optional<Coordinator::CommandElement> active_command = std::nullopt;
    std::deque<Coordinator::CommandElement> command_queue {};

    void new_reply(Reply::reply_type&& reply) {
        bool solicited = std::visit(
          [&]<typename T>(T const&) -> bool {
              if constexpr (std::is_same_v<typename T::solicit_type, solicit_type_never>)
                  return false;
              else if constexpr (std::is_same_v<typename T::solicit_type, solicit_type_always>)
                  return true;
              else
                  return active_command.has_value()
                      && std::holds_alternative<typename T::solicit_type>(active_command->command);
          },
          reply
        );

        // allow for snooping
        for (Module* module : coordinator.m_registered_modules) {
            module->incoming_reply(coordinator, reply);
        }

        if (!solicited) {
            std::visit(
              []<typename T>(T const&) {
                  Log::debug(
                    "GSM Reply Buffer", "unsolicited reply of type \"{}\" was not pushed to the buffer", T::name
                  );
              },
              reply
            );
            return;
        }

        if (active_command && std::holds_alternative<Command::HTTPData>(active_command->command) && std::holds_alternative<Reply::HTTPReadyForData>(reply)) {
            Command::HTTPData& http_data = std::get<Command::HTTPData>(active_command->command);
            Log::debug("GSM Coordinator", "since the active command is HTTPDATA, sending additional data...");

            StreamBufferHandle_t& handle = coordinator.m_transmit_task.stream();
            Tele::in_chunks(http_data.data, 16, [&handle](std::span<const char> chunk) {
                size_t sent = xStreamBufferSend(handle, data(chunk), size(chunk), portMAX_DELAY);
                return sent;
            });
        }

        bool finish_buffer = std::holds_alternative<Reply::Okay>(reply);

        reply_buffer.emplace_back(std::move(reply));

        if (!finish_buffer)
            return;

        // Log::debug("GSM Reply Buffer", "OK reply, finishing buffer with size {}", reply_buffer.size());

        if (!active_command) {
            Log::warn("GSM Reply Buffer", "active_command does not have a value!");
            return;
        }

        /*Log::debug(
          "GSM Reply Buffer", "the module that requested the command was situated at {}",
          reinterpret_cast<void*>(active_command->who)
        );*/

        coordinator.fullfill_command(std::move(*active_command), reply_buffer);

        if (active_command->sema != nullptr) {
            active_command->reply_container->reserve(reply_buffer.size());
            copy(begin(reply_buffer), end(reply_buffer), back_inserter(*active_command->reply_container));
            if (xSemaphoreGive(active_command->sema) != pdTRUE) {
                Error_Handler();
            }
        }

        active_command = std::nullopt;
        reply_buffer.clear();

        queue_action();
    }

    void new_command(Coordinator::CommandElement&& new_command) {
        command_queue.emplace_back(std::move(new_command));

        queue_action();
    }

private:
    void queue_action() {
        if (active_command.has_value())
            return;

        if (!command_queue.empty()) {
            Coordinator::CommandElement new_command = command_queue.front();
            command_queue.pop_front();
            command_queue.shrink_to_fit();

            active_command = new_command;
            coordinator.send_command_now(new_command.command);
        }
    }
};

void Coordinator::operator()() {
    CoordinatorQueueHelper helper {
        .coordinator = *this,
    };

    Tele::DelimitedReader line_reader {
        [&](std::string_view line, bool overflown) {
            // Log::trace("GSM Coordinator", "Received line: {}", Tele::EscapedString { line });
            if (overflown) {
                Log::warn("GSM Coordinator", "Last line was cut short due to an overflow");
            }

            if (line.empty())
                return;

            auto res = Reply::parse_reply(line);

            if (!res) {
                Log::warn("GSM Coordinator", "Parsing a line failed with message: {}", res.error());
                Log::warn("GSM Coordinator", "The errored line was: {}", Tele::EscapedString { line });
                return;
            }

            helper.new_reply(std::move(*res));
        },
        std::span(m_line_buffer),
        "\r\n",
    };

    for (queue_elem_type elem = CommandElement {};;) {
        if (xQueueReceive(m_queue_handle, &elem, portMAX_DELAY) != pdTRUE)
            Error_Handler();

        if (std::holds_alternative<DataElement>(elem)) {
            DataElement const& data = std::get<DataElement>(elem);
            line_reader.add_chars({ std::data(data.data), std::data(data.data) + data.sz });
        } else if (std::holds_alternative<CommandElement>(elem)) {
            helper.new_command(std::move(std::get<CommandElement>(elem)));
        }
    }
}

}
