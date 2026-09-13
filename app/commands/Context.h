// What a feature operation needs, and nothing about the window it came from.
//
// The operations in this directory are the ones that used to live in
// PreviewWindow: match this shot to the one being held, reframe that clip for a
// different shape, fit this music to a length, steady this camera move. They
// were methods on the window because they need the project, the sequence, what
// is selected, the media and the render cache -- and the window is where all of
// those happen to be kept.
//
// They are not about the window. Each is a piece of editing: read some model,
// look at some frames, build a command, execute it. What made them look like
// window code was the four or five lines each ended with, telling the monitor
// and the panels to redraw. Those lines are the window's business and stay
// there; see PreviewWindow::afterEdit.
//
// Free functions over this struct, so that adding a feature means adding a file
// rather than another method to the largest class in the program.
#pragma once

#include <cstdint>
#include <functional>

#include "zaro/core/edit/Operations.h"
#include "zaro/core/model/Clip.h"
#include "zaro/core/model/Project.h"
#include "zaro/core/model/Sequence.h"
#include "zaro/core/render/RenderCache.h"
#include "zaro/core/time/RationalTime.h"
#include "zaro/platform/ffmpeg/FFmpegRender.h"
#include "zaro/platform/qtext/QtTextRasterizer.h"
#include "zaro/ui/SequenceBinding.h"

namespace zaro::app::commands {

/// How far along a long operation is; return false to stop it.
///
/// The operations here do not know what a progress dialog is. The one that
/// counted frames used to build one itself, which is why it could not be called
/// from anywhere that did not have a window to parent it to.
using Progress = std::function<bool(std::int64_t done, std::int64_t total)>;

/// The project, what is picked in it, and what is needed to look at frames.
///
/// A view, not an owner: everything here belongs to the window and outlives the
/// call. Built fresh at each call site rather than cached, so it cannot be
/// stale -- which is the same reason `sequence` is an id and not a pointer.
struct Context {
    ui::SequenceBinding binding;
    /// What the timeline says is picked.
    model::TrackId track;
    model::ClipId clip;
    /// Where the playhead is.
    time::RationalTime position;
    platform::ffmpeg::ProjectMediaSource* media{nullptr};
    render::RenderCache* cache{nullptr};
    platform::qtext::QtTextRasterizer* text{nullptr};

    [[nodiscard]] model::Project& project() const { return *binding.project; }
    [[nodiscard]] edit::CommandStack& commands() const { return *binding.commands; }
    [[nodiscard]] const model::Sequence* sequence() const { return binding.sequenceOrNull(); }

    /// The selected clip, or null if there is not one.
    ///
    /// Null covers every way of not having one -- no sequence, no such track, no
    /// such clip -- because to an operation they are the same situation and
    /// produce the same message.
    [[nodiscard]] const model::Clip* selectedClip() const {
        const model::Sequence* seq = sequence();
        const model::Track* on = seq != nullptr ? seq->findTrack(track) : nullptr;
        return on != nullptr ? on->find(clip) : nullptr;
    }

    /// Where an edit is aimed: this sequence, this track.
    [[nodiscard]] edit::EditTarget target() const {
        return edit::EditTarget{binding.sequence, track};
    }
};

/// What an analysis reads, with no way to write anything.
///
/// The long half of an analysis -- compositing every frame of a shot, or
/// seeking a decoder through it -- runs on a worker thread, and this is what
/// that thread is handed. It is deliberately not a `Context`: there is no
/// command stack on it, because a worker has no business executing one against
/// a model the UI thread owns.
///
/// **Everything in here belongs to the thread running the analysis**, and the
/// project is a copy rather than the window's own. That is not caution.
/// `ProjectMediaSource` keeps a decoder per file and memoises the last frame
/// out of each, and there is no lock anywhere in it; the window goes on
/// compositing the monitor for every repaint while an analysis runs, because a
/// modal dialog stops input and not painting. Sharing one would put two threads
/// through the same libavcodec decoder, which does not show up as a wrong pixel.
struct AnalysisInput {
    const model::Project* project{nullptr};
    model::SequenceId sequence;
    model::TrackId track;
    model::ClipId clip;
    /// Where the playhead was when the analysis was asked for.
    time::RationalTime position;
    platform::ffmpeg::ProjectMediaSource* media{nullptr};
    platform::qtext::QtTextRasterizer* text{nullptr};

    [[nodiscard]] const model::Sequence* sequenceOrNull() const {
        return project != nullptr ? project->findSequence(sequence) : nullptr;
    }

    /// The clip the analysis is about, or null. Null covers every way of not
    /// having one, for the reason Context::selectedClip does.
    [[nodiscard]] const model::Clip* selectedClip() const {
        const model::Sequence* seq = sequenceOrNull();
        const model::Track* on = seq != nullptr ? seq->findTrack(track) : nullptr;
        return on != nullptr ? on->find(clip) : nullptr;
    }
};

/// The same view, onto what the window itself holds.
///
/// For the synchronous path -- the tests, and `trackMaskForward` and friends,
/// which still run an analysis and apply it in one call. Safe there precisely
/// because it is the UI thread reading its own model.
[[nodiscard]] inline AnalysisInput inputFor(const Context& context) {
    AnalysisInput input;
    input.project = &context.project();
    input.sequence = context.binding.sequence;
    input.track = context.track;
    input.clip = context.clip;
    input.position = context.position;
    input.media = context.media;
    input.text = context.text;
    return input;
}

}  // namespace zaro::app::commands
