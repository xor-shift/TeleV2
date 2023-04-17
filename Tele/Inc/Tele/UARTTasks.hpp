#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string_view>

#include <cmsis_os.h>
#include <queue.h>
#include <stm32f4xx_hal.h>
#include <stm32f4xx_hal_uart.h>
#include <stream_buffer.h>

#include <fmt/format.h>

#include <Tele/Delimited.hpp>
#include <Tele/STUtilities.hpp>
#include <Tele/StaticTask.hpp>

namespace Tele {

struct TxDelimitedRxTask : Task<1024, false> {
private:
    struct UARTQueueElement {
        inline static constexpr size_t chunk_size = 16;

        char data[chunk_size];
        uint16_t sz_rx;

        constexpr bool is_rx() const { return (sz_rx & 1) != 0; }
        constexpr uint16_t data_sz() const { return sz_rx >> 1; }

        constexpr void set_sz_rx(uint16_t sz, bool rx) {
            sz_rx = sz << 1;
            if (rx)
                sz_rx |= 1;
        }
    };

    using LineQueueElement = std::string*;

public:
    TxDelimitedRxTask(
      std::string_view task_name_base, UART_HandleTypeDef& uart_handle, size_t line_buffer_size,
      std::string_view delimiter
    );

    ~TxDelimitedRxTask();

    void create(const char* name) override;

    void transmit(std::span<const char> data);

    /// @return
    /// The string received from the queue, if any. If the value equals nullptr, the queue was empty.
    /// @remarks
    /// The string returned from this function must be freed (call delete[] on ret->data())
    std::string* receive_line_now();

    /// @return
    /// The string received from the queue. The pointer will never equal nullptr.
    /// @remarks
    /// The string returned from this function must be freed (call delete[] on ret->data())
    std::string* receive_line();

    void begin_rx();

    void isr_rx_event(UART_HandleTypeDef* huart, uint16_t offset);

protected:
    [[noreturn]] void operator()() override;

private:
    std::string m_task_name;

    UART_HandleTypeDef& m_uart_handle;
    std::string_view m_delimiter;
    size_t m_line_buffer_size;
    char* m_line_buffer;

    std::array<uint8_t, UARTQueueElement::chunk_size> m_uart_tx_buffer;

    uint16_t m_last_uart_offset = 0;
    std::array<uint8_t, UARTQueueElement::chunk_size * 2> m_uart_rx_buffer;

    QueueHandle_t m_uart_queue;
    QueueHandle_t m_line_queue;
};

struct TransmitTask : Task<128, false> {
    TransmitTask(UART_HandleTypeDef& huart) noexcept
        : m_huart(huart) {
        size_t sz = m_buffer_storage.size();
        m_stream = xStreamBufferCreateStatic(sz, 1, m_buffer_storage.data(), &m_buffer);
    }

    size_t transmit(std::span<const char> data) {
        return Tele::in_chunks(data, 16, [this](std::span<const char> chunk) {
            size_t sent = xStreamBufferSend(m_stream, chunk.data(), chunk.size(), portMAX_DELAY);
            return sent;
        });
    }

protected:
    [[noreturn]] void operator()() override {
        for (;;) {
            size_t pending_size = 0;

            for (;;) {
                while (pending_size == 0) {
                    pending_size
                      = xStreamBufferReceive(m_stream, m_uart_buffer.data(), m_uart_buffer.size(), portMAX_DELAY);
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

    std::array<uint8_t, 32> m_uart_buffer;

    std::array<uint8_t, 32> m_buffer_storage;
    StaticStreamBuffer_t m_buffer;
    StreamBufferHandle_t m_stream;
};

}
