#include "stdcompat.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <random>

#include <Stuff/Maths/Hash/Sha2.hpp>

#include "main.h"

#include "p256.hpp"
#include "secrets.hpp"
#include "util.hpp"

namespace Tele {

template<typename Func> static double benchmark_func(Func&& fn, size_t runs = 32, size_t groups = 8) {
    double multiplier = 1. / (groups * runs);
    double ret = 0.;

    for (auto group = 0uz; group < groups; group++) {
        auto tp_0 = HAL_GetTick();
        for (auto run = 0uz; run < runs; run++) {
            auto res = std::invoke(fn);
            do_not_optimize(res);
        }
        auto tp_1 = HAL_GetTick();
        ret += (tp_1 - tp_0) * multiplier;
    }

    return ret;
}

[[gnu::section(".ccmram")]] static std::array<uint32_t, 8192> s_large_buffer;

[[gnu::optimize("O0")]] void signature_benchmark(P256::PrivateKey const& sk) {
    std::random_device rd;

    std::generate(begin(s_large_buffer), end(s_large_buffer), [&rd] { return rd(); });

    uint32_t digest_u32[8];
    std::generate(digest_u32, digest_u32 + 8, [&rd] { return rd(); });
    uint8_t* digest_u8 = reinterpret_cast<uint8_t*>(digest_u32);

    auto bench_fn_sign_prehashed_u8 = [&sk, &digest_u8] { return P256::sign(sk, digest_u8); };

    auto bench_fn_sign_prehashed_u32 = [&sk, &digest_u32] { return P256::sign(sk, digest_u32); };

    auto bench_fn_sign_hash = [&sk] {
        Stf::Hash::SHA256State hash_state {};
        hash_state.update("Hello, world!");
        std::array<uint32_t, 8> digest = hash_state.finish();
        return P256::sign(sk, digest.data());
    };

    auto bench_fn_sha_generic = [](auto&& state) {
        state.update('\0');
        return state.finish();
    };

    auto bench_fn_sha_256 = [&] { return bench_fn_sha_generic(Stf::Hash::SHA256State {}); };

    auto bench_fn_sha_512 = [&] { return bench_fn_sha_generic(Stf::Hash::SHA512State {}); };

    auto bench_fn_sha_256_large = [] {
        Stf::Hash::SHA256State hash_state {};
        hash_state.update(std::span(reinterpret_cast<uint8_t*>(s_large_buffer.data()), s_large_buffer.size() * 4));
        std::array<uint32_t, 8> digest = hash_state.finish();
        return digest;
    };

    std::array<double, 6> results { {
      benchmark_func(bench_fn_sign_prehashed_u8, 8),
      benchmark_func(bench_fn_sign_prehashed_u32, 8),
      benchmark_func(bench_fn_sign_hash, 8),
      benchmark_func(bench_fn_sha_256, 8),
      benchmark_func(bench_fn_sha_512, 8),
      benchmark_func(bench_fn_sha_256_large, 4, 4),
    } };

    do_not_optimize(results);
}

void p256_test(P256::PrivateKey const& sk) {
    Stf::Hash::SHA256State hash_state {};

    std::array<char, 64> buf_sig_r;
    std::array<char, 64> buf_sig_s;

    for (auto [test_string, valid_signatures] : Tele::Config::sig_tests) {
        hash_state.reset();

        hash_state.update(test_string);
        std::array<uint32_t, 8> digest = hash_state.finish();

        P256::Signature signature = P256::sign(sk, digest.data());
        bool self_verification = P256::verify(sk.pk, digest.data(), signature);
        auto sig_r = Tele::to_chars(std::span(signature.r), buf_sig_r, std::endian::little);
        auto sig_s = Tele::to_chars(std::span(signature.s), buf_sig_s, std::endian::little);

        do_not_optimize(self_verification);
        do_not_optimize(sig_r);
        do_not_optimize(sig_s);

        // breakpoint here
        std::ignore = 0;
    }
}

}
