#pragma once

#include <cstdint>

#include "zaro/core/model/Ids.h"
#include "zaro/core/time/TimeRange.h"

namespace zaro::model {

enum class TransitionKind : std::uint8_t {
    CrossDissolve,
    /// The incoming shot is revealed behind a moving edge. Both shots stay
    /// where they are; what moves is the boundary.
    Wipe,
    /// The incoming shot travels in from off screen and pushes nothing: the
    /// outgoing one stays put underneath.
    Slide,
    /// The incoming shot is revealed through an ellipse opening from the
    /// centre. A wipe whose edge is a curve rather than a line, which is why
    /// it is the same mechanism with a different shape rather than a new one.
    Iris,
    /// The incoming shot grows from nothing in the centre of the frame, over
    /// the outgoing one. Unlike the four above it moves no edge and blends no
    /// pixels: what changes is the size of the picture.
    Zoom,
    /// A slide that takes the outgoing shot with it: the incoming one comes in
    /// from off screen and shoves the other out ahead of it. The same travel a
    /// slide has, applied to both clips instead of one.
    Push,
    /// Down to black and back up again, rather than a blend: the outgoing shot
    /// goes over the first half of the span and the incoming one arrives over
    /// the second. What sits between them is nothing at all, which is what
    /// makes it black without anything having to draw black.
    DipToBlack,
};

/// Which way a wipe's edge, or a slide's picture, travels.
///
/// The same word for both on purpose: a wipe to the right uncovers from the
/// left, and a slide to the right enters from the left, so a person who has
/// chosen a direction for one already knows what it does for the other.
enum class TransitionDirection : std::uint8_t {
    Right,
    Left,
    Down,
    Up,
};

/// How the blend is paced across the span.
///
/// A dissolve at a constant rate spends most of its length looking like a
/// half-and-half mix of two shots, which is the part that reads as mush. Easing
/// moves that time to the ends, where one shot or the other is still legible.
enum class TransitionEasing : std::uint8_t {
    Linear,
    /// Slow to start, so the outgoing shot is held before it goes.
    In,
    /// Slow to finish, so the incoming shot settles rather than arriving.
    Out,
    /// Slow at both ends. The usual choice, and the one that costs the middle.
    InOut,
};

[[nodiscard]] const char* toString(TransitionKind kind) noexcept;
[[nodiscard]] TransitionKind transitionKindFromString(const char* name) noexcept;
[[nodiscard]] const char* toString(TransitionDirection direction) noexcept;
[[nodiscard]] bool transitionDirectionFromString(const char* name,
                                                 TransitionDirection& out) noexcept;
[[nodiscard]] const char* toString(TransitionEasing easing) noexcept;
[[nodiscard]] TransitionEasing transitionEasingFromString(const char* name) noexcept;

/// Whether this kind has a direction to travel in.
///
/// Asked rather than listed, because two places need the answer and they must
/// not drift: the shape uses it to decide what to compute, and the panel uses
/// it to decide whether to draw the control. A kind that travels in the shape
/// but not in the panel is a transition nobody can aim.
[[nodiscard]] bool transitionTravels(TransitionKind kind) noexcept;

/// Whether this kind has an edge that softening means anything to.
///
/// Narrower than travelling: a slide travels and has no edge, because the
/// whole picture moves and its boundary is the frame. An iris does not travel
/// and has one.
[[nodiscard]] bool transitionHasEdge(TransitionKind kind) noexcept;

/// Shape a linear 0..1 through an easing curve.
///
/// Its own function, and not a lambda inside `progressAt`, because it is the
/// one piece of this that a test can pin down without a project: what a curve
/// does at its ends and at its middle is the whole of what "eased" means.
[[nodiscard]] double easedProgress(double linear, TransitionEasing easing) noexcept;

/// A blend across a cut.
///
/// The two clips stay adjacent and do not overlap on the timeline: a transition
/// is not two clips laid on top of each other, it is a span *straddling* the
/// cut during which both are shown. Keeping the clips non-overlapping means
/// every invariant the track already enforces still holds, and it is what lets
/// a transition be removed without having to work out where the clips should
/// go afterwards.
///
/// During the span the outgoing clip is asked for frames past its out point and
/// the incoming clip for frames before its in point. Those frames come from the
/// media either side of the cut -- the handles -- which is why a transition
/// cannot be added where there is no material to reach into.
struct Transition {
    TransitionId id;
    /// Outgoing. Invalid means there is nothing on the way out: the span is a
    /// fade *in*, from black or from silence.
    ClipId from;
    /// Incoming. Invalid means there is nothing on the way in: the span is a
    /// fade *out*, to black or to silence.
    ///
    /// A one-sided span is not a special case bolted on; it is the same idea
    /// with one side empty, which is why it is spelled as a missing clip
    /// rather than as a separate kind. It also behaves differently in one
    /// respect worth knowing: a two-sided span straddles a cut and reads both
    /// clips into their handles, while a one-sided one lies *inside* its clip
    /// and reads no handles at all -- so a fade out can always be added, even
    /// to a clip that uses every frame of its source.
    ClipId to;
    /// On the timeline, straddling the cut between the two clips.
    time::TimeRange range;
    TransitionKind kind{TransitionKind::CrossDissolve};
    /// Ignored by a cross dissolve, which has no direction to travel in.
    TransitionDirection direction{TransitionDirection::Right};
    /// How soft a wipe's edge is, as a fraction of the distance that edge
    /// travels: 0 is the hard line a wipe has always had, 1 a ramp as wide as
    /// the frame -- which is a wipe so soft it is most of the way to a
    /// dissolve. Ignored by the kinds that have no edge.
    double softness{0.0};
    /// How the blend is paced. Applied in `progressAt` rather than in the
    /// shape, so the picture and the sound are eased by one decision: the
    /// audio crossfade asks the same question of the same function, and an
    /// eased dissolve over a linear crossfade would be two answers.
    TransitionEasing easing{TransitionEasing::Linear};

    /// How far through, from 0 at the start to 1 at the end, after easing.
    [[nodiscard]] double progressAt(const time::RationalTime& t) const;

    /// Whether this span joins two clips rather than fading one against
    /// nothing.
    [[nodiscard]] bool isCrossFade() const noexcept { return from.isValid() && to.isValid(); }
    /// A fade up from black or silence.
    [[nodiscard]] bool isFadeIn() const noexcept { return !from.isValid() && to.isValid(); }
    /// A fade down to black or silence.
    [[nodiscard]] bool isFadeOut() const noexcept { return from.isValid() && !to.isValid(); }

    friend bool operator==(const Transition&, const Transition&) = default;
};

}  // namespace zaro::model
