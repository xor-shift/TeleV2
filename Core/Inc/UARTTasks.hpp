#pragma once

#include <functional>

#include <cmsis_os.h>
#include <main.h>
#include <stream_buffer.h>

template<typename Callback> struct ReceiveTask {
    constexpr ReceiveTask(UART_HandleTypeDef& huart, Callback&& cb = {}) noexcept
        : m_huart(huart)
        , m_callback(std::move(cb)) { }

    void create_buffer() {
        size_t sz = m_buffer_storage.size();
        m_stream = xStreamBufferCreateStatic(sz, 1, m_buffer_storage.data(), &m_buffer);
    }

    void create_task() {
        m_task = xTaskCreate(
          ReceiveTask::task, "uart rx task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 2, nullptr
        );
    }

    [[noreturn]] void operator()() {
        for (;;) {
            void* rx_buf_ptr = reinterpret_cast<void*>(&m_staging_byte);
            xStreamBufferReceive(m_stream, rx_buf_ptr, 1, portMAX_DELAY);
            std::invoke(m_callback, m_staging_byte);
        }
    }

    [[noreturn]] static void task(void* arg) {
        auto& self = *reinterpret_cast<ReceiveTask*>(arg);
        self();
    }

    void isr_rx_cplt() {
        xStreamBufferSendFromISR(m_stream, m_uart_rx_buffer.data(), m_uart_rx_buffer.size(), nullptr);
    }

    void begin_rx() {
        for (;;) {
            HAL_StatusTypeDef res = HAL_UART_Receive_IT(&m_huart, m_uart_rx_buffer.data(), m_uart_rx_buffer.size());
            if (res == HAL_OK)
                break;
        }
    }

private:
    UART_HandleTypeDef& m_huart;
    Callback m_callback;

    std::array<uint8_t, 1> m_uart_rx_buffer;

    uint8_t m_staging_byte;

    BaseType_t m_task;
    std::array<uint8_t, 32> m_buffer_storage;
    StaticStreamBuffer_t m_buffer;
    StreamBufferHandle_t m_stream;
};

struct TransmitTask {
    constexpr TransmitTask(UART_HandleTypeDef& huart) noexcept
        : m_huart(huart) { }

    void create() {
        create_buffer();
        create_task();
    }

    void create_buffer() {
        size_t sz = m_buffer_storage.size();
        m_stream = xStreamBufferCreateStatic(sz, 1, m_buffer_storage.data(), &m_buffer);
    }

    void create_task() {
        m_task = xTaskCreate(
          TransmitTask::task, "uart tx task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 2, nullptr
        );
    }

    [[noreturn]] void operator()() {
        for (;;) {
            uint8_t byte_to_send;
            bool pending = false;

            for (;;) {
                while (!pending) {
                    void* buf_ptr = reinterpret_cast<void*>(&byte_to_send);
                    auto sz = xStreamBufferReceive(m_stream, buf_ptr, 1, portMAX_DELAY);

                    if (sz != 0)
                        pending = true;
                }

                while (HAL_UART_Transmit_IT(&m_huart, &byte_to_send, 1) != HAL_OK) {
                    osThreadYield();
                }

                pending = false;
            }
        }
    }

    [[noreturn]] static void task(void* arg) {
        auto& self = *reinterpret_cast<TransmitTask*>(arg);
        self();
    }

    constexpr StreamBufferHandle_t& stream() { return m_stream; }

private:
    UART_HandleTypeDef& m_huart;

    BaseType_t m_task;
    std::array<uint8_t, 32> m_buffer_storage;
    StaticStreamBuffer_t m_buffer;
    StreamBufferHandle_t m_stream;
};
