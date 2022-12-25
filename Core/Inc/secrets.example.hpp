#pragma once

#include <string_view>

namespace Tele::Config {

// input your secret/private key here as a hex-encoded 256-bit unsigned integer representation
static constexpr std::string_view sk_text = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

// leave these empty if you don't want a sanity check of pk generation

static constexpr std::string_view pkx_text = "";
static constexpr std::string_view pky_text = "";

}
