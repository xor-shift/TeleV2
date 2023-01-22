#pragma once

#include <stdexcept>

#include <cmsis_os.h>
#include <semphr.h>

#include <Tele/LIS3DSH.hpp>
#include <Tele/StaticTask.hpp>

namespace Tele {

struct GyroTask : Tele::StaticTask<1024> {
    GyroTask(SPI_HandleTypeDef& spi, GPIO_TypeDef* cs_port, uint16_t cs_pin);

    ~GyroTask() noexcept override = default;

    [[gnu::cold]] static void task(void* arg) {
        auto& self = *reinterpret_cast<GyroTask*>(arg);
        self();
    }

    /// @remarks
    /// This function is signal-safe
    void isr_notify();

protected:
    virtual void raw_callback(Stf::Vector<uint16_t, 3>) {}

    [[noreturn]] void operator()() override;

private:
    LIS::State m_state;

    //SemaphoreHandle_t m_sema = NULL;
};

}
