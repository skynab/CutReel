// Media, tracks and the sequence itself.
//
// The operations whose subject is the project rather than a cut: importing
// media and relinking it, adding and removing tracks, conforming a sequence
// to a new shape.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "zaro/core/Check.h"
#include "zaro/core/edit/Operations.h"
#include "zaro/core/media/Waveform.h"

#include "OperationsCommon.h"

namespace zaro::edit {
namespace {

using model::Clip;
using model::ClipId;
using model::Project;
using model::Sequence;
using model::Track;
using model::TrackId;
using time::RationalTime;
using time::TimeRange;

// The helpers more than one operation file needs; see OperationsCommon.h.
using detail::atRate;
using detail::locate;
using detail::makeCommand;
using detail::trimmedIn;
using detail::trimmedOut;

class MediaNotesCommand final : public ProjectCommand {
public:
    MediaNotesCommand(model::MediaRefId media, std::string notes, std::string description)
        : ProjectCommand{std::move(description), "notes:" + std::to_string(media.value())},
          media_{media},
          notes_{std::move(notes)} {}

protected:
    void mutate(Project& project) override {
        for (model::MediaRef& media : project.mediaMutable()) {
            if (media.id == media_) {
                media.notes = notes_;
            }
        }
    }

private:
    model::MediaRefId media_;
    std::string notes_;
};

/// A grade on the file, merged by media id so a wheel drag is one undo step.
///
/// Merged the way the notes command is, and for the same reason: a colourist
/// pushing a wheel emits a correction per mouse move, and an undo stack that
/// recorded each of them would need fifty presses to get back to where the drag
/// started.
class MediaGradeCommand final : public ProjectCommand {
public:
    MediaGradeCommand(model::MediaRefId media, model::ColorCorrection color,
                      model::ColorWheels wheels, model::LutRef lut, std::string description)
        : ProjectCommand{std::move(description), "mediaGrade:" + std::to_string(media.value())},
          media_{media},
          color_{color},
          wheels_{wheels},
          lut_{std::move(lut)} {}

protected:
    void mutate(Project& project) override {
        for (model::MediaRef& media : project.mediaMutable()) {
            if (media.id == media_) {
                media.color = color_;
                media.wheels = wheels_;
                media.lut = lut_;
            }
        }
    }

private:
    model::MediaRefId media_;
    model::ColorCorrection color_;
    model::ColorWheels wheels_;
    model::LutRef lut_;
};

class RelinkMediaCommand final : public ProjectCommand {
public:
    RelinkMediaCommand(model::MediaRefId media, std::string path, std::string digest,
                       std::string description)
        : ProjectCommand{std::move(description)},
          media_{media},
          path_{std::move(path)},
          digest_{std::move(digest)} {}

protected:
    void mutate(Project& project) override {
        for (model::MediaRef& media : project.mediaMutable()) {
            if (media.id == media_) {
                media.path = path_;
                media.contentDigest = digest_;
                // The cache key is deliberately dropped rather than kept: it
                // described the old file's timestamp, and a stale one would
                // hand back the old file's waveform for the new file.
                media.contentHash.clear();
            }
        }
    }

private:
    model::MediaRefId media_;
    std::string path_;
    std::string digest_;
};

class ImportMediaCommand final : public ProjectCommand {
public:
    ImportMediaCommand(model::MediaRef media, std::string description)
        : ProjectCommand{std::move(description)}, media_{std::move(media)} {}

protected:
    void mutate(Project& project) override { project.addMedia(media_); }

private:
    model::MediaRef media_;
};

class RemoveMediaCommand final : public ProjectCommand {
public:
    RemoveMediaCommand(model::MediaRefId media, std::string description)
        : ProjectCommand{std::move(description)}, media_{media} {}

protected:
    void mutate(Project& project) override {
        // Every sequence, not just the open one: a file removed from the bin is
        // removed from the project, and a clip of it left in a sequence nobody
        // happens to be looking at is the copy that surfaces at export.
        //
        // By id rather than by reference, because the project deliberately
        // hands out no mutable list of sequences -- editing one goes through a
        // command, which is what this is.
        std::vector<model::SequenceId> ids;
        ids.reserve(project.sequences().size());
        for (const Sequence& sequence : project.sequences()) {
            ids.push_back(sequence.id());
        }

        for (const model::SequenceId id : ids) {
            Sequence& sequence = *project.findSequence(id);
            for (const model::TrackKind kind : {model::TrackKind::Video, model::TrackKind::Audio}) {
                for (Track& track : sequence.tracksMutable(kind)) {
                    std::vector<Clip> kept;
                    kept.reserve(track.clips().size());
                    for (const Clip& clip : track.clips()) {
                        if (clip.source != media_) {
                            kept.push_back(clip);
                        }
                    }
                    if (kept.size() != track.clips().size()) {
                        track.setClips(std::move(kept));
                    }
                }
            }
            // A fade on a clip that has just gone is a fade over nothing. The
            // sequence commands get this from LambdaCommand; this one is a
            // project command and has to say so itself.
            detail::dropOrphanTransitions(sequence);
        }
        static_cast<void>(project.removeMedia(media_));
    }

private:
    model::MediaRefId media_;
};

}  // namespace

Result<CommandPtr> makeImportMedia(Project& project, model::MediaRef media) {
    if (media.path.empty()) {
        return Error{ErrorCode::InvalidData, "media needs a path"};
    }
    if (!media.id.isValid()) {
        media.id = project.ids().next<model::MediaRefTag>();
    }
    if (project.findMedia(media.id) != nullptr) {
        return Error{ErrorCode::InvalidData, "that media id is already in the project"};
    }
    const std::string name = media.name.empty() ? media.path : media.name;
    return CommandPtr{std::make_unique<ImportMediaCommand>(std::move(media), "Import " + name)};
}

int clipsUsingMedia(const Project& project, model::MediaRefId media) {
    int count = 0;
    for (const Sequence& sequence : project.sequences()) {
        for (const auto* list : {&sequence.videoTracks(), &sequence.audioTracks()}) {
            for (const Track& track : *list) {
                for (const Clip& clip : track.clips()) {
                    if (clip.source == media) {
                        ++count;
                    }
                }
            }
        }
    }
    return count;
}

Result<CommandPtr> makeRemoveMedia(Project& project, model::MediaRefId mediaId) {
    const model::MediaRef* media = project.findMedia(mediaId);
    if (media == nullptr) {
        return Error{ErrorCode::NotFound, "no such media in this project"};
    }
    const std::string name = media->name.empty() ? media->path : media->name;
    return CommandPtr{std::make_unique<RemoveMediaCommand>(mediaId, "Remove " + name)};
}

Result<CommandPtr> makeSetMediaNotes(Project& project, model::MediaRefId mediaId,
                                     const std::string& notes) {
    const model::MediaRef* media = project.findMedia(mediaId);
    if (media == nullptr) {
        return Error{ErrorCode::NotFound, "no such media in this project"};
    }
    // Merged by media id, so typing a note is one undo step rather than one
    // per keystroke.
    return CommandPtr{std::make_unique<MediaNotesCommand>(
        mediaId, notes, "Note on " + (media->name.empty() ? media->path : media->name))};
}

Result<CommandPtr> makeSetMediaGrade(Project& project, model::MediaRefId mediaId,
                                     const model::ColorCorrection& color,
                                     const model::ColorWheels& wheels, const model::LutRef& lut) {
    const model::MediaRef* media = project.findMedia(mediaId);
    if (media == nullptr) {
        return Error{ErrorCode::NotFound, "no such media in this project"};
    }
    for (const double value :
         {color.temperature, color.tint, color.exposure, color.contrast, color.saturation}) {
        if (!std::isfinite(value)) {
            return Error{ErrorCode::InvalidData, "a colour correction has to be real numbers"};
        }
    }
    for (const double value :
         {wheels.slopeR, wheels.slopeG, wheels.slopeB, wheels.offsetR, wheels.offsetG,
          wheels.offsetB, wheels.powerR, wheels.powerG, wheels.powerB}) {
        if (!std::isfinite(value)) {
            return Error{ErrorCode::InvalidData, "a colour wheel has to be real numbers"};
        }
    }
    if (!std::isfinite(lut.amount)) {
        return Error{ErrorCode::InvalidData, "a LUT amount has to be a real number"};
    }
    return CommandPtr{std::make_unique<MediaGradeCommand>(
        mediaId, color, wheels, lut, "Grade " + (media->name.empty() ? media->path : media->name))};
}

Result<CommandPtr> makeRelinkMedia(Project& project, model::MediaRefId mediaId,
                                   const std::string& path) {
    const model::MediaRef* media = project.findMedia(mediaId);
    if (media == nullptr) {
        return Error{ErrorCode::NotFound, "no such media in this project"};
    }
    if (path.empty()) {
        return Error{ErrorCode::InvalidData, "a relink needs a file to point at"};
    }
    std::string digest;
    if (auto found = media::contentDigest(path)) {
        digest = *found;
    } else {
        return found.error();
    }
    return CommandPtr{std::make_unique<RelinkMediaCommand>(
        mediaId, path, std::move(digest),
        "Relink " + std::filesystem::path{path}.filename().string())};
}

Result<CommandPtr> makeSetTrackState(Project& project, model::SequenceId sequenceId,
                                     TrackId trackId, const TrackState& state) {
    const Sequence* sequence = project.findSequence(sequenceId);
    if (sequence == nullptr) {
        return Error{ErrorCode::NotFound, "no such sequence"};
    }
    if (sequence->findTrack(trackId) == nullptr) {
        return Error{ErrorCode::NotFound, "no such track in this sequence"};
    }
    if (!std::isfinite(state.gainDb) || !std::isfinite(state.pan)) {
        return Error{ErrorCode::InvalidData, "gain and pan have to be real numbers"};
    }
    const double pan = std::clamp(state.pan, -1.0, 1.0);
    // Keyed by track, so a fader drag is one undo step and the strip next to it
    // is a separate one.
    return makeCommand(sequenceId, "Adjust track", "track:" + std::to_string(trackId.value()),
                       [trackId, state, pan](Sequence& seq) {
                           Track* track = seq.findTrack(trackId);
                           if (track == nullptr) {
                               return;
                           }
                           track->setMuted(state.muted);
                           track->setSoloed(state.soloed);
                           track->setGainDb(state.gainDb);
                           track->setPan(pan);
                       });
}

Result<CommandPtr> makeSetTrackLock(Project& project, model::SequenceId sequenceId, TrackId trackId,
                                    bool locked, bool syncLocked) {
    const Sequence* sequence = project.findSequence(sequenceId);
    if (sequence == nullptr) {
        return Error{ErrorCode::NotFound, "no such sequence"};
    }
    if (sequence->findTrack(trackId) == nullptr) {
        return Error{ErrorCode::NotFound, "no such track in this sequence"};
    }
    // Not merged with anything: a lock is a deliberate press, and coalescing it
    // with the press before would make one undo give back two decisions.
    return makeCommand(sequenceId, locked ? "Lock track" : "Unlock track", {},
                       [trackId, locked, syncLocked](Sequence& seq) {
                           Track* track = seq.findTrack(trackId);
                           if (track == nullptr) {
                               return;
                           }
                           track->setLocked(locked);
                           track->setSyncLocked(syncLocked);
                       });
}

Result<CommandPtr> makeSetTrackProcessing(Project& project, model::SequenceId sequenceId,
                                          TrackId trackId, const model::AudioEq& eq,
                                          const model::Compressor& compressor) {
    const Sequence* sequence = project.findSequence(sequenceId);
    if (sequence == nullptr || sequence->findTrack(trackId) == nullptr) {
        return Error{ErrorCode::NotFound, "no such track in this sequence"};
    }
    for (const double value :
         {eq.highPassHz, eq.lowPassHz, eq.peakHz, eq.peakGainDb, eq.peakQ, compressor.thresholdDb,
          compressor.ratio, compressor.attackMs, compressor.releaseMs, compressor.makeupDb}) {
        if (!std::isfinite(value)) {
            return Error{ErrorCode::InvalidData, "processing settings have to be real numbers"};
        }
    }
    return makeCommand(sequenceId, "Adjust processing",
                       "processing:" + std::to_string(trackId.value()),
                       [trackId, eq, compressor](Sequence& seq) {
                           Track* track = seq.findTrack(trackId);
                           if (track != nullptr) {
                               track->setEq(eq);
                               track->setCompressor(compressor);
                           }
                       });
}

Result<CommandPtr> makeRenameTrack(Project& project, model::SequenceId sequenceId, TrackId trackId,
                                   std::string name) {
    const Sequence* sequence = project.findSequence(sequenceId);
    if (sequence == nullptr) {
        return Error{ErrorCode::NotFound, "no such sequence"};
    }
    if (sequence->findTrack(trackId) == nullptr) {
        return Error{ErrorCode::NotFound, "no such track in this sequence"};
    }
    // Keyed by track, so renaming two tracks in a row is two undo steps rather
    // than one that puts both back.
    return makeCommand(sequenceId, "Rename track", "rename:" + std::to_string(trackId.value()),
                       [trackId, name = std::move(name)](Sequence& seq) {
                           Track* track = seq.findTrack(trackId);
                           if (track == nullptr) {
                               return;
                           }
                           track->setName(name);
                       });
}

Result<CommandPtr> makeAddTrack(Project& project, model::SequenceId sequenceId,
                                model::TrackKind kind, std::string name) {
    if (project.findSequence(sequenceId) == nullptr) {
        return Error{ErrorCode::NotFound, "no such sequence"};
    }
    const TrackId id = project.ids().next<model::TrackTag>();
    return makeCommand(sequenceId, "Add " + std::string{model::toString(kind)} + " track", {},
                       [id, kind, name = std::move(name)](Sequence& sequence) {
                           sequence.addTrack(id, kind, name);
                       });
}

Result<CommandPtr> makeRazorAt(Project& project, const EditTarget& target,
                               const std::vector<time::RationalTime>& points) {
    auto located = locate(project, target);
    if (!located) {
        return located.error();
    }
    const time::Rational rate = located->sequence->frameRate();

    // Sorted and de-duplicated here rather than trusted: cutting the same
    // instant twice would make a clip of no length, and cutting out of order
    // would split pieces that the earlier cuts had already replaced.
    std::vector<RationalTime> cuts;
    cuts.reserve(points.size());
    for (const RationalTime& point : points) {
        cuts.push_back(atRate(point, rate));
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

    // Ids for the tails, handed out now so that applying and re-applying an
    // undone command produces the same clips rather than new ones each time.
    std::vector<std::pair<RationalTime, ClipId>> planned;
    for (const RationalTime& cut : cuts) {
        const Clip* clip = located->track->clipAt(cut);
        if (clip == nullptr || clip->start() == cut) {
            continue;
        }
        planned.emplace_back(cut, project.ids().next<model::ClipTag>());
    }
    if (planned.empty()) {
        return Error{ErrorCode::InvalidData, "none of those points is inside a clip"};
    }

    const TrackId trackId = target.track;
    return makeCommand(target.sequence, "Cut at scene changes", {},
                       [planned, trackId](Sequence& sequence) {
                           Track* track = sequence.findTrack(trackId);
                           ZARO_CHECK(track != nullptr, "track vanished between build and apply");
                           for (const auto& [cut, tailId] : planned) {
                               const Clip* found = track->clipAt(cut);
                               if (found == nullptr || found->start() == cut) {
                                   continue;
                               }
                               const Clip original = *found;
                               std::vector<Clip> rebuilt;
                               for (const Clip& existing : track->clips()) {
                                   if (existing.id != original.id) {
                                       rebuilt.push_back(existing);
                                       continue;
                                   }
                                   rebuilt.push_back(trimmedOut(original, cut));
                                   Clip tail = trimmedIn(original, cut);
                                   tail.id = tailId;
                                   rebuilt.push_back(std::move(tail));
                               }
                               track->setClips(std::move(rebuilt));
                           }
                       });
}

Result<CommandPtr> makeSetSequenceOutput(Project& project, model::SequenceId sequenceId,
                                         const model::Sequence::Output& output) {
    if (project.findSequence(sequenceId) == nullptr) {
        return Error{ErrorCode::NotFound, "no such sequence"};
    }
    if (!std::isfinite(output.highlightKnee) || output.highlightKnee <= 0.0) {
        return Error{ErrorCode::InvalidData, "the highlight knee has to be a positive number"};
    }
    if (output.transfer == media::TransferFunction::Unknown) {
        // There is no formula for it, so nothing could encode through it. The
        // same refusal the input side makes, for the same reason.
        return Error{ErrorCode::InvalidData, "that curve has no formula here"};
    }
    return makeCommand(sequenceId, "Set delivery", "delivery:" + std::to_string(sequenceId.value()),
                       [output](Sequence& sequence) { sequence.setOutput(output); });
}

Result<CommandPtr> makeConformSequence(Project& project, model::SequenceId sequenceId,
                                       const time::Rational& frameRate, std::int32_t width,
                                       std::int32_t height) {
    const Sequence* sequence = project.findSequence(sequenceId);
    if (sequence == nullptr) {
        return Error{ErrorCode::NotFound, "no such sequence"};
    }
    if (frameRate.num() <= 0 || frameRate.den() <= 0) {
        return Error{ErrorCode::InvalidData, "a sequence needs a real frame rate"};
    }
    if (width <= 0 || height <= 0) {
        return Error{ErrorCode::InvalidData, "a sequence needs a frame size"};
    }
    for (const auto* list : {&sequence->videoTracks(), &sequence->audioTracks()}) {
        for (const Track& track : *list) {
            if (!track.isEmpty()) {
                return Error{ErrorCode::InvalidData,
                             "that sequence already has clips on it, and changing its rate would "
                             "retime them"};
            }
        }
    }

    return makeCommand(sequenceId, "Conform sequence", {},
                       [frameRate, width, height](Sequence& target) {
                           target.setFrameRate(frameRate);
                           target.setSize(width, height);
                       });
}

Result<CommandPtr> makeResizeSequence(Project& project, model::SequenceId sequenceId,
                                      std::int32_t width, std::int32_t height) {
    const Sequence* sequence = project.findSequence(sequenceId);
    if (sequence == nullptr) {
        return Error{ErrorCode::NotFound, "no such sequence"};
    }
    if (width <= 0 || height <= 0) {
        return Error{ErrorCode::InvalidData, "a sequence needs a frame size"};
    }
    // Even dimensions: the encoders this ships with reject odd ones for the
    // subsampled formats, and a resize that only fails at export is a resize
    // that fails after the work is done.
    if ((width % 2) != 0 || (height % 2) != 0) {
        return Error{ErrorCode::InvalidData, "a frame size has to be even on both sides"};
    }
    if (sequence->width() == width && sequence->height() == height) {
        return Error{ErrorCode::InvalidData, "that is already the frame size"};
    }
    return makeCommand(sequenceId, "Resize sequence", {},
                       [width, height](Sequence& target) { target.setSize(width, height); });
}

Result<CommandPtr> makeRemoveTrack(Project& project, model::SequenceId sequenceId,
                                   TrackId trackId) {
    const Sequence* sequence = project.findSequence(sequenceId);
    if (sequence == nullptr) {
        return Error{ErrorCode::NotFound, "no such sequence"};
    }
    const model::Track* track = sequence->findTrack(trackId);
    if (track == nullptr) {
        return Error{ErrorCode::NotFound, "no such track"};
    }
    if (track->isLocked()) {
        return Error{ErrorCode::Unsupported, "track '" + track->name() + "' is locked"};
    }
    return makeCommand(sequenceId, "Delete track", {},
                       [trackId](Sequence& s) { s.removeTrack(trackId); });
}

}  // namespace zaro::edit
