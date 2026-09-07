#pragma once

#include <cstdint>

namespace zaro::model {

/// How a clip is placed in the frame.
///
/// These are geometry, not time, so they are doubles rather than rationals.
/// A position is a continuous quantity that never has to be exact for two
/// values to line up; a frame boundary does, which is why time is rational and
/// this is not.
///
/// Coordinates are in output pixels with the origin at the centre of the frame,
/// which is what makes scale and rotation behave the way people expect without
/// having to think about the frame size.
struct Transform {
    double positionX{0.0};
    double positionY{0.0};
    double scaleX{1.0};
    double scaleY{1.0};
    double rotationDegrees{0.0};
    /// The point the clip scales and rotates about, in source pixels relative
    /// to the source centre.
    double anchorX{0.0};
    double anchorY{0.0};
    double opacity{1.0};

    /// How much of each edge of the source is thrown away, as a percentage of
    /// that side: 0 keeps all of it, 100 keeps none.
    ///
    /// **A percentage, not pixels.** A crop of "10% off the left" means the
    /// same thing to a 4K master and to the 1080 proxy standing in for it,
    /// which is what keeps the picture from jumping when proxies are switched
    /// on. In pixels those are two different crops.
    ///
    /// **Cut, not fitted.** What is cropped away becomes transparent and what
    /// is left stays exactly where it was -- the crop does not zoom the
    /// remainder back up to fill the frame. That is what makes crop and scale
    /// two controls rather than one: cropping says which part of the shot to
    /// keep, and position and scale say where on screen to put it. Together
    /// they are how several pictures are arranged in one frame, which is the
    /// thing this exists for.
    ///
    /// **On the transform rather than in the effect list.** It is geometry, it
    /// is read on the same pass that samples the source, and both renderers
    /// already take a Transform per clip. An entry in the effect stack would be
    /// a second place for geometry to live.
    ///
    /// Opposing edges that meet or cross leave nothing: the clip draws nothing
    /// rather than folding inside out.
    double cropLeft{0.0};
    double cropRight{0.0};
    double cropTop{0.0};
    double cropBottom{0.0};

    [[nodiscard]] bool isCropped() const noexcept {
        return cropLeft != 0.0 || cropRight != 0.0 || cropTop != 0.0 || cropBottom != 0.0;
    }

    [[nodiscard]] bool isIdentity() const noexcept {
        return positionX == 0.0 && positionY == 0.0 && scaleX == 1.0 && scaleY == 1.0 &&
               rotationDegrees == 0.0 && anchorX == 0.0 && anchorY == 0.0 && opacity == 1.0 &&
               !isCropped();
    }

    friend bool operator==(const Transform&, const Transform&) = default;
};

enum class BlendMode : std::uint8_t { Normal, Add, Multiply, Screen };

[[nodiscard]] const char* toString(BlendMode mode) noexcept;
[[nodiscard]] BlendMode blendModeFromString(const char* name) noexcept;

}  // namespace zaro::model
