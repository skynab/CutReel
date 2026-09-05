#pragma once

#include <cstdint>

#include "zaro/core/model/ClipEffects.h"
#include "zaro/core/model/Mask.h"
#include "zaro/core/model/Transition.h"

namespace zaro::render {

/// How one of the two clips is drawn part way through a transition.
///
/// Everything here composes with what the clip already says rather than
/// replacing it: a clip somebody has faded to 60% or scaled to half should
/// arrive that way, not be silently reset because it happens to be in a
/// transition.
struct TransitionSide {
    /// Multiplies whatever the clip's own opacity is. A dissolve is entirely
    /// this on the incoming side; a dip is this on both.
    double opacity{1.0};

    /// Added to the clip's own position. A slide is entirely this on the
    /// incoming side; a push is this on both.
    double offsetX{0.0};
    double offsetY{0.0};

    /// Multiplies the clip's own scale. A zoom is entirely this.
    double scaleX{1.0};
    double scaleY{1.0};

    /// Where this clip shows, in output coordinates -- the same space a clip's
    /// own mask lives in. A wipe and an iris are entirely this.
    ///
    /// `isSet()` is false for the kinds that mask nothing. It does not replace
    /// the clip's own mask: both apply, and their coverages multiply, because
    /// a masked clip in a wipe should be shown where the mask says *and* where
    /// the wipe has got to.
    model::Mask mask;
};

/// How both clips are drawn part way through a transition.
///
/// One function, called by both render paths. The alternative -- each path
/// working out where a wipe's edge is -- is two answers to one question, and
/// this project has already paid for that once: the outgoing half of a
/// transition went two phases ungraded because three draw sites had drifted.
///
/// Both sides rather than only the incoming one, which is what this described
/// until a push and a dip needed otherwise. Those two are not incoming-side
/// effects with a trick: in a push the outgoing shot is shoved off the frame,
/// and in a dip it fades away before the other arrives. Expressing them by
/// modifying the incoming clip alone is not possible, and the alternative --
/// each render path special-casing two kinds -- is the drift this file exists
/// to prevent.
struct TransitionShape {
    /// The clip on its way in. Every kind touches this one.
    TransitionSide incoming;
    /// The clip on its way out. Left at its defaults by every kind that leaves
    /// the outgoing shot where it is, which is most of them.
    TransitionSide outgoing;
};

/// Work out the shape at a moment, given the frame it is drawn into.
[[nodiscard]] TransitionShape transitionShapeFor(const model::Transition& transition,
                                                 double progress, std::int32_t width,
                                                 std::int32_t height);

/// Compose a side onto whatever the clip's own transform already says.
///
/// Here rather than spelled out at each draw site, for the reason the shape
/// itself is one function: there are four such sites across two render paths,
/// and a side applied in three of them is a transition that looks different in
/// the preview and the export. It was three fields when the sites were written
/// out by hand and is five now, which is how that drift starts.
[[nodiscard]] model::Transform shapedTransform(model::Transform own, const TransitionSide& side);

}  // namespace zaro::render
