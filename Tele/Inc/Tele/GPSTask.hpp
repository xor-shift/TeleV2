#pragma once

#include <Tele/DataCollector.hpp>
#include <Tele/StaticTask.hpp>
#include <Tele/STUtilities.hpp>
#include <Tele/UARTTasks.hpp>

#include <stm32f4xx_hal.h>
#include <stm32f4xx_hal_uart.h>

namespace Tele {

struct GPSTask : Task<1024, true> {
    GPSTask(DataCollectorTask& data_collector, UART_HandleTypeDef& handle);

    void isr_rx_event(UART_HandleTypeDef* huart, uint16_t offset) { m_uart_task.isr_rx_event(huart, offset); }

    void begin_rx() { m_uart_task.begin_rx(); }

    void create(const char* name) override {
        Task::create(name);

        m_uart_task.create(name);
    }

protected:
    [[noreturn]] void operator()() override;

private:
    DataCollectorTask& m_data_collector;

    UART_HandleTypeDef& m_handle;

    TxDelimitedRxTask m_uart_task;
};

}
