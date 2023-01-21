#include <Tele/GSMCoordinator.hpp>

#include <list>

#include <Tele/Delimited.hpp>
#include <Tele/Format.hpp>
#include <Tele/Log.hpp>
#include <Tele/STUtilities.hpp>

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
        throw std::runtime_error("m_queue_handle is null??");

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
        throw std::runtime_error("xSemaphoreTake failed");
    }

    vSemaphoreDelete(sema);

    return container;
}

void Coordinator::send_command_now(Command::command_type const& command) {
    std::visit(
      [this]<typename T>(T const& cmd) {
          Log::debug("sending a \"{}\" command", cmd.name);

          auto str = fmt::format("{}\r\n", cmd);
          StreamBufferHandle_t& handle = m_transmit_task.stream();
          // Log::debug("sending line: {}", str);

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
    /*for (auto const& reply : replies) {
        cmd.who->incoming_reply(*this, reply);
    }*/
    if (cmd.sema != nullptr) {
        cmd.reply_container->reserve(replies.size());
        copy(begin(replies), end(replies), back_inserter(*cmd.reply_container));
        if (xSemaphoreGive(cmd.sema) != pdTRUE) {
            throw std::runtime_error("xSemaphoreGive failed");
        }
    }
}

struct CoordinatorQueueHelper {
    Coordinator& coordinator;

    std::vector<Reply::reply_type> reply_buffer {};
    std::optional<Coordinator::CommandElement> active_command = std::nullopt;
    std::list<Coordinator::CommandElement> command_queue {};

    bool is_solicited(Reply::reply_type const& reply) {
        return std::visit(
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
    }

    void snoop(Reply::reply_type const& reply) {
        for (Module* module : coordinator.m_registered_modules) {
            module->incoming_reply(coordinator, reply);
        }
    }

    void new_reply(Reply::reply_type&& reply) {
        snoop(reply);

        // unexpected restart, fail all waiting commands
        if (active_command && std::holds_alternative<Reply::Ready>(reply)) {
            Log::error(
              "received a RDY message with {} active command and {} queued commands", active_command ? "an" : "no",
              command_queue.size()
            );

            coordinator.fullfill_command(std::move(*active_command), reply_buffer);
            reply_buffer.clear();

            while (!command_queue.empty()) {
                Coordinator::CommandElement&& command = std::move(command_queue.front());
                coordinator.fullfill_command(std::move(command), {});
                command_queue.pop_front();
            }

            return;
        }

        bool solicited = is_solicited(reply);
        bool finish_buffer = std::holds_alternative<Reply::Okay>(reply) || std::holds_alternative<Reply::Error>(reply);

        if (active_command && std::holds_alternative<Command::HTTPData>(active_command->command) && std::holds_alternative<Reply::HTTPReadyForData>(reply)) {
            Command::HTTPData& http_data = std::get<Command::HTTPData>(active_command->command);
            Log::debug("since the active command is HTTPDATA, sending additional data...");

            StreamBufferHandle_t& handle = coordinator.m_transmit_task.stream();
            Tele::in_chunks(http_data.data, 16, [&handle](std::span<const char> chunk) {
                size_t sent = xStreamBufferSend(handle, data(chunk), size(chunk), portMAX_DELAY);
                return sent;
            });
        }

        if (!solicited) {
            std::visit(
              []<typename T>(T const&) {
                  Log::debug("unsolicited reply of type \"{}\" was not pushed to the buffer", T::name);
              },
              reply
            );
            return;
        }

        reply_buffer.emplace_back(std::move(reply));

        if (!finish_buffer)
            return;

        // Log::debug("OK reply, finishing buffer with size {}", reply_buffer.size());

        if (!active_command) {
            Log::warn("active_command does not have a value!");
            return;
        }

        /*Log::debug(
          "the module that requested the command was situated at {}",
          reinterpret_cast<void*>(active_command->who)
        );*/

        coordinator.fullfill_command(std::move(*active_command), reply_buffer);

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
            // Log::trace("Received line: {}", Tele::EscapedString { line });
            if (overflown) {
                Log::warn("Last line was cut short due to an overflow");
            }

            if (line.empty())
                return;

            auto res = Reply::parse_reply(line);

            if (!res) {
                Log::warn("Parsing a line failed with message: {}", res.error());
                Log::warn("The errored line was: {}", Tele::EscapedString { line });
                return;
            }

            helper.new_reply(std::move(*res));
        },
        std::span(m_line_buffer),
        "\r\n",
    };

    for (queue_elem_type elem = CommandElement {};;) {
        if (xQueueReceive(m_queue_handle, &elem, portMAX_DELAY) != pdTRUE) {
            throw std::runtime_error("xQueueReceive failed");
        }

        if (std::holds_alternative<DataElement>(elem)) {
            DataElement const& data = std::get<DataElement>(elem);
            line_reader.add_chars({ std::data(data.data), std::data(data.data) + data.sz });
        } else if (std::holds_alternative<CommandElement>(elem)) {
            helper.new_command(std::move(std::get<CommandElement>(elem)));
        }
    }
}

}
