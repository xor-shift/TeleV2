#pragma once

#include <array>
#include <string_view>

namespace Tele::Config {

struct SigTest {
    std::string_view test_string;
    std::array<std::pair<std::string_view, std::string_view>, 4> valid_signatures;
};

// input your secret/private key here as a hex-encoded 256-bit unsigned integer representation
static constexpr std::string_view sk_text = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

// leave these empty if you don't want a sanity check of pk generation

static constexpr std::string_view pkx_text = "";
static constexpr std::string_view pky_text = "";

namespace Endpoints {

static constexpr std::string_view reset_request = "http://example.com/session_reset_challenge";
static constexpr std::string_view packet_diagnostic = "http://example.com/packet/diagnostic";
static constexpr std::string_view packet_essentials = "http://example.com/packet/essentials";
static constexpr std::string_view packet_full = "http://example.com/packet/full";

}

static constexpr std::array<SigTest, 2> sig_tests { {
  { "", {} },
  { "Hello, world!", {} },
} };

}
