#pragma once

#include <array>
#include <atomic>
#include <variant>

#include <fmt/core.h>

#include <cmsis_os.h>
#include <queue.h>

#include <GSMCommands.hpp>
#include <UARTTasks.hpp>

namespace GSM {

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

    void fullfill_command(CommandElement&& cmd, std::span<Reply::reply_type> replies);
};

}
