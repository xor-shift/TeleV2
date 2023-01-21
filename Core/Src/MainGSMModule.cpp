#include <MainGSMModule.hpp>

#include <Stuff/Util/Hacks/Try.hpp>
#include <Stuff/Util/Visitor.hpp>

#include <Tele/CharConv.hpp>
#include <Tele/Log.hpp>

#include <Globals.hpp>
#include <secrets.hpp>
#include <stdcompat.hpp>
#include <Packets.hpp>

namespace GSM {

bool MainModule::initialize(std::span<uint32_t, 4> out_rng_vector) {
    bool was_open = false;
    for (size_t i = 0; !m_ready; i++) {
        vTaskDelay(100);
        if (i >= 50) {
            was_open = true;
            break;
        }
    }

    Log::info("the module was{} open", was_open ? "" : "n't");

    // std::ignore = m_coordinator->send_command_async(this, Command::AT {});
    std::ignore = m_coordinator->send_command_async(this, Command::SetErrorVerbosity { ErrorVerbosity::MEEString });
    if (was_open) {
        std::ignore = m_coordinator->send_command_async(this, Command::CFUN { CFUNType::Full, true });
    }

    for (size_t i = 0; !m_sms_ready || !m_call_ready; i++) {
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
      reset_success.prng_vector[0],                                    //
      reset_success.prng_vector[1],                                    //
      reset_success.prng_vector[2],                                    //
      reset_success.prng_vector[3]
    );

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

int MainModule::main() {
    Tele::PacketSequencer sequencer {};
    std::array<uint32_t, 4> initial_vector;

    if (!initialize(initial_vector)) {
        return 1;
    }

    sequencer.reset(initial_vector);

    for (;;) {
        Tele::FullPacket packet {
            .speed = 1,
            .bat_temp_readings = { 2, 3, 4, 5, 6 },
            .voltage = 7,
            .remaining_wh = 8,

            .longitude = 9,
            .latitude = 10,

            .free_heap_space = xPortGetFreeHeapSize(),
            .amt_allocs = 0,
            .amt_frees = 0,
            .performance = { 0, 0, 0 },
        };

        std::string data_to_send = sequencer.sequence(std::move(packet));

        auto res = http_request(
          Tele::Config::Endpoints::packet_essentials, HTTPRequestType::POST, "text/plain",
          std::string_view { data_to_send }
        );

        // vTaskDelay(5000);
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

void MainModule::reset_state() {
    m_ready = false;
    m_functional = false;
    m_have_sim = false;
    m_call_ready = false;
    m_sms_ready = false;
    m_bearer_open = false;
    m_gprs_open = false;
}

[[noreturn]] void MainModule::operator()() {
    if (m_coordinator == nullptr)
        Error_Handler();

    static const TickType_t s_fail_wait = 2500;

    for (size_t retries = 0;; retries++) {
        reset_state();
        int res = main();
        Log::warn("the main procedure failed with code {}", res);
        Log::warn("waiting {} ticks before restarting for retry no #{}", s_fail_wait, retries + 1);
        vTaskDelay(s_fail_wait);
    }
}

void MainModule::incoming_reply(GSM::Coordinator&, Reply::reply_type const& reply) {
    Stf::MultiVisitor visitor {
        [this](Reply::Ready const&) { m_ready = true; },
        [this](Reply::CFUN const&) { m_functional = true; },
        [this](Reply::CPIN const&) { m_have_sim = true; },
        [this](Reply::SMSReady const&) { m_sms_ready = true; },
        [this](Reply::CallReady const&) { m_call_ready = true; },
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
