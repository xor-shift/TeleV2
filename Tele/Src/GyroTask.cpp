#include <Tele/GyroTask.hpp>

namespace Tele {

GyroTask::GyroTask(SPI_HandleTypeDef& spi, GPIO_TypeDef* cs_port, uint16_t cs_pin)
    : m_state({ spi, cs_port, cs_pin }) {
    //vSemaphoreCreateBinary(m_sema);
}

void GyroTask::isr_notify() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(handle(), &xHigherPriorityTaskWoken);
    // xSemaphoreGiveFromISR(m_sema, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

[[noreturn]] void GyroTask::operator()() {
    m_state.reboot();

    const uint8_t whoami_res = m_state.whoami();
    if (whoami_res != 0x3F) {
        throw std::runtime_error("bad LIS whoami value");
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
        .data_rate = LIS::DataRate::Hz3_125,
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

        // if (xSemaphoreTake( m_sema, portMAX_DELAY ) == pdFALSE)
        //     continue;

        // vTaskDelay(333);

        //m_callback(m_state.read_scaled(LIS::FullScale::G16));
        raw_callback(m_state.read_raw());

        std::ignore = 0;
    }
}

}
