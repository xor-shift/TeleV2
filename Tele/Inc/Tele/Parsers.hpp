#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace Tele {

bool parse_ip(std::string_view ip, bool& ipv4, std::span<uint8_t> out);

}
