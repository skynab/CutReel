// Which clips are on a frame, in what order, and what a transition does to
// them. One traversal, for every backend that draws one.
//
// **Why this is its own file.** There were two of these: `RenderGraph`
// composites a frame on the CPU and `GpuRenderGraph` queues one on the GPU, and
// each walked the sequence itself. The walks had to agree -- a preview that
// disagrees with an export is the failure this render path is written to avoid
// -- and agreement was maintained by ten comments saying so. Variants of "the
// CPU path's twin, and it has to stay its twin" appeared nine times in
// GpuRenderGraph.cpp alone.
//
// Convention did not hold, twice, in the space of a month:
//
//   * A fade at the head or tail of a run went down a separate branch in *both*
//     paths that ramped an opacity of its own and never asked the transition
//     what kind it was, so every kind looked like a dissolve there. Fixed in
//     both. (`10b545d`)
//   * A dissolve onto a nested sequence drew nothing, silently, in *both*
//     paths, because the function each used to resolve a transition side knew
//     about media and graphics and not about nests. Fixed in both. (`27a4c7b`)
//
// Both were one bug that had to be found once and fixed twice. With the
// traversal here, that class of divergence is not expressible: there is one
// walk, and a backend supplies only the drawing.
//
// **What the walk decides**, and therefore what a backend no longer may:
// track order and which tracks are audible, whether a transition is under the
// playhead, whether it is a cross fade or a fade against what is below, which
// clip is the sole side of a fade, how far through it is, what shape that makes
// at this instant, how that shape folds into the clip's own pinned transform,
// which side gets which scratch buffer, and that burned-in captions go last.
//
// **What a backend decides:** how to turn a clip into pixels. That is the only
// thing the two actually do differently.
#pragma once

#include "zaro/core/model/Clip.h"
#include "zaro/core/model/Mask.h"
#include "zaro/core/model/Sequence.h"
#include "zaro/core/render/TextRasterizer.h"
#include "zaro/core/time/RationalTime.h"

namespace zaro::render {

/// Where a walk sends what it finds.
///
/// Every method is told what to draw and where, never what kind of clip it is:
/// a nest, a title, a shape and a piece of footage differ in how their picture
/// is obtained and in nothing else, and both backends already had one function
/// that obtained any of them. `draw` is that function.
class VideoSink {
public:
    VideoSink() = default;
    VideoSink(const VideoSink&) = delete;
    VideoSink& operator=(const VideoSink&) = delete;
    VideoSink(VideoSink&&) = delete;
    VideoSink& operator=(VideoSink&&) = delete;
    virtual ~VideoSink() = default;

    /// Draw one clip's picture, whatever kind of clip it is.
    ///
    /// `transform` is final: pinned to whatever the clip follows, and with any
    /// transition shape already folded in. A backend that recomputed either
    /// would be the second opinion this file exists to remove.
    ///
    /// `wipe` is the mask a transition's shape imposes -- the travelling edge of
    /// a wipe, the aperture of an iris -- or null. It does not replace the
    /// clip's own mask; see TransitionSide::mask for why both apply.
    ///
    /// `side` is 0 for a lone clip or a transition's outgoing half and 1 for its
    /// incoming half. It picks a scratch buffer: both halves of a dissolve are
    /// resolved before either is composited, so a backend that generates a
    /// picture into a buffer needs two.
    ///
    /// Failure is silent by design. A clip whose picture cannot be resolved --
    /// unreadable media, a nest that is missing, a title with no font engine --
    /// leaves a gap, which is visible and diagnosable. Returning an error would
    /// make one bad clip stall the edit.
    virtual void draw(const model::Clip& clip, const model::Transform& transform,
                      const time::RationalTime& at, int side, const model::Mask* wipe) = 0;

    /// An adjustment layer: correct what is already composited beneath it.
    ///
    /// Separate from `draw` because it has no picture of its own -- it is a
    /// correction applied to the accumulated frame, which is why the GPU path
    /// cannot queue one and composites the whole frame on the CPU instead when
    /// a sequence has any. See GpuRenderGraph::needsCpuFallback.
    virtual void adjustment(const model::Clip& clip, const time::RationalTime& at) = 0;

    /// One burned-in caption, over everything that was drawn.
    ///
    /// Handed a graphic rather than the text, so that both backends put a
    /// caption in the same place for the same style: the placement is in
    /// `captionGraphic`, and a second copy of it is a second answer.
    virtual void caption(const model::Graphic& graphic) = 0;
};

/// Walk a sequence's video tracks at an instant and drive `sink`.
///
/// Bottom track first: index 0 is V1, and each later track composites over what
/// is already there.
///
/// Draws nothing itself and holds no state, so it is safe to call from a nested
/// composite -- which is how a nested sequence is rendered, one level down, by
/// whichever backend is walking.
void walkVideo(const model::Sequence& sequence, const time::RationalTime& at, VideoSink& sink);

}  // namespace zaro::render
