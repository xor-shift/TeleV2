#pragma once

#include <cstdint>
#include <span>

#include <Stuff/Maths/BLAS/Vector.hpp>

#include "main.h"

extern "C" SPI_HandleTypeDef hspi1;

namespace LIS {

static SPI_HandleTypeDef& spi = hspi1;

enum class Register : uint8_t {
    Info1 = 0x0D,  // ro
    Info2 = 0x0E,  // ro
    WhoAmI = 0x0F, // ro

    CtrlReg3 = 0x23, // rw
    CtrlReg4 = 0x20, // rw
    CtrlReg5 = 0x24, // rw
    CtrlReg6 = 0x25, // rw

    Status = 0x27, // ro

    OutTemperature = 0x0C, // ro

    OffsetX = 0x10, // rw
    OffsetY = 0x11, // rw
    OffsetZ = 0x12, // rw

    CShiftX = 0x13, // rw
    CShiftY = 0x14, // rw
    CShiftZ = 0x15, // rw

    LCountL = 0x16, // rw
    LCountR = 0x17, // rw

    // TODO: better naming
    Stat = 0x18, // ro

    VFC1 = 0x1B, // rw
    VFC2 = 0x1C, // rw
    VFC3 = 0x1D, // rw
    VFC4 = 0x1E, // rw

    Threshold3 = 0x1F, // rw

    OutXL = 0x28, // ro
    OutXH = 0x29, // ro
    OutYL = 0x2A, // ro
    OutYH = 0x2B, // ro
    OutZL = 0x2C, // ro
    OutZH = 0x2D, // ro
};

enum class DataRate : uint8_t {
    PowerDown = 0x00,
    // 3.125Hz
    Hz3_125 = 0x01,
    // 6.25Hz
    Hz6_25 = 0x02,
    // 12.5Hz
    Hz12_5 = 0x03,
    // 25Hz
    Hz25 = 0x04,
    // 50Hz
    Hz50 = 0x05,
    // 100Hz
    Hz100 = 0x06,
    // 400Hz
    Hz400 = 0x07,
    // 800Hz
    Hz800 = 0x08,
    // 1.6kHz
    Hz1K6 = 0x09,
};

enum class FullScale : uint8_t {
    // ±2G
    G2 = 0x00,
    // ±4G
    G4 = 0x01,
    // ±6G
    G6 = 0x02,
    // ±8G
    G8 = 0x03,
    // ±16G
    G16 = 0x04,
};

enum class AABandwidth : uint8_t {
    Hz800 = 0x00,
    Hz400 = 0x01,
    Hz200 = 0x02,
    Hz50 = 0x03,
};

enum class SelfTest : uint8_t {
    Normal = 0x00,
    PositiveSign = 0x01,
    NegativeSign = 0x02,
    NotAllowed = 0x03,
};

struct ControlReg3 {
    // Soft reset bit
    //  false: no soft reset
    //  true: soft reset
    bool soft_reset : 1 = false;

    bool reserved : 1 = false;

    // Vector filter enable/disable
    bool vfilt_enable : 1 = false;

    // Interrupt 1 enable/disable
    //  false: INT1/DRDY signal disabled
    //  true: INT1/DRDY signal enabled
    bool int1_enable : 1 = false;

    // Interrupt 2 enable/disable
    //  false: INT2 signal disabled
    //  true: INT2 signal enabled
    bool int2_enable : 1 = false;

    // Interrupt signal latching
    //  false: interrupt signals latched
    //  true: interrupt signal pulsed
    bool int_latch : 1 = false;

    // Interrupt signal polarity
    //  false: interrupt signals active LOW
    //  true: interrupt signals active HIGH
    bool int_polarity : 1 = false;

    // DRDY signal enable to INT1
    //  false: data ready signal not connected
    //  true: data ready signal connected to INT1
    bool dr_enable : 1 = false;
};

struct ControlReg4 {
    bool x_enable : 1 = true;
    bool y_enable : 1 = true;
    bool z_enable : 1 = true;

