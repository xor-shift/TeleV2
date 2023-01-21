#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

#include <Stuff/Maths/Scalar.hpp>
#include <Stuff/Serde/Serde.hpp>

namespace Tele {

template<std::integral Repr, int Power> struct Fixed {
    Repr data;

    constexpr Fixed() = default;

    template<std::floating_point T> constexpr Fixed(T v) { *this = v; }

    template<std::integral T> constexpr Fixed(T v) { *this = v; }

    template<std::floating_point T> constexpr Fixed& operator=(T v) {
        constexpr T multiplier = Stf::pow(static_cast<T>(2), -Power);
        v *= multiplier;
        if (v >= std::numeric_limits<Repr>::max()) {
            data = std::numeric_limits<Repr>::max();
        } else if (v <= std::numeric_limits<Repr>::min()) {
            data = std::numeric_limits<Repr>::min();
        } else {
            data = v;
        }

        return *this;
    }

    template<std::integral T> constexpr Fixed& operator=(T v) {
        if (Power >= 0) {
            v >>= Power;
        } else {
            v <<= -Power;
        }

        data = v;

        return *this;
    }

    template<std::floating_point T> constexpr operator T() const {
        constexpr T multiplier = Stf::pow(static_cast<T>(2), Power);
        return data * multiplier;
    }
};

template<typename Serializer, std::signed_integral Repr, Repr Power>
constexpr tl::expected<void, std::string_view> _libstf_adl_serializer(Serializer& serializer, Fixed<Repr, Power> v) {
    return Stf::serialize(serializer, v.data);
}

template<std::unsigned_integral Repr, float Min, float Max>
    requires(Max > Min)
struct RangeFloat {
    static constexpr float value_range = Max - Min;
    static constexpr float value_step = value_range / (1 << std::numeric_limits<Repr>::digits);

    Repr data;

    constexpr RangeFloat() = default;

    template<std::floating_point T> constexpr RangeFloat(T v) { *this = v; }

    template<std::floating_point T> constexpr RangeFloat& operator=(T v) {
        v = Stf::clamp(v, Min, Max);
        const float offset = v - Min;
        const float step_offset = offset / value_step;
        data = std::floor(step_offset);

        return *this;
    }

    template<std::floating_point T> constexpr operator T() const { return Min + data * value_step; }
};

template<typename Serializer, std::unsigned_integral Repr, float Min, float Max>
constexpr tl::expected<void, std::string_view>
_libstf_adl_serializer(Serializer& serializer, RangeFloat<Repr, Min, Max> v) {
    return Stf::serialize(serializer, v.data);
}

}