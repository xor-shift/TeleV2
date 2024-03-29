#pragma once

#include <stdcompat.hpp>

#include <Stuff/Intro/Builder.hpp>
#include <Stuff/Intro/Introspectors/Array.hpp>
#include <Stuff/Intro/Introspectors/Span.hpp>
#include <Stuff/Maths/Hash/Sha2.hpp>
#include <Stuff/Serde/IntroSerializers.hpp>
#include <Stuff/Serde/Serializables/Variant.hpp>

#include <Stuff/Serde/Serializers/JSON.hpp>

namespace Tele {

struct EssentialsPacket {
    float speed;
    float bat_temp_readings[5];
    float voltage;
    float remaining_wh;
};

inline constexpr auto _libstf_adl_introspector(EssentialsPacket&&) {
    auto accessor = Stf::Intro::StructBuilder<EssentialsPacket> {} //
                      .add_simple<&EssentialsPacket::speed, "spd">()
                      .add_simple<&EssentialsPacket::bat_temp_readings, "temps">()
                      .add_simple<&EssentialsPacket::voltage, "v">()
                      .add_simple<&EssentialsPacket::remaining_wh, "wh">();
    return accessor;
}

struct DiagnosticPacket {
    uint32_t free_heap_space;
    uint32_t amt_allocs;
    uint32_t amt_frees;
    uint32_t performance[3];
};

inline constexpr auto _libstf_adl_introspector(DiagnosticPacket&&) {
    auto accessor = Stf::Intro::StructBuilder<DiagnosticPacket> {} //
                      .add_simple<&DiagnosticPacket::free_heap_space, "free">()
                      .add_simple<&DiagnosticPacket::amt_allocs, "alloc">()
                      .add_simple<&DiagnosticPacket::amt_frees, "free">()
                      .add_simple<&DiagnosticPacket::performance, "perf">();
    return accessor;
}

struct FullPacket {
    // BMS
    float battery_voltages[27];
    float battery_temps[5];
    float spent_mah;
    float spent_mwh;
    float current;
    float soc_percent;

    // Fuel cell (hydro only)
    float hydro_current;
    float hydro_ppm;
    float hydro_temp;

    // VCS
    float temperature_smps;
    float temperature_engine_driver;
    float vc_engine_driver[2];
    float vc_telemetry[2];
    float vc_smps[2];
    float vc_bms[2];

    // Engine
    float rpm;
    float speed;
    float vc_engine[2];

    // Local
    float longitude;
    float latitude;
    float gyro[3];

    // Diagnostic
    uint32_t queue_fill_amt;
    uint32_t tick_counter;
    uint32_t free_heap_space;
    uint32_t amt_allocs;
    uint32_t amt_frees;
    float cpu_usage;
};

inline constexpr auto _libstf_adl_introspector(FullPacket&&) {
    auto accessor = Stf::Intro::StructBuilder<FullPacket> {} // BMS
                      .add_simple<&FullPacket::battery_voltages, "v">()
                      .add_simple<&FullPacket::battery_temps, "temps">()
                      .add_simple<&FullPacket::spent_mah, "mah">()
                      .add_simple<&FullPacket::spent_mwh, "mwh">()
                      .add_simple<&FullPacket::current, "amps">()
                      .add_simple<&FullPacket::soc_percent, "soc">()

                      // Fuel cell (hydro only)
                      .add_simple<&FullPacket::hydro_current, "hc">()
                      .add_simple<&FullPacket::hydro_ppm, "hd">()
                      .add_simple<&FullPacket::hydro_temp, "ht">()

                      // VCS
                      .add_simple<&FullPacket::temperature_smps, "ts">()
                      .add_simple<&FullPacket::temperature_engine_driver, "ted">()
                      .add_simple<&FullPacket::vc_engine_driver, "vced">()
                      .add_simple<&FullPacket::vc_telemetry, "vct">()
                      .add_simple<&FullPacket::vc_smps, "vcs">()
                      .add_simple<&FullPacket::vc_bms, "vcb">()

                      // Engine
                      .add_simple<&FullPacket::rpm, "rpm">()
                      .add_simple<&FullPacket::speed, "spd">()
                      .add_simple<&FullPacket::vc_engine, "vce">()

                      // Local
                      .add_simple<&FullPacket::longitude, "long">()
                      .add_simple<&FullPacket::latitude, "lat">()
                      .add_simple<&FullPacket::gyro, "gyro">()

                      // Diagnostic
                      .add_simple<&FullPacket::queue_fill_amt, "q">()
                      .add_simple<&FullPacket::tick_counter, "tc">()
                      .add_simple<&FullPacket::free_heap_space, "heap">()
                      .add_simple<&FullPacket::amt_allocs, "alloc">()
                      .add_simple<&FullPacket::amt_frees, "free">()
                      .add_simple<&FullPacket::cpu_usage, "cu">();
    return accessor;
}

using packets_variant = std::variant<EssentialsPacket, DiagnosticPacket, FullPacket>;

struct Packet {
    uint32_t sequence_id;
    int32_t timestamp;
    uint32_t rng_state;
    packets_variant data;
};

inline constexpr auto _libstf_adl_introspector(Packet&&) {
    auto accessor = Stf::Intro::StructBuilder<Packet> {} //
                      .add_simple<&Packet::sequence_id, "seq">()
                      .add_simple<&Packet::timestamp, "ts">()
                      .add_simple<&Packet::rng_state, "rng">()
                      //.add_with_transform<&Packet::sig_r, "sig_r">([](auto s) { return std::string_view(s); })
                      //.add_with_transform<&Packet::sig_s, "sig_s">([](auto s) { return std::string_view(s); })
                      .add_simple<&Packet::data, "data">();
    return accessor;
}

struct PacketSequencer {
    Packet sequence(packets_variant inner);

    void reset(std::span<uint32_t, 4> rng_vector);

private:
    uint32_t m_last_seq_id = 0;
    uint32_t m_rng_state[4] = { 0xDEADBEEF, 0xCAFEBABE, 0xDEADC0DE, 0x8BADF00D };

    // CircularBuffer<Packet, 32> m_resend_window;

    static constexpr uint32_t xoshiro_next(uint32_t (&s)[4]) {
        const uint32_t result = std::rotl(s[0] + s[3], 7) + s[0];

        const uint32_t t = s[1] << 9;

        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];

        s[2] ^= t;

        s[3] = std::rotl(s[3], 11);

        return result;
    }
};

}
