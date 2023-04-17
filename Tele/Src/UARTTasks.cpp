#include <Tele/UARTTasks.hpp>

#include <Tele/Log.hpp>

namespace Tele {
TxDelimitedRxTask::TxDelimitedRxTask(
  std::string_view task_name_base, UART_HandleTypeDef& uart_handle, size_t line_buffer_size,
  std::string_view delimiter
)
    : m_task_name(fmt::format("txdrx({})", task_name_base))
    , m_uart_handle(uart_handle)
    , m_delimiter(delimiter)
    , m_line_buffer_size(line_buffer_size)
    , m_line_buffer(new char[line_buffer_size])
    , m_uart_queue(xQueueCreate(16, sizeof(UARTQueueElement)))
    , m_line_queue(xQueueCreate(16, sizeof(LineQueueElement))) {
    if (m_uart_queue == nullptr || m_line_queue == nullptr) {
        throw std::runtime_error("failed to create a queue");
    }
}

TxDelimitedRxTask::~TxDelimitedRxTask() {
    delete[] m_line_buffer;
    m_line_buffer = nullptr;
}

void TxDelimitedRxTask::create(const char* name) {
    Task::create(m_task_name.c_str());
}

void TxDelimitedRxTask::transmit(std::span<const char> data) {
    while (!data.empty()) {
        size_t transmit_sz = std::min(data.size(), UARTQueueElement::chunk_size);
        auto chunk_span = data.subspan(0, transmit_sz);
        data = data.subspan(transmit_sz);

        UARTQueueElement elem {};
        std::copy(chunk_span.begin(), chunk_span.end(), std::begin(elem.data));
        elem.set_sz_rx(transmit_sz, false);
    }
}

/// @remarks
/// The string returned from this function must be freed (call delete[] on ret->data())
std::string* TxDelimitedRxTask::receive_line_now() {
    std::string* elem;
    if (xQueueReceive(m_line_queue, &elem, 0) != pdTRUE)
        return nullptr;

    return elem;
}

std::string* TxDelimitedRxTask::receive_line() {
    LineQueueElement elem;
    if (xQueueReceive(m_line_queue, &elem, portMAX_DELAY) != pdTRUE)
        throw std::runtime_error("xQueueReceive failed");

    return elem;
}

void TxDelimitedRxTask::begin_rx() {
    for (;;) {
        HAL_StatusTypeDef res
          = HAL_UARTEx_ReceiveToIdle_DMA(&m_uart_handle, m_uart_rx_buffer.data(), m_uart_rx_buffer.size());
        if (res == HAL_OK)
            break;
    }
}

void TxDelimitedRxTask::isr_rx_event(UART_HandleTypeDef* huart, uint16_t offset) {
    if (huart != &m_uart_handle)
        return;

    if (m_last_uart_offset == offset)
        return;

    if (m_last_uart_offset > offset)
        m_last_uart_offset = 0;

    std::span<uint8_t> rx_buffer { m_uart_rx_buffer };
    rx_buffer = rx_buffer.subspan(m_last_uart_offset, offset - m_last_uart_offset);

    m_last_uart_offset = offset;

    size_t n_chunks
      = Tele::in_chunks<uint8_t>(rx_buffer, UARTQueueElement::chunk_size, [this](std::span<uint8_t> chunk) {
            UARTQueueElement elem {};

            std::copy(chunk.begin(), chunk.end(), std::begin(elem.data));

            elem.set_sz_rx(chunk.size(), true);

            BaseType_t higher_prio_task_awoken = pdFALSE;
            xQueueSendFromISR(m_uart_queue, &elem, &higher_prio_task_awoken);
            portYIELD_FROM_ISR(higher_prio_task_awoken);

            return chunk.size();
        });
}

void TxDelimitedRxTask::operator()() {
    Tele::DelimitedReader line_reader {
        [&](std::string_view line, bool overflown) {
            // Log::debug("{}", line);
            std::string* line_ptr = new std::string(line.begin(), line.end());
            if (xQueueSend(m_line_queue, &line_ptr, 0) != pdTRUE) {
                delete line_ptr;
            }
        },
        std::span(m_line_buffer, m_line_buffer_size),
        m_delimiter,
    };

    for (UARTQueueElement elem;;) {
        if (xQueueReceive(m_uart_queue, &elem, portMAX_DELAY) != pdTRUE) {
            throw std::runtime_error("xQueueReceive failed");
        }

        // FIXME: no duplex communication
        // FIXME: no continuous communication (use half-finished rx interrupts)
        for (;;) {
            HAL_UART_StateTypeDef status = HAL_UART_GetState(&m_uart_handle);
            if (status == HAL_UART_STATE_READY || status == HAL_UART_STATE_BUSY_RX)
                break;

            taskYIELD();
        }

        if (elem.is_rx()) {
            line_reader.add_chars(std::string_view(elem.data, elem.data_sz()));
        } else {
            std::copy(std::begin(elem.data), std::end(elem.data), m_uart_tx_buffer.data());
            while (HAL_UART_Transmit_DMA(&m_uart_handle, m_uart_tx_buffer.data(), elem.data_sz()) != HAL_OK) {
                taskYIELD();
            }
        }
    }
}

}
