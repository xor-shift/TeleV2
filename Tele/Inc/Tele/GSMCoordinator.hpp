#pragma once

#include <atomic>
#include <optional>
#include <vector>

#include <cmsis_os.h>
#include <semphr.h>

#include <Tele/GSMCommands.hpp>
#include <Tele/UARTTasks.hpp>

namespace Tele::GSM {

struct Coordinator;

struct Module {
    virtual ~Module() = default;

    virtual void registered(Coordinator* coordinator) { m_coordinator = coordinator; }

    virtual void incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) { }

protected:
    Coordinator* m_coordinator = nullptr;
};

struct Coordinator
    : Module
    , Tele::StaticTask<2048> {
    static constexpr size_t k_queue_size = 32;
    friend struct CoordinatorQueueHelper;

    struct CommandElement {
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

    Coordinator(UART_HandleTypeDef& huart, TransmitTask& transmit_task)
        : m_huart(huart)
        , m_transmit_task(transmit_task)
        , m_queue_handle(
            xQueueCreateStatic(k_queue_size, sizeof(queue_elem_type), data(m_queue_storage), &m_static_queue)
          ) {

        if (m_queue_handle == nullptr)
            throw std::runtime_error("m_queue_handle is null??");
    }

    Coordinator(Coordinator const&) = delete;
    Coordinator(Coordinator&) = delete;

    void begin_rx();

    void isr_rx_event(UART_HandleTypeDef* huart, uint16_t offset);

    size_t register_module(Module* module);

    std::vector<Reply::reply_type> send_command_async(Module* who, Command::command_type&& command);

    void forge_reply(Module* who, Reply::reply_type&& reply);

    void reset_state() {
        m_ready = false;
        m_functional = false;
        m_have_sim = false;
        m_call_ready = false;
        m_sms_ready = false;
        m_state_inconsistent = false;
    }

    bool device_ready() const { return m_ready.load(); }
    bool device_functional() const { return m_functional.load(); }
    bool device_have_sim() const { return m_have_sim.load(); }
    bool device_call_ready() const { return m_call_ready.load(); }
    bool device_sms_ready() const { return m_sms_ready.load(); }
    bool device_inconsistent_state() const { return m_state_inconsistent.load(); }

protected:
    [[noreturn]] void operator()() override;

private:
    UART_HandleTypeDef& m_huart;
    TransmitTask& m_transmit_task;

    std::vector<Module*> m_registered_modules {};

    std::array<char, 1024> m_line_buffer;

    uint16_t m_uart_rx_offset = 0;
    std::array<uint8_t, 32> m_uart_rx_buffer;

    std::array<uint8_t, k_queue_size * sizeof(queue_elem_type)> m_queue_storage;
    StaticQueue_t m_static_queue;
    QueueHandle_t m_queue_handle = nullptr;

    // higher level control

    std::atomic_bool m_ready = false;
    std::atomic_bool m_functional = false;
    std::atomic_bool m_have_sim = false;
    std::atomic_bool m_call_ready = false;
    std::atomic_bool m_sms_ready = false;
    std::atomic_bool m_state_inconsistent = false;

    void send_command_now(Command::command_type const& command);

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
