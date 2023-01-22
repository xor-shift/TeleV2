#pragma once

#include <atomic>
#include <vector>
#include <optional>

#include <cmsis_os.h>
#include <semphr.h>

#include <Tele/GSMCommands.hpp>
#include <Tele/UARTTasks.hpp>

namespace GSM {

struct Coordinator;

struct Module {
    virtual ~Module() = default;

    virtual void registered(Coordinator* coordinator) { m_coordinator = coordinator; }

    virtual void incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) = 0;

protected:
    Coordinator* m_coordinator = nullptr;
};

struct Coordinator : Tele::StaticTask<4096> {
    static constexpr size_t k_queue_size = 32;
    friend struct CoordinatorQueueHelper;

    struct CommandElement {
        uint32_t order = 0;
        Module* who = nullptr;

        std::vector<Reply::reply_type>* reply_container = nullptr;
        SemaphoreHandle_t sema = nullptr;

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

    std::vector<Reply::reply_type> send_command_async(Module* who, Command::command_type&& command);

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

    void fullfill_command(CommandElement&& cmd, std::span<Reply::reply_type> replies);
};

// utilities

namespace Detail {

template<size_t OrigSize, typename T, typename... Ts>
constexpr bool reply_extractor_helper(auto& out, std::span<Reply::reply_type> in) {
    Reply::reply_type&& head = std::move(in.front());

    if (!std::holds_alternative<T>(head))
        return false;

    std::get<OrigSize - sizeof...(Ts) - 1>(out) = std::move(std::get<T>(std::move(head)));

    if constexpr (sizeof...(Ts) != 0) {
        return reply_extractor_helper<OrigSize, Ts...>(out, in.subspan(1));
    } else {
        return true;
    }
}

}

template<typename... Ts> constexpr std::optional<std::tuple<Ts...>> extract_replies_from_range(auto&& replies) {
    if (replies.size() != sizeof...(Ts))
        return std::nullopt;

    std::tuple<Ts...> ret;
    if (!Detail::reply_extractor_helper<sizeof...(Ts), Ts...>(ret, { replies }))
        return std::nullopt;

    return ret;
}

template<typename T> constexpr bool extract_single_reply(T& out, auto&& replies) {
    if (replies.size() != 2)
        return false;

    if (!std::holds_alternative<T>(replies[0]))
        return false;

    if (!std::holds_alternative<Reply::Okay>(replies[1]))
        return false;

    out = std::get<T>(replies[0]);
    return true;
}

}
