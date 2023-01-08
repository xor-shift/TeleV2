#pragma once

#include <Stuff/Maths/Hash/Sha2.hpp>
#include <Stuff/Intro/Builder.hpp>
#include <Stuff/Intro/Introspectors/Span.hpp>
#include <Stuff/Intro/Introspectors/Array.hpp>
#include <Stuff/Serde/IntroSerializers.hpp>
#include <Stuff/Serde/Serializables/Variant.hpp>

#include <Stuff/Serde/Serializers/JSON.hpp>

#include <util.hpp>

extern P256::PrivateKey g_privkey;

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

using packets_variant = std::variant<EssentialsPacket>;

struct Packet {
    uint32_t sequence_id;
    uint32_t timestamp;
    uint32_t rng_state;
    char sig_r[65] = { 0 };
    char sig_s[65] = { 0 };
    packets_variant data;
};

inline constexpr auto _libstf_adl_introspector(Packet&&) {
    auto accessor = Stf::Intro::StructBuilder<Packet> {} //
                      .add_simple<&Packet::sequence_id, "seq">()
                      .add_simple<&Packet::timestamp, "ts">()
                      .add_simple<&Packet::rng_state, "rng">()
                      .add_with_transform<&Packet::sig_r, "sig_r">([](auto s) { return std::string_view(s); })
                      .add_with_transform<&Packet::sig_s, "sig_s">([](auto s) { return std::string_view(s); })
                      .add_simple<&Packet::data, "data">();
    return accessor;
}

struct PacketSequencer {
    Packet transmit(packets_variant inner) {
        Packet packet {
            .sequence_id = ++m_last_seq_id,
            .timestamp = 0,
            .rng_state = xoshiro_next(m_rng_state),
            .sig_r = { 0 },
            .sig_s = { 0 },
            .data = inner,
        };

        std::string buffer;
        PushBackStream stream { buffer };
        Stf::Serde::JSON::Serializer<PushBackStream<std::string>> serializer { stream };

        Stf::serialize(serializer, packet);

        Stf::Hash::SHA256State hash_state {};
        hash_state.update(std::string_view { buffer });
        std::array<uint32_t, 8> hash = hash_state.finish();

        P256::Signature signature = sign(g_privkey, hash.data());
        Tele::to_chars(std::span(signature.r), packet.sig_r, std::endian::big);
        packet.sig_r[64] = 0;
        Tele::to_chars(std::span(signature.s), packet.sig_s, std::endian::big);
        packet.sig_s[64] = 0;

        bool _;
        m_resend_window.push_back(packet, _);

        return packet;
    }

private:
    uint32_t m_last_seq_id = 0;
    uint32_t m_rng_state[4] = { 0xDEADBEEF, 0xCAFEBABE, 0xDEADC0DE, 0x8BADF00D };

    CircularBuffer<Packet, 32> m_resend_window;

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
