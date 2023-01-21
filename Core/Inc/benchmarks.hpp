#pragma once

#include <p256.hpp>

namespace Tele {

void signature_benchmark(P256::PrivateKey const& sk);

void p256_test(P256::PrivateKey const& sk);

void test_parse_ip();

}