    // Block data update
    //  false: continuous update
    //  true: output registers not update until MSB and LSB reading [sic] (?)
    bool block_data_update : 1 = false;

    // Output data rate and power mode selection.
    DataRate data_rate : 4 = DataRate::PowerDown;
};

struct ControlReg5 {
    // SPI serial interface mode selection
    //  false: 4-wire
    //  true: 3-wire
    bool spi_selection : 1 = false;

    SelfTest self_test : 2 = SelfTest::Normal;
    FullScale full_scale : 3 = FullScale::G2;
    AABandwidth aa_bandwidth : 2 = AABandwidth::Hz800;
};

struct ControlReg6 {
    // BOOT interrupt on Int2
    bool p2_boot : 1 = false;

    // IFO overrun interrupt on Int1
    bool p1_overrun : 1 = false;

    // FIFO Watermark interrupt on Int1
    bool p1_wtm : 1 = false;

    // Enable FIFO Empty indication on Int1
    bool p1_empty : 1 = false;

    // Register address automatically incremented during a multiple byte access with a serial interface (I2C or SPI)
    bool add_inc : 1 = false;

    // Enable FIFO Watermark level use
    bool fifo_wtm : 1 = false;

    bool fifo_enable : 1 = false;

    // Force reboot, cleared as soon as the reboot is finished
    // Active high
    bool reboot : 1 = false;
};

static_assert(sizeof(ControlReg3) == sizeof(uint8_t));
static_assert(sizeof(ControlReg4) == sizeof(uint8_t));
static_assert(sizeof(ControlReg5) == sizeof(uint8_t));
static_assert(sizeof(ControlReg6) == sizeof(uint8_t));

constexpr uint8_t mk_read_addr(Register reg) { return static_cast<uint8_t>(reg) | 0x80; }

constexpr uint8_t mk_write_addr(Register reg) { return static_cast<uint8_t>(reg); }

struct State {
    SPI_HandleTypeDef& spi;
    GPIO_TypeDef* cs_port;
    uint16_t cs_pin;

    void reboot() const;

    uint8_t whoami() const;

    void read_config(ControlReg3& reg) const;
    void read_config(ControlReg4& reg) const;
    void read_config(ControlReg5& reg) const;
    void read_config(ControlReg6& reg) const;

    void configure(ControlReg3 reg) const;
    void configure(ControlReg4 reg) const;
    void configure(ControlReg5 reg) const;
    void configure(ControlReg6 reg) const;

    Stf::Vector<uint16_t, 3> read_raw() const;

    Stf::Vector<float, 3> read_scaled(FullScale scale) const {
        auto raw = Stf::vector<float>(read_raw());

        float scale_mult;
        switch (scale) {
        case FullScale::G2: scale_mult = 2; break;
        case FullScale::G4: scale_mult = 4; break;
        case FullScale::G6: scale_mult = 6; break;
        case FullScale::G8: scale_mult = 8; break;
        case FullScale::G16: scale_mult = 16; break;
        default: scale_mult = 1;
        }

        return Stf::vector((raw / 65535.f - 1.f) * scale_mult);
        return Stf::vector(raw / 32767.5f - 1.f);
    }

private:
    void enable_chip(bool enable) const;

    void write_register(Register reg, std::span<uint8_t> buf) const;

    void read_register(Register reg, std::span<uint8_t> buf) const;

    void write_register(Register reg, uint8_t v) const { return write_register(reg, std::span(&v, 1)); }

    template<size_t Sz> std::array<uint8_t, Sz> read_register(Register reg) const {
        std::array<uint8_t, Sz> ret;
        read_register(reg, ret);
        return ret;
    }

    inline uint8_t read_register(Register reg) const { return read_register<1>(reg)[0]; }
};

}
