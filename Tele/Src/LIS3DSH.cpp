#include <Tele/LIS3DSH.hpp>

#include <span>

#include <cmsis_os.h>

namespace LIS {

void State::enable_chip(bool enable) const {
    HAL_GPIO_WritePin(cs_port, cs_pin, enable ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void State::write_register(Register reg, std::span<uint8_t> buf) const {
    uint8_t address = mk_write_addr(reg);

    enable_chip(true);
    HAL_SPI_Transmit(&spi, &address, 1, 10);
    HAL_SPI_Transmit(&spi, data(buf), size(buf), 10);
    enable_chip(false);
}

void State::read_register(Register reg, std::span<uint8_t> buf) const {
    uint8_t address = mk_read_addr(reg);

    enable_chip(true);
    HAL_StatusTypeDef status_transmit = HAL_SPI_Transmit(&spi, &address, 1, 10);
    HAL_StatusTypeDef status_receive = HAL_SPI_Receive(&spi, data(buf), size(buf), 10);
    enable_chip(false);
}

void State::reboot() const {
    write_register(Register::CtrlReg6, 0x80);

    do {
        vTaskDelay(8);
    } while ((read_register(Register::CtrlReg6) & 0x80) != 0x00);
}

uint8_t State::whoami() const { return read_register<1>(Register::WhoAmI)[0]; }

// clang-format off

void State::read_config(ControlReg3& reg) const { reg = *reinterpret_cast<ControlReg3*>(read_register<1>(Register::CtrlReg3).data()); }
void State::read_config(ControlReg4& reg) const { reg = *reinterpret_cast<ControlReg4*>(read_register<1>(Register::CtrlReg4).data()); }
void State::read_config(ControlReg5& reg) const { reg = *reinterpret_cast<ControlReg5*>(read_register<1>(Register::CtrlReg5).data()); }
void State::read_config(ControlReg6& reg) const { reg = *reinterpret_cast<ControlReg6*>(read_register<1>(Register::CtrlReg6).data()); }

void State::configure(ControlReg3 reg) const { return write_register(Register::CtrlReg3, *reinterpret_cast<uint8_t*>(&reg)); }
void State::configure(ControlReg4 reg) const { return write_register(Register::CtrlReg4, *reinterpret_cast<uint8_t*>(&reg)); }
void State::configure(ControlReg5 reg) const { return write_register(Register::CtrlReg5, *reinterpret_cast<uint8_t*>(&reg)); }
void State::configure(ControlReg6 reg) const { return write_register(Register::CtrlReg6, *reinterpret_cast<uint8_t*>(&reg)); }

// clang-format on

Stf::Vector<uint16_t, 3> State::read_raw() const {
    /*uint8_t buffer[6];
    read_register(Register::OutXL, buffer);

    uint16_t x = (static_cast<uint16_t>(buffer[1]) << 8) | buffer[0];
    uint16_t y = (static_cast<uint16_t>(buffer[3]) << 8) | buffer[2];
    uint16_t z = (static_cast<uint16_t>(buffer[5]) << 8) | buffer[3];

    return Stf::vector(x, y, z);*/

    uint8_t xl = read_register(Register::OutXL);
    uint8_t xh = read_register(Register::OutXH);
    uint8_t yl = read_register(Register::OutYL);
    uint8_t yh = read_register(Register::OutYH);
    uint8_t zl = read_register(Register::OutZL);
    uint8_t zh = read_register(Register::OutZH);

    return Stf::vector(
      (static_cast<uint16_t>(xh) << 8) | xl, //
      (static_cast<uint16_t>(yh) << 8) | yl, //
      (static_cast<uint16_t>(zh) << 8) | zl
    );
}

}
