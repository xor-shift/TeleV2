#pragma once

#include <StaticTask.hpp>
#include <LIS3DSH.hpp>

namespace Tele {

template<typename Callback>
struct GyroTask {
    constexpr GyroTask(Callback&& callback = {})
      :m_callback(std::move(callback)) {}

    ~GyroTask() noexcept = default;

    void create(const char* name) {
        const size_t stack_size = sizeof(m_stack) / sizeof(m_stack[0]);
        const auto prio = tskIDLE_PRIORITY + 2;

        m_handle = xTaskCreateStatic(GyroTask::task, name, stack_size, this, prio, m_stack, &m_static_task);
    }

    [[gnu::cold]] static void task(void* arg) {
        auto& self = *reinterpret_cast<GyroTask*>(arg);
        self();
    }

    constexpr TaskHandle_t handle() const { return m_handle; }

protected:
    [[noreturn]] void operator()() {
        m_state.reboot();

        const uint8_t whoami_res = m_state.whoami();
        if (whoami_res != 0x3F) {
            Error_Handler();
        }

        LIS::ControlReg3 config_reg_3 {
            .int1_enable = true,
            .dr_enable = true,
        };

        LIS::ControlReg4 config_reg_4 {
            .x_enable = true,
            .y_enable = true,
            .z_enable = true,
            .data_rate = LIS::DataRate::Hz3_125,
        };

        LIS::ControlReg5 config_reg_5 {
            .full_scale = LIS::FullScale::G4,
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

            Stf::Vector<uint16_t, 3> raw_reading = m_state.read_raw();

            m_callback(raw_reading);

            std::ignore = raw_reading;
        }
    }

private:
    Callback m_callback;

    LIS::State m_state {
        .spi = hspi1,
        .cs_port = CS_I2C_SPI_GPIO_Port,
        .cs_pin = CS_I2C_SPI_Pin,
    };

    TaskHandle_t m_handle;

    StackType_t m_stack[configMINIMAL_STACK_SIZE];
    StaticTask_t m_static_task;
};

}