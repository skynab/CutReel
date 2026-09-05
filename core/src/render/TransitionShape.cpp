#include "zaro/core/render/TransitionShape.h"

#include <algorithm>
#include <cstdint>

namespace zaro::render {

model::Transform shapedTransform(model::Transform own, const TransitionSide& side) {
    own.opacity *= side.opacity;
    own.positionX += side.offsetX;
    own.positionY += side.offsetY;
    own.scaleX *= side.scaleX;
    own.scaleY *= side.scaleY;
    return own;
}

TransitionShape transitionShapeFor(const model::Transition& transition, double progress,
                                   std::int32_t width, std::int32_t height) {
    TransitionShape out;
    const double t = std::clamp(progress, 0.0, 1.0);
    const auto frameWidth = static_cast<double>(width);
    const auto frameHeight = static_cast<double>(height);

    switch (transition.kind) {
        case model::TransitionKind::Wipe: {
            // A rectangle covering the part of the frame the edge has passed,
            // growing from the side the direction comes from.
            const bool horizontal = transition.direction == model::TransitionDirection::Right ||
                                    transition.direction == model::TransitionDirection::Left;
            const double travel = horizontal ? frameWidth : frameHeight;
            // Softness is a fraction of the distance the edge travels, so the
            // same number reads the same on a wide frame and a tall one.
            const double feather = std::clamp(transition.softness, 0.0, 1.0) * travel;

            // Two consequences of a soft edge, and both fall out of one number
            // rather than needing a branch. A ramp `feather` wide reaches half
            // that either side of the boundary, so:
            //
            //  - the boundary starts and ends half a ramp beyond the frame,
            //    or a wipe would begin already showing a sliver of the
            //    incoming shot and end still veiling a sliver of it;
            //  - the other three sides of the rectangle sit a whole ramp
            //    outside the frame, so their own ramps never reach a pixel
            //    anybody sees. Without that a wipe would finish with the
            //    incoming clip softened along all four edges, which is a
            //    vignette rather than a wipe.
            //
            // At a softness of zero the feather is zero, the padding with it,
            // and every number below is exactly what a hard wipe has always
            // produced -- so this is one formula rather than two paths.
            const double pad = feather;
            const double edge = -(travel / 2.0) - (feather / 2.0) + (t * (travel + feather));
            // Right and Down sweep from the low side; Left and Up mirror them,
            // covering from the high side instead.
            const bool forward = transition.direction == model::TransitionDirection::Right ||
                                 transition.direction == model::TransitionDirection::Down;
            const double far = (travel / 2.0) + pad;
            const double lo = forward ? -far : -edge;
            const double hi = forward ? edge : far;

            out.incoming.mask.shape = model::MaskShape::Rectangle;
            out.incoming.mask.feather = feather;
            const double across = (horizontal ? frameHeight : frameWidth) + (2.0 * pad);
            if (horizontal) {
                out.incoming.mask.width = hi - lo;
                out.incoming.mask.centreX = (lo + hi) / 2.0;
                out.incoming.mask.height = across;
            } else {
                out.incoming.mask.height = hi - lo;
                out.incoming.mask.centreY = (lo + hi) / 2.0;
                out.incoming.mask.width = across;
            }
            return out;
        }
        case model::TransitionKind::Slide:
        case model::TransitionKind::Push: {
            // Off screen at the start, home at the end. A slide leaves the
            // outgoing clip where it is underneath; a push shoves it out of the
            // frame ahead of the incoming one, which is the whole difference
            // between them -- so they share the arithmetic and differ by one
            // line rather than being two switches that have to agree about
            // what "right" means.
            const double remaining = 1.0 - t;
            const bool pushes = transition.kind == model::TransitionKind::Push;
            switch (transition.direction) {
                case model::TransitionDirection::Right:
                    out.incoming.offsetX = -frameWidth * remaining;
                    out.outgoing.offsetX = pushes ? frameWidth * t : 0.0;
                    break;
                case model::TransitionDirection::Left:
                    out.incoming.offsetX = frameWidth * remaining;
                    out.outgoing.offsetX = pushes ? -frameWidth * t : 0.0;
                    break;
                case model::TransitionDirection::Down:
                    out.incoming.offsetY = -frameHeight * remaining;
                    out.outgoing.offsetY = pushes ? frameHeight * t : 0.0;
                    break;
                case model::TransitionDirection::Up:
                    out.incoming.offsetY = frameHeight * remaining;
                    out.outgoing.offsetY = pushes ? -frameHeight * t : 0.0;
                    break;
            }
            return out;
        }
        case model::TransitionKind::Iris: {
            // The same mechanism a wipe uses with a different shape in it: an
            // ellipse opening from the centre rather than a line crossing the
            // frame. Both paths already composite an elliptical mask, so this
            // costs a branch rather than a feature.
            //
            // Scaled by root two at the end, which is what makes the ellipse
            // pass exactly through the corners of the frame: on the axes it
            // reaches half its width and half its height, and a corner is
            // further away than either by that factor. Without it the wipe
            // would finish with four unrevealed corners.
            constexpr double kCircumscribe = 1.41421356237309505;
            const double feather =
                std::clamp(transition.softness, 0.0, 1.0) * std::min(frameWidth, frameHeight);
            // Grown half a ramp past each end, for the reason a wipe's edge is:
            // an iris has to open from nothing and finish showing everything,
            // and a soft edge reaches half its width either side of where the
            // boundary nominally is. A negative half-extent is no ellipse at
            // all, which is exactly the "nothing" wanted at the start.
            const double halfWidth =
                (t * (((frameWidth * kCircumscribe) / 2.0) + feather)) - (feather / 2.0);
            const double halfHeight =
                (t * (((frameHeight * kCircumscribe) / 2.0) + feather)) - (feather / 2.0);
            out.incoming.mask.shape = model::MaskShape::Ellipse;
            out.incoming.mask.feather = feather;
            out.incoming.mask.width = 2.0 * halfWidth;
            out.incoming.mask.height = 2.0 * halfHeight;
            return out;
        }
        case model::TransitionKind::Zoom:
            // Nothing is blended and no edge moves: the incoming shot simply
            // grows from nothing in the middle of the frame, over the outgoing
            // one, which stays where it is. The one kind here that is a
            // transform rather than a mask or an opacity.
            out.incoming.scaleX = t;
            out.incoming.scaleY = t;
            return out;
        case model::TransitionKind::DipToBlack:
            // Two fades end to end rather than a blend: the outgoing shot goes
            // over the first half, the incoming one comes up over the second,
            // and in between there is nothing at all.
            //
            // The black is the absence of both rather than a colour drawn
            // between them, which is why this needs no fill primitive and why
            // it is dip to *black* rather than dip to a colour anybody picks.
            // A chosen colour would need a solid drawn over the outgoing clip
            // and under the incoming one -- a new operation in both render
            // paths, which is a bigger thing than this and not one either path
            // can currently do.
            out.outgoing.opacity = std::clamp(1.0 - (2.0 * t), 0.0, 1.0);
            out.incoming.opacity = std::clamp((2.0 * t) - 1.0, 0.0, 1.0);
            return out;
        case model::TransitionKind::CrossDissolve:
        default:
            // With premultiplied `over` and an opaque source this gives
            // out*(1-p) + in*p, which is what a dissolve is.
            out.incoming.opacity = t;
            return out;
    }
}

}  // namespace zaro::render
