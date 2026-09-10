#pragma once

#include <string>
#include <vector>

#include "zaro/core/media/MediaInfo.h"
#include "zaro/core/model/ColorCorrection.h"
#include "zaro/core/model/Ids.h"
#include "zaro/core/model/Sequence.h"

namespace zaro::model {

/// A named range of a media reference.
///
/// **Not a new kind of media.** A subclip records where somebody said the good
/// part is; placing one makes an ordinary clip whose source range starts inside
/// it, and from there the renderer, the media source, the cache and every edit
/// operation carry on knowing nothing about subclips at all. Making it a media
/// reference with an offset would mean every read had to be translated, in a
/// layer that currently resolves a path and nothing else.
///
/// **So a clip made from one can be trimmed past its edges.** Premiere can
/// restrict those trims; doing that here would need a second kind of clip that
/// every trim, ripple, roll, slip and slide had to learn about, to enforce a
/// boundary somebody chose as a note to themselves. The subclip stays in the
/// bin as that note, and the cut is not constrained by it.
struct Subclip {
    SubclipId id;
    MediaRefId source;
    /// In the source's own time.
    time::TimeRange range;
    std::string name;

    friend bool operator==(const Subclip&, const Subclip&) = default;
};

/// A file on disk that clips point at.
///
/// The project references media; it never contains it. `contentDigest` is what
/// makes relinking possible when a path changes, and what tells a proxy from
/// the thing it stands in for.
struct MediaRef {
    MediaRefId id;
    std::string path;

    /// A smaller copy of the same material, for editing.
    ///
    /// Attached rather than generated: making one is a transcode, and a
    /// transcode belongs to whatever tool the footage came out of. What matters
    /// here is that the two files describe the same thing -- same duration,
    /// same rate -- so that swapping between them moves no edit. A proxy of a
    /// different length would silently retime the cut.
    std::string proxyPath;
    /// A cache key: changes when the file is touched, which is what a cache
    /// wants and what a relink must not have.
    std::string contentHash;
    /// An identity that survives being copied or restored, for finding this
    /// file again when it has moved. See `media::contentDigest`.
    std::string contentDigest;

    /// Whatever somebody wants to say about this file: which camera, which
    /// take, whether it is any good.
    ///
    /// One free-text field rather than a schema of named fields. A schema is a
    /// guess about what a production tracks, and productions that track
    /// something it did not think of end up putting it in a "notes" field
    /// anyway -- which is this one, without the two places to look.
    std::string notes;
    std::string name;
    media::MediaInfo info;

    /// What this footage's curve really is, when the file is wrong about it.
    ///
    /// Camera log is almost never tagged: a container has a number for BT.709
    /// and no number for S-Log3, so an S-Log3 file says BT.709 and decodes to a
    /// washed-out picture that grades badly and looks, at a glance, like
    /// footage somebody underexposed. There is no way to detect it from the
    /// pixels -- a flat shot and a log shot are the same picture -- so the only
    /// honest mechanism is somebody saying so.
    ///
    /// On the media rather than the clip: it is a fact about the file, and
    /// every clip that reads that file needs the same answer. `Unknown` means
    /// believe the container.
    media::TransferFunction transferOverride{media::TransferFunction::Unknown};

    /// What this footage's gamut really is, when the file is wrong about it.
    ///
    /// The same problem as the curve and the same only-honest answer, one step
    /// less common: a container that carries no tag at all is read as BT.709,
    /// and phone footage that is really Display P3 is then composited as though
    /// its red were Rec.709's -- undersaturated, and disagreeing with anything
    /// beside it that *was* tagged.
    ///
    /// Unlike the curve, this one is sometimes detectable from the container
    /// and often simply absent rather than wrong. `Unknown` means believe it.
    media::ColorPrimaries primariesOverride{media::ColorPrimaries::Unknown};

