#include "zaro/core/render/VideoWalk.h"

#include "zaro/core/render/TransitionShape.h"

namespace zaro::render {

namespace {

/// The mask a shape imposes, or null when it imposes none.
///
/// `isSet()` is false for the kinds that mask nothing -- a dissolve is an
/// opacity ramp and covers the whole frame -- and passing an unset mask down as
/// though it were a real one would make every dissolve a full-frame mask
/// multiply for no change in the picture.
const model::Mask* wipeOf(const TransitionSide& side) {
    return side.mask.isSet() ? &side.mask : nullptr;
}

/// One side of a transition, drawn with its shape folded into its own transform.
void drawSide(const model::Sequence& sequence, const model::Clip& clip,
              const time::RationalTime& at, const TransitionSide& side, int which,
              VideoSink& sink) {
    if (!clip.enabled) {
        return;
    }
    sink.draw(clip, shapedTransform(model::pinnedTransformAt(sequence, clip, at), side), at, which,
              wipeOf(side));
}

/// A transition under the playhead, however many clips it has.
///
/// Returns false when the span turned out to have neither side, which is a
/// malformed transition rather than a state worth drawing; the caller then falls
/// through to the ordinary clip on the track.
bool walkTransition(const model::Sequence& sequence, const model::Track& track,
                    const model::Transition& transition, const time::RationalTime& at,
                    VideoSink& sink) {
    const model::Clip* outgoing = track.find(transition.from);
    const model::Clip* incoming = track.find(transition.to);
    const double progress = transition.progressAt(at);

    // A span with one side empty is a fade against whatever is below -- black,
    // where this is the bottom track. The one clip is drawn at the fade's own
    // coverage and nothing is composited under it, which is what makes a fade
    // out on V1 go to black and one on V2 reveal the track beneath rather than
    // punch a hole in it.
    if (transition.isFadeIn() || transition.isFadeOut()) {
        const model::Clip* only = transition.isFadeIn() ? incoming : outgoing;
        // A fade whose one side is missing is malformed, and the answer is to
        // draw nothing rather than to fall through to the clip on the track:
        // that clip is the one the fade was about, and drawing it unfaded would
        // pop it to full opacity for the length of the span. Both render paths
        // took the track out of the frame here, unconditionally, and this keeps
        // that.
        if (only == nullptr) {
            return true;
        }
        // A fade out is a fade in played backwards, so one shape serves both:
        // the shot arrives at `progress` or departs at what is left of it.
        //
        // Through `transitionShapeFor` rather than an opacity of its own, so
        // that a kind means the same thing at the end of a run as it does
        // across a cut -- a wipe uncovers the shot against black, a slide
        // brings it on from off screen. Both paths used to ramp an opacity
        // here, which made every kind look like a dissolve and made the
        // inspector's Type control a lie wherever a span had one side empty.
        //
        // A dissolve is unchanged by that: its shape *is* an opacity of exactly
        // this ramp. Linear, not equal power -- this is coverage against a
        // background, and the eye reads a straight ramp as an even fade. The
        // sound version is equal power for the opposite reason: see AudioGraph.
        const TransitionShape shape =
            transitionShapeFor(transition, transition.isFadeIn() ? progress : 1.0 - progress,
                               sequence.width(), sequence.height());
        // The incoming side either way, because there is only ever one clip
        // here and the shape describes what it does.
        drawSide(sequence, *only, at, shape.incoming, 0, sink);
        return true;
    }

    if (outgoing == nullptr || incoming == nullptr) {
        return false;
    }

    // Both clips contribute to this frame. They never overlap on the timeline,
    // so a transition is the only place two clips from one track do.
    //
    // The outgoing clip is read past its out point and the incoming one before
    // its in point, both reaching into the handles either side of the cut --
    // which is what the backends' own source lookup does, extrapolating
    // linearly, and exactly the mapping wanted here.
    const TransitionShape shape =
        transitionShapeFor(transition, progress, sequence.width(), sequence.height());
    drawSide(sequence, *outgoing, at, shape.outgoing, 0, sink);
    // Over the outgoing clip at the transition's progress: with premultiplied
    // `over` and an opaque source, a dissolve gives out*(1-p) + in*p.
    drawSide(sequence, *incoming, at, shape.incoming, 1, sink);
    return true;
}

}  // namespace

void walkVideo(const model::Sequence& sequence, const time::RationalTime& at, VideoSink& sink) {
    // Bottom-up. Index 0 is V1, the lowest track, and each later track
    // composites over what is already there.
    for (const model::Track& track : sequence.videoTracks()) {
        if (!sequence.isAudible(track)) {
            continue;
        }
        if (const model::Transition* transition = track.transitionAt(at);
            transition != nullptr && walkTransition(sequence, track, *transition, at, sink)) {
            continue;
        }

        const model::Clip* clip = track.clipAt(at);
        if (clip == nullptr || !clip->enabled) {
            continue;
        }

        // An adjustment layer has no picture; it corrects what is beneath it.
        if (clip->adjustment) {
            sink.adjustment(*clip, at);
            continue;
        }

        // Everything else -- media, a nest, a title, a shape -- is a picture
        // drawn through the clip's own transform. Side 0: a lone clip needs one
        // scratch buffer, not two.
        sink.draw(*clip, model::pinnedTransformAt(sequence, *clip, at), at, 0, nullptr);
    }

    // Captions last, over everything: they are a deliverable laid on top of the
    // picture rather than a layer in it, and a caption a later track could cover
    // is a caption nobody can read.
    if (!sequence.captions().isBurnedIn()) {
        return;
    }
    for (const model::Caption* caption : sequence.captions().at(at)) {
        sink.caption(captionGraphic(sequence.captions().style(), caption->text, sequence.width(),
                                    sequence.height()));
    }
}

}  // namespace zaro::render
