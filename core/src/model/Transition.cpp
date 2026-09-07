#include "zaro/core/model/Transition.h"

#include <algorithm>
#include <cstring>

namespace zaro::model {

const char* toString(TransitionKind kind) noexcept {
    switch (kind) {
        case TransitionKind::Wipe:
            return "wipe";
        case TransitionKind::Slide:
            return "slide";
        case TransitionKind::Iris:
            return "iris";
        case TransitionKind::Zoom:
            return "zoom";
        case TransitionKind::Push:
            return "push";
        case TransitionKind::DipToBlack:
            return "dipToBlack";
        case TransitionKind::CrossDissolve:
        default:
            return "crossDissolve";
    }
}

TransitionKind transitionKindFromString(const char* name) noexcept {
    // A name this build does not know falls back to a dissolve rather than
    // refusing the project: an unfamiliar transition is a cut somebody can
    // still watch, and the alternative is a file that will not open.
    if (name != nullptr) {
        for (const TransitionKind kind :
             {TransitionKind::CrossDissolve, TransitionKind::Wipe, TransitionKind::Slide,
              TransitionKind::Iris, TransitionKind::Zoom, TransitionKind::Push,
              TransitionKind::DipToBlack}) {
            if (std::strcmp(name, toString(kind)) == 0) {
                return kind;
            }
        }
    }
    return TransitionKind::CrossDissolve;
}

bool transitionTravels(TransitionKind kind) noexcept {
    // An iris opens from the centre and a zoom grows there, so neither has a
    // side to come from. A dissolve and a dip have no geometry at all.
    return kind == TransitionKind::Wipe || kind == TransitionKind::Slide ||
           kind == TransitionKind::Push;
}

bool transitionHasEdge(TransitionKind kind) noexcept {
    return kind == TransitionKind::Wipe || kind == TransitionKind::Iris;
}

const char* toString(TransitionDirection direction) noexcept {
    switch (direction) {
        case TransitionDirection::Left:
            return "left";
        case TransitionDirection::Down:
            return "down";
        case TransitionDirection::Up:
            return "up";
        case TransitionDirection::Right:
        default:
            return "right";
    }
}

const char* toString(TransitionEasing easing) noexcept {
    switch (easing) {
        case TransitionEasing::In:
            return "in";
        case TransitionEasing::Out:
            return "out";
        case TransitionEasing::InOut:
            return "inOut";
        case TransitionEasing::Linear:
        default:
            return "linear";
    }
}

TransitionEasing transitionEasingFromString(const char* name) noexcept {
    // A curve this build does not know falls back to a constant rate, for the
    // reason an unknown kind falls back to a dissolve: a cut somebody can still
    // watch beats a file that will not open.
    if (name != nullptr) {
        for (const TransitionEasing easing : {TransitionEasing::Linear, TransitionEasing::In,
                                              TransitionEasing::Out, TransitionEasing::InOut}) {
            if (std::strcmp(name, toString(easing)) == 0) {
                return easing;
            }
        }
    }
    return TransitionEasing::Linear;
}

double easedProgress(double linear, TransitionEasing easing) noexcept {
    const double t = std::clamp(linear, 0.0, 1.0);
    switch (easing) {
        // Quadratic in and out, and the smoothstep between them. Deliberately
        // the mildest curves that read as eased at all: a transition is
        // usually under a second, and a steeper curve spends so little of it
        // moving that the blend reads as a cut with a stutter either side.
        case TransitionEasing::In:
            return t * t;
        case TransitionEasing::Out:
            return t * (2.0 - t);
        case TransitionEasing::InOut:
            return t * t * (3.0 - (2.0 * t));
        case TransitionEasing::Linear:
        default:
            return t;
    }
}

bool transitionDirectionFromString(const char* name, TransitionDirection& out) noexcept {
    if (name == nullptr) {
        return false;
    }
    for (const TransitionDirection direction :
         {TransitionDirection::Right, TransitionDirection::Left, TransitionDirection::Down,
          TransitionDirection::Up}) {
        if (std::strcmp(name, toString(direction)) == 0) {
            out = direction;
            return true;
        }
    }
    return false;
}

double Transition::progressAt(const time::RationalTime& t) const {
    if (range.isEmpty()) {
        return 1.0;
    }
    const double elapsed = (t - range.start()).toSecondsDouble();
    const double total = range.duration().toSecondsDouble();
    if (total <= 0.0) {
        return 1.0;
    }
    // Eased here, which is what puts the picture and the sound on one curve:
    // both render paths and the audio crossfade ask this one question, and
    // easing anywhere further down would reach only whichever of them it was
    // written into.
    return easedProgress(elapsed / total, easing);
}

}  // namespace zaro::model
