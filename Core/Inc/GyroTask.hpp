#pragma once

#include "semphr.h"
#include <LIS3DSH.hpp>
#include <StaticTask.hpp>

namespace Tele {

template<typename Callback>
struct GyroTask : StaticTask<2048> {
    constexpr GyroTask(Callback&& callback = {})
      :m_callback(std::move(callback)) {
        m_sema = xSemaphoreCreateBinary();
    }

    ~GyroTask() noexcept override = default;

    [[gnu::cold]] static void task(void* arg) {
        auto& self = *reinterpret_cast<GyroTask*>(arg);
        self();
    }

    void notify_isr() {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(handle(), &xHigherPriorityTaskWoken);
        //xSemaphoreGiveFromISR(m_sema, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

protected:
    [[noreturn]] void operator()() override {
        m_state.reboot();

        const uint8_t whoami_res = m_state.whoami();
        if (whoami_res != 0x3F) {
            Error_Handler();
        }

        LIS::ControlReg3 config_reg_3 {
            .int1_enable = true,
            .int_latch = true,
            .dr_enable = true,
        };

        LIS::ControlReg4 config_reg_4 {
            .x_enable = true,
            .y_enable = true,
            .z_enable = true,
            .data_rate = LIS::DataRate::Hz50,
        };

        LIS::ControlReg5 config_reg_5 {
            .full_scale = LIS::FullScale::G16,
            .aa_bandwidth = LIS::AABandwidth::Hz50,
        };

        m_state.configure(config_reg_3);
        m_state.configure(config_reg_4);
        m_state.configure(config_reg_5);
        m_state.read_config(config_reg_3);
        m_state.read_config(config_reg_4);
        m_state.read_config(config_reg_5);

        for (;;) {
            std::ignore = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            //if (xSemaphoreTake( m_sema, portMAX_DELAY ) == pdFALSE)
            //    continue;

            //vTaskDelay(333);

            m_callback(m_state.read_scaled(LIS::FullScale::G16));

            std::ignore = 0;
        }
    }

private:
    Callback m_callback;

    LIS::State m_state {
        .spi = hspi1,
        .cs_port = CS_I2C_SPI_GPIO_Port,
        .cs_pin = CS_I2C_SPI_Pin,
    };

    SemaphoreHandle_t m_sema = NULL;
};

}