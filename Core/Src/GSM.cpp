#include <GSM.hpp>

#include <chrono>

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

    if (line.starts_with("+CGATT: ")) {
        switch (line.back()) {
        case '0': return GPRSStatus { false };
        case '1': return GPRSStatus { true };
        default: return tl::unexpected { "bad gprs status" };
        }
    }

    if (auto [res, cid, status, address] = scn::scan_tuple<char, char, std::string>(line, "+SAPBR: {},{},{}"); res) {
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

    /*
    if (auto res = ctre::match<"^\\+HTTPACTION: ([012]),([0-9]{3}),([0-9]+)">(line); res) {
        //"+HTTPACTION: 0,200,38"

        auto [whole, method_str, code_str, length_str] = res;

        TRY_PARSE(int, method, method_str);
        TRY_PARSE(int, code, code_str);
        TRY_PARSE(size_t, length, length_str);

        HTTPResponseReady ret {
            .code = code,
            .body_length = length,
        };

        switch (method) {
        case static_cast<int>(HTTPRequestType::GET): ret.method = HTTPRequestType::GET; break;
        case static_cast<int>(HTTPRequestType::POST): ret.method = HTTPRequestType::POST; break;
        case static_cast<int>(HTTPRequestType::HEAD): ret.method = HTTPRequestType::HEAD; break;
        default: return tl::unexpected { "bad http request method" };
        }

        return ret;
    }

    if (auto res = ctre::match<"^\\+SAPBR ([123]): DEACT">(line); res) {
        //
    }
     */

    return tl::unexpected { "line did not match any known replies" };

#undef TRY_PARSE
}

}

