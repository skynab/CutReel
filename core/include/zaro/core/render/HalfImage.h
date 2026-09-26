#pragma once

#include <cstdint>
#include <vector>

#include "zaro/core/render/RgbaImage.h"

namespace zaro::render {

/// IEEE 754 binary16 from a float, rounded to nearest even. Infinities and NaN
/// survive; anything past the largest half becomes infinity, as the hardware
/// conversion does.
[[nodiscard]] std::uint16_t halfFromFloat(float value) noexcept;
[[nodiscard]] float floatFromHalf(std::uint16_t half) noexcept;

/// A working-space frame at half precision, for keeping rather than working on.
///
/// Half the bytes of an RgbaImage, which is what decides how much of a timeline
/// a render cache can hold: 33 MB a frame at 1080p filled a gigabyte in about a
/// second of footage. Half rather than 8-bit because the working space is scene
/// linear and goes past 1.0 -- 8 bits would clip the highlights and band the
/// shadows, where half keeps eleven bits of precision across the whole range,
/// well below anything a display can show.
///
/// Laid out as RGBA16F, so the GPU can take it as it is.
class HalfImage {
public:
    HalfImage() = default;
    explicit HalfImage(const RgbaImage& source);

    HalfImage(const HalfImage&) = delete;
    HalfImage& operator=(const HalfImage&) = delete;
    HalfImage(HalfImage&&) noexcept = default;
    HalfImage& operator=(HalfImage&&) noexcept = default;
    ~HalfImage() = default;

    [[nodiscard]] bool isValid() const noexcept { return width_ > 0 && height_ > 0; }
    [[nodiscard]] std::int32_t width() const noexcept { return width_; }
    [[nodiscard]] std::int32_t height() const noexcept { return height_; }
    /// Four halves a pixel, rows packed with no padding.
    [[nodiscard]] const std::uint16_t* data() const noexcept { return halves_.data(); }
    [[nodiscard]] std::size_t byteSize() const noexcept {
        return halves_.size() * sizeof(std::uint16_t);
    }

    /// Back to full precision, reusing `out`'s storage when it is already the
    /// right size.
    void expandInto(RgbaImage& out) const;

private:
    std::vector<std::uint16_t> halves_;
    std::int32_t width_{0};
    std::int32_t height_{0};
};

}  // namespace zaro::render
