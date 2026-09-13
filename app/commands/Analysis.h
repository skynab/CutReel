// Operations that look at the frames and write what they find.
#pragma once

#include "zaro/core/Error.h"
#include "zaro/core/render/Reframe.h"
#include "zaro/core/render/ShotMatch.h"
#include "zaro/core/render/Stabilise.h"

#include "Context.h"

namespace zaro::app::commands {

/// What tracking a mask through the shot came to.
struct MaskTrack {
    int frames{0};
    double confidence{1.0};
    /// Set when the track stopped early, saying why. The keyframes found
    /// before it stopped are kept: a track that held for two seconds and
    /// then lost the thing it was on is worth two seconds of keyframes and
    /// a note, not a refusal.
    std::string stopped;
};

/// Match the selected clip to the frame being held as the reference.
///
/// Returns the match so a caller can say what happened. Nothing is applied
/// when the two shots are too unalike: an automatic grade that is confidently
/// wrong is worse than none, and the person looking at both frames is better
/// placed to decide than a distance measure is.
Result<render::ShotMatch> matchToReference(const Context& context,
                                           const time::RationalTime& reference);

/// Follow the selected clip's mask through the rest of the clip.
///
/// **Frame to frame, not against the first frame.** A reference frame does
/// not drift, but it also stops matching the moment the thing turns, moves
/// under a different light, or is partly covered -- which is most shots
/// worth tracking. Frame to frame follows all of that and accumulates a
/// little error instead, which is the trade every tracker makes and the one
/// people can correct by hand afterwards.
///
/// **On the composited picture, not on the decoded source.** The mask lives
/// in output coordinates over whatever is on screen, so what it has to
/// follow is what is on screen.
///
/// `tell` is asked once a frame and stops the track when it answers false, the
/// same bargain detectScenes makes. Without it this composited every frame of
/// the clip behind a wait cursor, which on a long one is a window that looks
/// hung with no way out but to kill it.
Result<MaskTrack> trackMaskForward(const Context& context, const Progress& tell = {});

/// What tracking found, before any of it is in the project.
struct MaskTrackAnalysis {
    MaskTrack report;
    model::Curve x;
    model::Curve y;
};

/// The long half of trackMaskForward: composite every frame and follow the
/// mask, writing nothing.
///
/// Split out so it can be run on a worker thread. It reads only what is in
/// `input` -- see AnalysisInput for why that has to be a copy -- and hands back
/// the curves rather than executing a command, because the command has to be
/// executed against the window's live project by the thread that owns it.
Result<MaskTrackAnalysis> analyseMaskTrack(const AnalysisInput& input, const Progress& tell = {});

/// The short half: put the tracked curves into the project, as one undo step.
/// Main thread only.
Status applyMaskTrack(const Context& context, const MaskTrackAnalysis& analysed);

/// Hold the selected clip still.
///
/// **On the clip's own frames, not on the composite.** What is being
/// measured is how the camera moved, and the composite already has this
/// clip's transform applied to it -- including the correction being
/// computed, which would make the analysis chase its own tail. It also has
/// whatever is layered over the clip in it, which moved for reasons of its
/// own.
///
/// Reports progress and can be stopped, for the reason trackMaskForward can.
Result<render::StabiliseResult> stabiliseClip(const Context& context, const Progress& tell = {});

/// What the stabiliser measured, before any of it is in the project.
struct StabiliseAnalysis {
    render::StabiliseResult report;
    model::Curve x;
    model::Curve y;
};

/// The long half of stabiliseClip, and the short half. Split for the reason
/// analyseMaskTrack is.
Result<StabiliseAnalysis> analyseStabilise(const AnalysisInput& input, const Progress& tell = {});
Status applyStabilise(const Context& context, const StabiliseAnalysis& analysed);

/// Recompose the selected clip to fill the sequence's frame.
///
/// **On the clip's own frames**, like the stabiliser and for the same
/// reason: the composite already has this clip's transform on it, and the
/// transform is what is being decided.
Result<render::ReframeResult> reframeClip(const Context& context);

}  // namespace zaro::app::commands
