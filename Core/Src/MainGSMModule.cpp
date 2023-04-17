#include <MainGSMModule.hpp>

#include <Stuff/Util/Hacks/Try.hpp>
#include <Stuff/Util/Scope.hpp>
#include <Stuff/Util/Visitor.hpp>

#include <Tele/CharConv.hpp>
#include <Tele/Log.hpp>
#include <Tele/Stream.hpp>

#include <Globals.hpp>
#include <Packets.hpp>
#include <main.h>
#include <secrets.hpp>
#include <stdcompat.hpp>

namespace Tele::GSM {

struct CustomGyroTask : Tele::GyroTask {
    CustomGyroTask(MainModule& module, SPI_HandleTypeDef& spi, GPIO_TypeDef* cs_port, uint16_t cs_pin)
        : GyroTask(spi, cs_port, cs_pin)
        , m_module(module) { }

protected:
    virtual void raw_callback(Stf::Vector<uint16_t, 3> raw) { m_module.isr_gyro_new(raw); }

private:
    MainModule& m_module;
};

MainModule::MainModule(PacketForgerTask& packet_forger)
    : m_gyro_task(std::make_unique<CustomGyroTask>(std::ref(*this), hspi1, CS_I2C_SPI_GPIO_Port, CS_I2C_SPI_Pin))
    , m_packet_forger(packet_forger) { }

void MainModule::isr_gyro_notify() { m_gyro_task->isr_notify(); }

void MainModule::isr_gyro_new(Stf::Vector<uint16_t, 3> raw) { }

void MainModule::create(const char* name) {
    m_gyro_task->create("gryo");
    Task::create(name);
}

/*void MainModule::reset_state() {
    m_ready = false;
    m_functional = false;
    m_have_sim = false;
    m_call_ready = false;
    m_sms_ready = false;
    m_state_inconsistent = false;
}*/

bool MainModule::initialize_device() {
    /*std::ignore = m_coordinator->send_command_async(this, Command::AT {});
    std::ignore = m_coordinator->send_command_async(this, Command::Echo { false });
    std::ignore = m_coordinator->send_command_async(this, Command::SetErrorVerbosity { ErrorVerbosity::MEEString });
    std::ignore = m_coordinator->send_command_async(this, Command::SetBaud { BaudRate::BPS460k8 });
    std::ignore = m_coordinator->send_command_async(this, Command::SaveToNVRAM { });

    for (;;) {
        portYIELD();
    }*/

    /*
     * I am tired of dealing with the shenanigans of this device, regardless of
     * the card and the module booting at the same time, we are going to
     * restart the device.
     *
     * Delay to let it boot enough to accept AT commands.
     * I pray that this is deterministic...
     * TODO: add command timeouts (new forged reply to signal timeouts?)
     */
    vTaskDelay(2000);

    std::ignore = m_coordinator->send_command_async(this, Command::CFUN { CFUNType::Full, true });
    // reset_state();

    m_coordinator->reset_state();

    for (size_t i = 0; !m_coordinator->device_sms_ready() || !m_coordinator->device_call_ready(); i++) {
        vTaskDelay(100);
        if (i >= 150) // 15 secs
            return false;
    }

    m_coordinator->send_command_async(this, Command::SetBearerParameter { BearerProfile::Profile0, "Contype", "GPRS" });
    m_coordinator->send_command_async(this, Command::SetBearerParameter { BearerProfile::Profile0, "APN", "internet" });
    m_coordinator->send_command_async(this, Command::OpenBearer { BearerProfile::Profile0 });

    for (size_t i = 0;; i++) {
        vTaskDelay(100);
        if (i >= 150) // 15 secs
            return false;

        Command::QueryBearerParameters cmd { BearerProfile::Profile0 };
        Reply::BearerParameters reply;

        if (!extract_single_reply(reply, m_coordinator->send_command_async(this, cmd)))
            continue;

        if (reply.status != BearerStatus::Connected || reply.profile != BearerProfile::Profile0)
            continue;

        break;
    }

    m_coordinator->send_command_async(this, Command::AttachToGPRS {});

    for (size_t i = 0;; i++) {
        vTaskDelay(100);
        if (i >= 150) // 15 secs
            return false;

        Command::QueryGPRS cmd {};
        Reply::GPRSStatus reply;

        if (!extract_single_reply(reply, m_coordinator->send_command_async(this, cmd)))
            continue;

        if (!reply.attached)
            continue;

        break;
    }

    Reply::PositionAndTime pos_time_reply;
    if (!extract_single_reply(
          pos_time_reply, m_coordinator->send_command_async(this, Command::QueryPositionAndTime {})
        )) {
        return false;
    }
    Log::debug("setting time to: {}", pos_time_reply.unix_time);
    Tele::set_time(pos_time_reply.unix_time);

    return true;
}

bool MainModule::initialize_session(std::span<uint32_t, 4> out_rng_vector) {
    Reply::ResetChallenge challenge;
    std::tie(std::ignore, challenge, std::ignore) = TRY_OR_RET(
      false, extract_replies_from_range<Reply::HTTPResponse, Reply::ResetChallenge, Reply::Okay>(
               TRY_OR_RET(false, http_request(Tele::Config::Endpoints::reset_request, HTTPRequestType::GET))
             )
    );

    auto signature = P256::sign(Tele::g_privkey, data(challenge.challenge));
    std::array<char, 128> challenge_response_buffer;
    std::span<char> challenge_response_r { begin(challenge_response_buffer), begin(challenge_response_buffer) + 64 };
    std::span<char> challenge_response_s { begin(challenge_response_buffer) + 64, end(challenge_response_buffer) };
    std::ignore = Tele::to_chars<uint32_t>({ signature.r }, challenge_response_r, std::endian::little);
    std::ignore = Tele::to_chars<uint32_t>({ signature.s }, challenge_response_s, std::endian::little);

    Reply::ResetSuccess reset_success;
    std::tie(std::ignore, reset_success, std::ignore) = TRY_OR_RET(
      false, extract_replies_from_range<Reply::HTTPResponse, Reply::ResetSuccess, Reply::Okay>(TRY_OR_RET(
               false, http_request(
                        Tele::Config::Endpoints::reset_request, HTTPRequestType::POST, "text/plain",
                        { begin(challenge_response_buffer), end(challenge_response_buffer) }
                      )
             ))
    );
    std::copy(begin(reset_success.prng_vector), end(reset_success.prng_vector), begin(out_rng_vector));

    Log::debug(
      "received prng vector: {:08X} {:08X} {:08X} {:08X}", //
      reset_success.prng_vector[0],                        //
      reset_success.prng_vector[1],                        //
      reset_success.prng_vector[2],                        //
      reset_success.prng_vector[3]
    );

    return true;
}

int MainModule::packet_loop() {
    // clang-format off
    TRY_OR_RET(1, extract_replies_from_range<Reply::Okay>(m_coordinator->send_command_async(this, Command::HTTPInit {})));
    TRY_OR_RET(1, extract_replies_from_range<Reply::Okay>(m_coordinator->send_command_async(this, Command::HTTPSetBearer { BearerProfile::Profile0 })));
    TRY_OR_RET(1, extract_replies_from_range<Reply::Okay>(m_coordinator->send_command_async(this, Command::HTTPSetUA { "https://github.com/xor-shift/TeleV2" })));
    TRY_OR_RET(1, extract_replies_from_range<Reply::Okay>(m_coordinator->send_command_async(this, Command::HTTPSetURL { Tele::Config::Endpoints::packet_full })));
    TRY_OR_RET(1, extract_replies_from_range<Reply::Okay>(m_coordinator->send_command_async(this, Command::HTTPContentType { "text/plain" })));
    // clang-format on

    for (;;) {
        vTaskDelay(500);

        std::array<Tele::Packet, 10> arr;
        size_t pending_packet_count = m_packet_forger.get_pending_packets(arr);
        std::span<Tele::Packet> pending_packets { begin(arr), pending_packet_count };

        std::string serialization_buffer;
        Tele::PushBackStream serialization_stream { serialization_buffer };
        Stf::Serde::JSON::Serializer<Tele::PushBackStream<std::string>> serializer { serialization_stream };
        Stf::serialize(serializer, pending_packets);

        Stf::Hash::SHA256State hash_state {};
        hash_state.update(std::string_view { serialization_buffer });
        std::array<uint32_t, 8> hash = hash_state.finish();
        P256::Signature signature = sign(Tele::g_privkey, hash.data());

        serialization_buffer += "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
        serialization_buffer += "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";

        std::span<char> sig_span { end(serialization_buffer) - 128, end(serialization_buffer) };

        Tele::to_chars(std::span(signature.r), sig_span.subspan(0, 64), std::endian::little);
        Tele::to_chars(std::span(signature.s), sig_span.subspan(64, 64), std::endian::little);

        // clang-format off
        TRY_OR_RET(2, extract_replies_from_range<Reply::HTTPReadyForData, Reply::Okay>(m_coordinator->send_command_async(this, Command::HTTPData { .data = { serialization_buffer } })));
        TRY_OR_RET(2, extract_replies_from_range<Reply::Okay>(m_coordinator->send_command_async(this, Command::HTTPMakeRequest { HTTPRequestType::POST })));
        TRY_OR_RET(2, wait_for_http());
        TRY_OR_RET(2, extract_replies_from_range<Reply::Okay>(m_coordinator->send_command_async(this, Command::HTTPRead { })));
        // clang-format on
    }
    m_coordinator->send_command_async(this, Command::HTTPTerm {});

    return 0;
}

int MainModule::main() {
    if (!initialize_device()) {
        return 1;
    }

    std::array<uint32_t, 4> initial_vector;

    if (!initialize_session(initial_vector)) {
        return 2;
    }

    m_packet_forger.reset_sequencer(initial_vector);

    auto reinitialize_device = [&] {
        for (size_t i = 0;; i++) {
            if (initialize_device())
                break;
            Log::warn("device initialization failed, retry count: {}", i);
        }
    };

    for (size_t i = 0, j = 0;; i++, j++) {
        int res = packet_loop();
        Log::warn("packet loop exited with status {}, retry count: {}", res, i);

        if (j >= 5) {
            j = 0;
            Log::warn("{} failures since last device restart, restarting it", j);
            reinitialize_device();
        } else if (m_coordinator->device_inconsistent_state()) {
            Log::warn("inconsistent state, reinitializing device");
            reinitialize_device();
        }

        vTaskDelay(500);
    }

    return -1;
}

std::optional<Reply::HTTPResponseReady> MainModule::wait_for_http(BaseType_t timeout_decisecs) {
    std::optional<Reply::HTTPResponseReady> ret = std::nullopt;

    for (size_t i = 0; i < timeout_decisecs; i++) {
        if (!m_last_http_response) {
            vTaskDelay(100);
            continue;
        }

        std::swap(ret, m_last_http_response);
        break;
    }

    return ret;
}

std::optional<std::vector<Reply::reply_type>> MainModule::http_request(
  std::string_view url, HTTPRequestType method, std::string_view content_type, std::string_view content
) {
    m_coordinator->send_command_async(this, Command::HTTPInit {});
    m_coordinator->send_command_async(this, Command::HTTPSetBearer { BearerProfile::Profile0 });
    m_coordinator->send_command_async(this, Command::HTTPSetUA { "https://github.com/xor-shift/TeleV2" });
    m_coordinator->send_command_async(this, Command::HTTPSetURL { url });

    if (content_type != "") {
        m_coordinator->send_command_async(this, Command::HTTPContentType { content_type });
        m_coordinator->send_command_async(this, Command::HTTPData { .data = { begin(content), end(content) } });
    }

    m_coordinator->send_command_async(this, Command::HTTPMakeRequest { method });

    TRYX(wait_for_http());

    auto ret = m_coordinator->send_command_async(this, Command::HTTPRead {});

    m_coordinator->send_command_async(this, Command::HTTPTerm {});

    return ret;
}

[[noreturn]] void MainModule::operator()() {
    if (m_coordinator == nullptr)
        Error_Handler();

    static const TickType_t s_fail_wait = 2500;

    for (size_t retries = 0;; retries++) {
        // reset_state();
        int res = main();
        Log::warn("the main procedure failed with code {}", res);
        Log::warn("waiting {} ticks before restarting for retry no #{}", s_fail_wait, retries + 1);
        vTaskDelay(s_fail_wait);
    }
}

void MainModule::incoming_reply(GSM::Coordinator&, Reply::reply_type const& reply) {
    Stf::MultiVisitor visitor {
        [this](Reply::HTTPResponseReady const& reply) {
            // FIXME: race
            // this function is called from another thread, we could be reading m_last_http_response while it is being
            // written into
            // make ABSOLUTELY SURE that there are no more than one requests being made at a time, if that is even
            // possible
            // TODO: add an unsolicited reply queue to all modules and signal unsolicited replies using that.
            // (and never call any function of the modules from within the coordinator's thread)
            m_last_http_response = reply;
        },
        [](auto) {},
    };

    std::visit(visitor, reply);
}

}