    /// A grade on the file itself, which every clip that reads it inherits.
    ///
    /// **The same footage is usually graded once, not once per cut of it.** A
    /// take that appears four times in the timeline is one lighting setup with
    /// one answer to "what should this look like", and grading it four times
    /// means four chances to get a different answer -- then re-grading all four
    /// when the take is recut. Putting the answer on the file is what makes
    /// that a single edit.
    ///
    /// It does not replace the grade on the clip; it sits under it. A clip's
    /// own correction is applied on top of this one, so a shot can be balanced
    /// once at the source and then pushed warmer for the one scene that wants
    /// it, without the two edits fighting over the same numbers. That is the
    /// difference the Color workspace calls a source grade and an instance
    /// grade, and it is why both exist.
    ///
    /// Composed at render time rather than folded together here: white balance
    /// is a channel multiply and contrast is a power about middle grey, and
    /// those two do not commute, so there is no single correction that means
    /// "this one then that one". See `render::gradePixel`.
    ColorCorrection color;
    /// The source grade's wheels, applied with `color`. An ASC CDL, as on a
    /// clip -- see `ColorWheels`.
    ColorWheels wheels;

    /// The LUT this footage is read through, before anything else.
    ///
    /// An input transform rather than a look: the .cube that gets a camera's
    /// log onto a curve a grade can be built on. It belongs to the file for the
    /// same reason `transferOverride` does -- it answers a question about what
    /// the file is, and every clip reading it needs the same answer.
    LutRef lut;

    /// Whether this file carries a source grade worth applying.
    [[nodiscard]] bool isGraded() const noexcept {
        return !color.isIdentity() || !wheels.isIdentity() || lut.isSet();
    }

    /// The curve to decode this file through: the override if there is one,
    /// otherwise whatever the container said.
    [[nodiscard]] media::TransferFunction transfer() const {
        if (transferOverride != media::TransferFunction::Unknown) {
            return transferOverride;
        }
        const media::VideoStreamInfo* video = info.primaryVideo();
        return video != nullptr ? video->color.transfer : media::TransferFunction::Unknown;
    }

    /// The gamut to read this file as: the override if there is one, otherwise
    /// whatever the container said.
    [[nodiscard]] media::ColorPrimaries primaries() const {
        if (primariesOverride != media::ColorPrimaries::Unknown) {
            return primariesOverride;
        }
        const media::VideoStreamInfo* video = info.primaryVideo();
        return video != nullptr ? video->color.primaries : media::ColorPrimaries::Unknown;
    }

    friend bool operator==(const MediaRef& a, const MediaRef& b) {
        // MediaInfo is a cache of what the file said, not part of identity: two
        // refs to the same file are the same ref even if one has not been
        // probed yet.
        return a.id == b.id && a.path == b.path && a.contentHash == b.contentHash &&
               a.notes == b.notes && a.contentDigest == b.contentDigest && a.name == b.name &&
               a.transferOverride == b.transferOverride &&
               a.primariesOverride == b.primariesOverride && a.color == b.color &&
               a.wheels == b.wheels && a.lut == b.lut;
    }
};

class Project {
public:
    /// Whether to read proxies where a clip's media has one.
    ///
    /// A property of the project rather than of a clip: nobody wants some shots
    /// on proxies and some not, and the whole reason to be on proxies is that
    /// the machine cannot keep up with the originals.
    ///
    /// **Export ignores this.** Delivering the small copies because somebody
    /// left a toggle on is a mistake with no warning attached and no way back
    /// once the file has gone out.
    [[nodiscard]] bool usingProxies() const noexcept { return useProxies_; }
    void setUsingProxies(bool value) noexcept { useProxies_ = value; }

    /// The file to read for this media: its proxy when one is attached and
    /// proxies are on, its own path otherwise.
    [[nodiscard]] const std::string& resolvedPath(const MediaRef& media) const {
        return useProxies_ && !media.proxyPath.empty() ? media.proxyPath : media.path;
    }

