#pragma once

#include <charconv>
#include <functional>
#include <stdexcept>
#include <string_view>

#include <fmt/core.h>

#include <cmsis_os.h>
#include <queue.h>
#include <stream_buffer.h>

#include <Tele/StaticTask.hpp>
#include <Tele/Stream.hpp>

namespace Tele {

struct TerminalTask : StaticTask<2048> {
    static constexpr size_t k_queue_size = 32;

    struct QueueElement {
        static constexpr size_t max_sz = 32;

        std::array<char, max_sz> data;
        size_t sz;
        bool incoming;

        constexpr std::string_view str() const { return { data.data(), sz }; }
    };

    virtual ~TerminalTask() override = default;

    void create(const char* name) override;

    void isr_new_chars(std::string_view str);

    void send_str(std::string_view str);

protected:
    [[noreturn]] void operator()() override;

    virtual void new_line(std::string_view line) {}

private:
    std::array<uint8_t, k_queue_size * sizeof(QueueElement)> m_queue_storage;
    StaticQueue_t m_static_queue;
    QueueHandle_t m_queue_handle = nullptr;

    std::array<uint8_t, 32> m_stream_buffer_storage;
    StaticStreamBuffer_t m_static_stream_buffer;
    StreamBufferHandle_t m_stream_buffer_handle = nullptr;

    std::string_view m_prompt = "> ";

    void raw_send(std::string_view buffer);

    // when this is called, the prompt line is empty
    void input_line_callback(std::string_view line);
};

}
