#include <Packets.hpp>

#include <chrono>

#include <Globals.hpp>

#include <Tele/CharConv.hpp>
#include <Tele/Stream.hpp>

namespace Tele {

/*std::string PacketSequencer::sequence(Tele::packets_variant inner) {
    int32_t timestamp
      = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    Packet packet {
        .sequence_id = m_last_seq_id++,
        .timestamp = timestamp,
        .rng_state = xoshiro_next(m_rng_state),
        .data = inner,
    };

    // TODO: save packet

    std::string serialization_buffer {};
    Tele::PushBackStream serialization_stream { serialization_buffer };
    Stf::Serde::JSON::Serializer<Tele::PushBackStream<std::string>> serializer { serialization_stream };
    Stf::serialize(serializer, packet);

    Stf::Hash::SHA256State hash_state {};
    hash_state.update(std::string_view { serialization_buffer });
    std::array<uint32_t, 8> hash = hash_state.finish();
    P256::Signature signature = sign(Tele::g_privkey, hash.data());

    serialization_buffer += "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
    serialization_buffer += "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";

    std::span<char> sig_span { end(serialization_buffer) - 128, end(serialization_buffer) };

    Tele::to_chars(std::span(signature.r), sig_span.subspan(0, 64), std::endian::little);
    Tele::to_chars(std::span(signature.s), sig_span.subspan(64, 64), std::endian::little);

    return { serialization_buffer, packet };
}*/

Packet PacketSequencer::sequence(Tele::packets_variant inner) {
    int32_t timestamp
      = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    Packet packet {
        .sequence_id = m_last_seq_id++,
        .timestamp = timestamp,
        .rng_state = xoshiro_next(m_rng_state),
        .data = inner,
    };

    // TODO: save packet in a resend window

    return packet;
}

void PacketSequencer::reset(std::span<uint32_t, 4> rng_vector) {
    m_last_seq_id = 0;
    copy(begin(rng_vector), end(rng_vector), m_rng_state);
}

}
