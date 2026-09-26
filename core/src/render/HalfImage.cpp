#include "zaro/core/render/HalfImage.h"

#include <bit>
#include <cstdint>

namespace zaro::render {

std::uint16_t halfFromFloat(float value) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign = static_cast<std::uint16_t>((bits >> 16U) & 0x8000U);
    const std::uint32_t exponent = (bits >> 23U) & 0xffU;
    std::uint32_t mantissa = bits & 0x7fffffU;

    if (exponent == 0xffU) {
        // Infinity stays infinity; NaN stays a NaN, quiet.
        return static_cast<std::uint16_t>(sign | 0x7c00U | (mantissa != 0 ? 0x200U : 0U));
    }
    const std::int32_t unbiased = static_cast<std::int32_t>(exponent) - 127;
    if (unbiased > 15) {
        return static_cast<std::uint16_t>(sign | 0x7c00U);
    }
    if (unbiased >= -14) {
        // Normal. Round the 13 dropped mantissa bits to nearest even; a carry
        // out of the mantissa correctly bumps the exponent, up to infinity.
        std::uint32_t half = (static_cast<std::uint32_t>(unbiased + 15) << 10U) | (mantissa >> 13U);
        const std::uint32_t dropped = mantissa & 0x1fffU;
        if (dropped > 0x1000U || (dropped == 0x1000U && (half & 1U) != 0)) {
            ++half;
        }
        return static_cast<std::uint16_t>(sign | half);
    }
    if (unbiased < -25) {
        return sign;  // below half the smallest subnormal
    }
    // Subnormal: the implicit leading one becomes explicit and shifts down.
    mantissa |= 0x800000U;
    const auto shift = static_cast<std::uint32_t>(-unbiased - 1);  // 14..24
    std::uint32_t half = mantissa >> shift;
    const std::uint32_t remainder = mantissa & ((1U << shift) - 1U);
    const std::uint32_t midpoint = 1U << (shift - 1U);
    if (remainder > midpoint || (remainder == midpoint && (half & 1U) != 0)) {
        ++half;
    }
    return static_cast<std::uint16_t>(sign | half);
}

float floatFromHalf(std::uint16_t half) noexcept {
    const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000U) << 16U;
    const std::uint32_t exponent = (half >> 10U) & 0x1fU;
    std::uint32_t mantissa = half & 0x3ffU;

    if (exponent == 0x1fU) {
        return std::bit_cast<float>(sign | 0x7f800000U | (mantissa << 13U));
    }
    if (exponent != 0) {
        return std::bit_cast<float>(sign | ((exponent + 112U) << 23U) | (mantissa << 13U));
    }
    if (mantissa == 0) {
        return std::bit_cast<float>(sign);
    }
    // Subnormal half, normal float: shift until the leading one is implicit.
    std::uint32_t shifted = 113U;
    while ((mantissa & 0x400U) == 0) {
        mantissa <<= 1U;
        --shifted;
    }
    return std::bit_cast<float>(sign | (shifted << 23U) | ((mantissa & 0x3ffU) << 13U));
}

HalfImage::HalfImage(const RgbaImage& source) : width_{source.width()}, height_{source.height()} {
    if (!source.isValid()) {
        width_ = 0;
        height_ = 0;
        return;
    }
    const auto pixels = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    halves_.resize(pixels * 4);
    const Rgba* in = source.row(0);
    std::uint16_t* out = halves_.data();
    for (std::size_t i = 0; i < pixels; ++i) {
        out[(i * 4) + 0] = halfFromFloat(in[i].r);
        out[(i * 4) + 1] = halfFromFloat(in[i].g);
        out[(i * 4) + 2] = halfFromFloat(in[i].b);
        out[(i * 4) + 3] = halfFromFloat(in[i].a);
    }
}

void HalfImage::expandInto(RgbaImage& out) const {
    if (out.width() != width_ || out.height() != height_) {
        out = RgbaImage{width_, height_};
    }
    if (!isValid()) {
        return;
    }
    const auto pixels = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    Rgba* to = out.row(0);
    const std::uint16_t* in = halves_.data();
    for (std::size_t i = 0; i < pixels; ++i) {
        to[i] = Rgba{floatFromHalf(in[(i * 4) + 0]), floatFromHalf(in[(i * 4) + 1]),
                     floatFromHalf(in[(i * 4) + 2]), floatFromHalf(in[(i * 4) + 3])};
    }
}

}  // namespace zaro::render
