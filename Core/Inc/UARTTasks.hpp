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
        m_handle = xTaskCreateStatic(
          ReceiveTask::task, "uart rx task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 2, m_stack,
          &m_static_task
        );
    }

    void begin_rx() {
        for (;;) {
            HAL_StatusTypeDef res
              = HAL_UARTEx_ReceiveToIdle_DMA(&m_huart, m_uart_rx_buffer.data(), m_uart_rx_buffer.size());
            if (res == HAL_OK)
                break;
        }
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

    void isr_rx_event(uint16_t start_idx, uint16_t end_idx) {
        std::span<uint8_t> rx_buffer { m_uart_rx_buffer };
        rx_buffer = rx_buffer.subspan(start_idx, end_idx - start_idx);

        if (rx_buffer.size() > 1)
            std::ignore = std::ignore;

        xStreamBufferSendFromISR(m_stream, rx_buffer.data(), rx_buffer.size(), nullptr);
    }

private:
    UART_HandleTypeDef& m_huart;
    Callback m_callback;

    std::array<uint8_t, 16> m_uart_rx_buffer;

    uint8_t m_staging_byte;

    std::array<uint8_t, 32> m_buffer_storage;
    StaticStreamBuffer_t m_buffer;
    StreamBufferHandle_t m_stream;

    TaskHandle_t m_handle;

    StackType_t m_stack[configMINIMAL_STACK_SIZE];
    StaticTask_t m_static_task;
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
        m_handle = xTaskCreateStatic(
          TransmitTask::task, "uart tx task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 2, m_stack,
          &m_static_task
        );
    }

    [[noreturn]] void operator()() {
        for (;;) {
            size_t pending_size = 0;

            for (;;) {
                while (pending_size == 0) {
                    pending_size
                      = xStreamBufferReceive(m_stream, m_uart_buffer.data(), m_uart_buffer.size(), portMAX_DELAY);
                }

                while (HAL_UART_Transmit_DMA(&m_huart, m_uart_buffer.data(), pending_size) != HAL_OK) {
                    osThreadYield();
                }

                pending_size = 0;
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

    std::array<uint8_t, 32> m_uart_buffer;

    std::array<uint8_t, 32> m_buffer_storage;
    StaticStreamBuffer_t m_buffer;
    StreamBufferHandle_t m_stream;

    TaskHandle_t m_handle;

    StackType_t m_stack[configMINIMAL_STACK_SIZE];
    StaticTask_t m_static_task;
};