    Project() = default;

    [[nodiscard]] const std::vector<MediaRef>& media() const noexcept { return media_; }
    [[nodiscard]] const std::vector<Subclip>& subclips() const noexcept { return subclips_; }
    [[nodiscard]] const Subclip* findSubclip(SubclipId id) const;
    /// Added directly rather than through a command, like media: the bin is not
    /// the cut, and nothing about a subclip changes what any sequence renders.
    SubclipId addSubclip(Subclip subclip);
    bool removeSubclip(SubclipId id);
    /// Whether putting `inner` inside `outer` would make a cycle.
    ///
    /// A sequence containing itself, directly or through any chain of nests, is
    /// not a picture -- it is a render that never finishes. This is checked
    /// before the edit rather than guarded against during the render, because a
    /// depth limit turns an impossible project into a merely wrong one, and the
    /// person who made it gets no explanation either way.
    [[nodiscard]] bool nestingWouldCycle(SequenceId outer, SequenceId inner) const;

    /// Mutable access, for the few things that change a media reference in
    /// place -- attaching a proxy, relinking a moved file. Not for editing:
    /// anything that changes the cut goes through a command.
    [[nodiscard]] std::vector<MediaRef>& mediaMutable() noexcept { return media_; }
    [[nodiscard]] const std::vector<Sequence>& sequences() const noexcept { return sequences_; }

    [[nodiscard]] const MediaRef* findMedia(MediaRefId id) const;
    [[nodiscard]] Sequence* findSequence(SequenceId id);
    [[nodiscard]] const Sequence* findSequence(SequenceId id) const;

    MediaRefId addMedia(MediaRef ref);
    /// Take a file out of the bin, with any subclips somebody marked on it.
    ///
    /// The subclips go with it because a subclip is a note about where the good
    /// part of a *file* is, and a note about a file the project no longer has
    /// is a row in the bin pointing at nothing. Clips on a sequence are not
    /// touched here -- that is an edit, and edits go through a command; see
    /// `edit::makeRemoveMedia`, which is what a caller should be using.
    ///
    /// Returns whether there was such a reference.
    bool removeMedia(MediaRefId id);
    SequenceId addSequence(Sequence sequence);

    [[nodiscard]] IdGenerator& ids() noexcept { return ids_; }
    [[nodiscard]] const IdGenerator& ids() const noexcept { return ids_; }

    [[nodiscard]] SequenceId activeSequence() const noexcept { return activeSequence_; }
    void setActiveSequence(SequenceId id) noexcept { activeSequence_ = id; }

    /// Fields a loader needs to restore verbatim.
    void setMedia(std::vector<MediaRef> value) { media_ = std::move(value); }
    void setSequences(std::vector<Sequence> value) { sequences_ = std::move(value); }

    friend bool operator==(const Project& a, const Project& b) {
        // The id counter is bookkeeping, not content: a project loaded from
        // disk and one built in memory are equal if they describe the same cut.
        return a.media_ == b.media_ && a.subclips_ == b.subclips_ && a.sequences_ == b.sequences_ &&
               a.activeSequence_ == b.activeSequence_;
    }

private:
    std::vector<MediaRef> media_;
    std::vector<Subclip> subclips_;
    bool useProxies_{false};
    std::vector<Sequence> sequences_;
    SequenceId activeSequence_;
    IdGenerator ids_;
};

/// A project with one empty sequence, ready to be edited into.
///
/// One sequence rather than none: a window with nothing to show has to special
/// case every panel, and "no sequence" is a state somebody can only leave by
/// making one anyway.
///
/// The rate and size are a placeholder, and are meant to be replaced by the
/// first thing put on the timeline -- see `edit::makeConformSequence`. Asking
/// somebody to choose a format before they have opened any footage is asking a
/// question whose answer is in the footage.
[[nodiscard]] Project newProject(const std::string& sequenceName = "Sequence 01");

}  // namespace zaro::model
