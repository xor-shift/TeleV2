#pragma once

#include <functional>
#include <span>
#include <string_view>

#include <stm32f4xx_hal.h>
#include <stm32f4xx_hal_uart.h>
#include <cmsis_os.h>
#include <stream_buffer.h>

#include <Tele/StaticTask.hpp>
#include <Tele/STUtilities.hpp>

template<typename Callback> struct ReceiveTask : Tele::StaticTask<1024> {
    constexpr ReceiveTask(UART_HandleTypeDef& huart, Callback&& cb = {}) noexcept
        : m_huart(huart)
        , m_callback(std::move(cb)) { }

    void create(const char* name) override {
        size_t sz = m_buffer_storage.size();
        m_stream = xStreamBufferCreateStatic(sz, 1, m_buffer_storage.data(), &m_buffer);
        StaticTask::create(name);
    }

    void begin_rx() {
        for (;;) {
            HAL_StatusTypeDef res
              = HAL_UARTEx_ReceiveToIdle_DMA(&m_huart, m_uart_rx_buffer.data(), m_uart_rx_buffer.size());
            if (res == HAL_OK)
                break;
        }
    }

    void isr_rx_event(uint16_t start_idx, uint16_t end_idx) {
        std::span<uint8_t> rx_buffer { m_uart_rx_buffer };
        rx_buffer = rx_buffer.subspan(start_idx, end_idx - start_idx);

        Tele::in_chunks<uint8_t>(rx_buffer, m_buffer_storage.size() / 2, [this](std::span<uint8_t> chunk) {
            BaseType_t higher_prio_task_awoken = pdFALSE;
            size_t res = xStreamBufferSendFromISR(m_stream, chunk.data(), chunk.size(), &higher_prio_task_awoken);
            portYIELD_FROM_ISR(higher_prio_task_awoken);
            return res;
        });
    }

protected:
    [[noreturn]] void operator()() override {
        std::array<char, 32> staging_bytes;

        for (;;) {
            void* rx_buf_ptr = reinterpret_cast<void*>(staging_bytes.data());
            size_t amt_read = xStreamBufferReceive(m_stream, rx_buf_ptr, staging_bytes.size(), portMAX_DELAY);
            std::invoke(m_callback, std::string_view(staging_bytes.data(), amt_read));
        }
    }

private:
    UART_HandleTypeDef& m_huart;
    Callback m_callback;

    std::array<uint8_t, 64> m_uart_rx_buffer;

    std::array<uint8_t, 32> m_buffer_storage;
    StaticStreamBuffer_t m_buffer;
    StreamBufferHandle_t m_stream;
};

struct TransmitTask : Tele::StaticTask<128> {
    constexpr TransmitTask(UART_HandleTypeDef& huart) noexcept
        : m_huart(huart) { }

    void create(const char* name) override {
        size_t sz = m_buffer_storage.size();
        m_stream = xStreamBufferCreateStatic(sz, 1, m_buffer_storage.data(), &m_buffer);

        StaticTask::create(name);
    }

    constexpr StreamBufferHandle_t& stream() { return m_stream; }

protected:
    [[noreturn]] void operator()() override {
        for (;;) {
            size_t pending_size = 0;

            for (;;) {
                while (pending_size == 0) {
                    pending_size
                      = xStreamBufferReceive(m_stream, m_uart_buffer.data(), m_uart_buffer.size(), portMAX_DELAY);
                }

                if (m_uart_buffer[0] == 'A' && m_uart_buffer[1] == '1') {
                    std::ignore = std::ignore;
                }

                while (HAL_UART_Transmit_DMA(&m_huart, m_uart_buffer.data(), pending_size) != HAL_OK) {
                    taskYIELD();
                }

                for (;;) {
                    HAL_UART_StateTypeDef status = HAL_UART_GetState(&m_huart);
                    if (status == HAL_UART_STATE_READY || status == HAL_UART_STATE_BUSY_RX)
                        break;

                    taskYIELD();
                }

                pending_size = 0;
            }
        }
    }

private:
    UART_HandleTypeDef& m_huart;

    std::array<uint8_t, 64> m_uart_buffer;

    std::array<uint8_t, 64> m_buffer_storage;
    StaticStreamBuffer_t m_buffer;
    StreamBufferHandle_t m_stream;
};