void TimerModule::operator()() {
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

void MainModule::reset_state() {
    m_ready = false;
    m_functional = false;
    m_have_sim = false;
    m_call_ready = false;
    m_sms_ready = false;

    m_bearer_open = false;
    m_gprs_open = false;

    m_requested_bearer = false;
    m_requested_gprs = false;
    m_requested_http = false;
    m_requested_reset = false;
}

void MainModule::periodic_callback() {
    auto bail = [this] {
        m_coordinator->send_command(
          this,
          Command::CFUN {
            .fun_type = CFUNType::Full,
            .reset_before = true,
          },
          false
        );
        m_timer = 0;
        reset_state();
    };

    m_timer += 1;

    if (m_timer >= 60) { // 30 seconds
        if (!m_bearer_open) {
            bail();
            return;
        }
    } else if (m_timer >= 40) { // 20 seconds
        if (!m_call_ready || !m_sms_ready) {
            bail();
            return;
        }
    }

    if (!m_call_ready || !m_sms_ready)
        return;

    if (!m_bearer_open) {
        if (!m_requested_bearer) {
            m_requested_bearer = true;

            m_coordinator->send_command(
              this,
              Command::SetBearerParameter {
                .profile = BearerProfile::Profile0,
                .tag = "Contype",
                .value = "GPRS",
              },
              false
            );

            m_coordinator->send_command(
              this,
              Command::SetBearerParameter {
                .profile = BearerProfile::Profile0,
                .tag = "APN",
                .value = "internet",
              },
              false
            );

            m_coordinator->send_command(
              this,
              Command::OpenBearer {
                .profile = BearerProfile::Profile0,
              },
              false
            );
        }

        m_coordinator->send_command(
          this,
          Command::QueryBearerParameters {
            .profile = BearerProfile::Profile0,
          },
          false
        );

        return;
    }

    if (!m_gprs_open) {
        if (!m_requested_gprs) {
            m_requested_gprs = true;

            m_coordinator->send_command(this, Command::AttachToGPRS {}, false);
        }

        m_coordinator->send_command(this, Command::QueryGPRS {}, false);
        return;
    }

    if (!m_requested_http) {
        m_requested_http = true;
        m_coordinator->send_command(this, Command::HTTPInit {}, false);
        m_coordinator->send_command(this, Command::HTTPSetBearer { .profile = BearerProfile::Profile0 }, false);
        m_coordinator->send_command(
          this, Command::HTTPSetUA { .user_agent = "https://github.com/xor-shift/TeleV2" }, false
        );
        return;
    }

    if (!m_requested_reset) {
        m_requested_reset = true;
        m_coordinator->send_command(this, Command::HTTPSetURL { .url = Tele::Config::Endpoints::reset_request }, false);
        m_coordinator->send_command(this, Command::HTTPMakeRequest { .request_type = HTTPRequestType::GET }, false);
        return;
    }

    // all's well, periodically check sanity (and location (and time))

    switch (m_timer % 10) { // 5 second timeslots
    case 0: m_coordinator->send_command(this, Command::QueryGPRS {}, false); break;
    case 5: m_coordinator->send_command(this, Command::QueryBearerParameters { BearerProfile::Profile0 }, false); break;
    default: break;
    }

    switch (m_timer % 60) { // 30 seconds timeslots
    case 51: m_coordinator->send_command(this, Command::QueryPositionAndTime {}, false); break;
    default: break;
    }
}

void MainModule::incoming_reply(GSM::Coordinator&, Reply::reply_type const& reply) {
    Stf::MultiVisitor visitor {
        [this](Reply::PeriodicMessage const& message) { periodic_callback(); },
        [this](Reply::Ready const&) { m_ready = true; },
        [this](Reply::CFUN const&) { m_functional = true; },
        [this](Reply::CPIN const&) { m_have_sim = true; },
        [this](Reply::SMSReady const&) { m_sms_ready = true; },
        [this](Reply::CallReady const&) { m_call_ready = true; },
        [this](Reply::GPRSStatus const& reply) { m_gprs_open = reply.attached; },
        [this](Reply::PositionAndTime const& reply) { Tele::set_time(reply.unix_time); },
        [this](Reply::BearerParameters const& reply) {
            if (reply.profile != BearerProfile::Profile0) {
                return;
            }

            if (reply.status == BearerStatus::Connected) {
                m_bearer_open = true;
            } else {
                m_bearer_open = false;
                m_gprs_open = false;
            }
        },
        [this](Reply::HTTPResponseReady const& reply) {
            m_coordinator->send_command(this, Command::HTTPRead {}, false);
        },
        [](auto) {},
    };

    /*if (!std::holds_alternative<Reply::PeriodicMessage>(reply)) {
        m_timer = 0;
    }*/

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

void Coordinator::send_command_now(Command::command_type const& command) {
    std::visit(
      [this](auto const& cmd) {
          Log::debug("GSM Coordinator", "Sending a \"{}\" command", cmd.name);

          auto str = fmt::format("{}\r\n", cmd);
          StreamBufferHandle_t& handle = m_transmit_task.stream();

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

}

void Coordinator::operator()() {
    queue_elem_type elem = CommandElement {};

    std::vector<Reply::reply_type> reply_buffer;
    std::optional<CommandElement> active_command = std::nullopt;

    auto new_reply = [&](Reply::reply_type&& reply) {
        bool solicited = std::visit(
          [&active_command]<typename T>(T const&) {
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
        bool finish_buffer = std::holds_alternative<Reply::Okay>(reply);

        reply_buffer.emplace_back(std::move(reply));

        if (!finish_buffer)
            return;

        Log::debug("GSM Reply Buffer", "OK reply, finishing buffer with size {}", reply_buffer.size());

        if (!active_command) {
            Log::debug("GSM Reply Buffer", "active_command does not have a value!");
            return;
        }

        Log::debug(
          "GSM Reply Buffer", "the module that requested the command was situated at {}",
          reinterpret_cast<void*>(active_command->who)
        );

        fullfill_command(std::move(*active_command), reply_buffer);
        active_command = std::nullopt;
        reply_buffer.clear();
    };

    Tele::DelimitedReader line_reader {
        [&](std::string_view line, bool overflown) {
            Log::trace("GSM Coordinator", "Received line: {}", Tele::EscapedString { line });
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

            new_reply(std::move(*res));

            /*for (Module* module : m_registered_modules) {
                module->incoming_reply(*this, reply);
            }*/
        },
        std::span(m_line_buffer),
        "\r\n",
    };

    Stf::MultiVisitor visitor {
        [&line_reader](DataElement const& elem) {
            line_reader.add_chars({ data(elem.data), elem.sz }); //
        },
        [this](CommandElement const& elem) { send_command_now(elem.command); },
    };

    for (;;) {
        if (xQueueReceive(m_queue_handle, &elem, portMAX_DELAY) != pdTRUE)
            Error_Handler();

        if (std::holds_alternative<DataElement>(elem)) {
            DataElement const& data = std::get<DataElement>(elem);

            line_reader.add_chars({ std::data(data.data), std::data(data.data) + data.sz });
        } else if (std::holds_alternative<CommandElement>(elem)) {
            CommandElement const& command = std::get<CommandElement>(elem);

            active_command = command;
        }

        // visit(visitor, elem);
    }
}

}
