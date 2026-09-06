#pragma once

#include <optional>
#include <string>
#include <vector>

#include "zaro/core/model/AudioProcessing.h"
#include "zaro/core/model/Clip.h"
#include "zaro/core/model/Transition.h"

namespace zaro::model {

enum class TrackKind { Video, Audio };

[[nodiscard]] const char* toString(TrackKind kind) noexcept;

/// An ordered lane of non-overlapping clips.
///
/// Two invariants hold at all times, and every mutator asserts them:
///
///   * clips are sorted by timeline start
///   * no two clips overlap
///
/// Gaps are implicit -- the space between one clip's exclusive end and the
/// next clip's start. The alternative, storing explicit gap items, makes ripple
/// operations a list splice but makes the far more common question "what is
/// playing at time T" a linear walk instead of a binary search. The compositor
/// asks that question for every track on every frame, so the sparse form wins.
class Track {
public:
    Track() = default;
    Track(TrackId id, TrackKind kind, std::string name)
        : id_{id}, kind_{kind}, name_{std::move(name)} {}

    [[nodiscard]] TrackId id() const noexcept { return id_; }
    [[nodiscard]] TrackKind kind() const noexcept { return kind_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void setName(std::string value) { name_ = std::move(value); }

    [[nodiscard]] bool isMuted() const noexcept { return muted_; }
    void setMuted(bool value) noexcept { muted_ = value; }
    /// Solo is not the opposite of mute.
    ///
    /// Muting a track silences that track. Soloing one silences every track
    /// that is *not* soloed, which is a property of the sequence rather than of
    /// the track — so whether a soloed track is audible depends on nothing, and
    /// whether an ordinary track is audible depends on whether anything else is
    /// soloed. `Sequence::isAudible` is where that rule lives; a track cannot
    /// answer it alone.
    [[nodiscard]] bool isSoloed() const noexcept { return soloed_; }
    void setSoloed(bool value) noexcept { soloed_ = value; }

    [[nodiscard]] bool isLocked() const noexcept { return locked_; }
    void setLocked(bool value) noexcept { locked_ = value; }

    /// Whether this track follows ripple edits made on other tracks.
    ///
    /// Distinct from being locked. A locked track refuses all editing; a track
    /// with sync lock off can still be edited directly but stays put when
    /// something else ripples -- which is how a music bed or a lower third is
    /// kept from sliding every time the picture is trimmed.
    [[nodiscard]] bool isSyncLocked() const noexcept { return syncLocked_; }
    void setSyncLocked(bool value) noexcept { syncLocked_ = value; }

    /// Track gain in decibels and pan from -1 to +1, applied after clip gain.
    /// The track's processing chain. Applied to the summed clips, before the
    /// fader — which is where an insert goes on every console ever built, and
    /// means pulling the fader down turns down what the compressor did rather
    /// than changing what it does.
    [[nodiscard]] const AudioEq& eq() const noexcept { return eq_; }
    void setEq(const AudioEq& value) { eq_ = value; }
    [[nodiscard]] const Compressor& compressor() const noexcept { return compressor_; }
    void setCompressor(const Compressor& value) { compressor_ = value; }

    [[nodiscard]] double gainDb() const noexcept { return gainDb_; }
    void setGainDb(double value) noexcept { gainDb_ = value; }
    [[nodiscard]] double pan() const noexcept { return pan_; }
    void setPan(double value) noexcept { pan_ = value; }

    [[nodiscard]] const std::vector<Clip>& clips() const noexcept { return clips_; }
    [[nodiscard]] const std::vector<Transition>& transitions() const noexcept {
        return transitions_;
    }

    /// The transition covering `t`, if any.
    [[nodiscard]] const Transition* transitionAt(const time::RationalTime& t) const;
    [[nodiscard]] const Transition* findTransition(TransitionId id) const;

    void setTransitions(std::vector<Transition> transitions);

    /// Drop every transition whose clips are no longer on this track.
    ///
    /// A transition is a span *between* two clips rather than a thing sitting
    /// on one, so nothing about removing a clip makes the span go away by
    /// itself -- and a dissolve left behind still draws over the cut and still
    /// asks the renderer for frames of a clip that is gone. Deleting a shot
    /// under a fade left exactly that: a fade nobody could select, on nothing.
    ///
    /// Called by the track itself from `remove` and `setClips`, which is every
    /// path a clip can leave a track by. Doing it in the operations instead
    /// meant remembering it in six places, and the seventh was always the one
    /// somebody reported.
    ///
    /// An invalid id on one side is left alone: that is what a fade in or a
    /// fade out is, and only the side that names a clip has to still be there.
    /// A span naming nothing on either side is dropped -- it has no cut to
    /// straddle and nothing to blend.
    /// Returns how many were dropped.
    std::size_t pruneOrphanTransitions();

    [[nodiscard]] bool isEmpty() const noexcept { return clips_.empty(); }

    /// The clip playing at `t`, or nullptr in a gap. Binary search.
    [[nodiscard]] const Clip* clipAt(const time::RationalTime& t) const;

    [[nodiscard]] const Clip* find(ClipId id) const;
    [[nodiscard]] Clip* find(ClipId id);
    [[nodiscard]] std::optional<std::size_t> indexOf(ClipId id) const;

    /// Every clip whose timeline range intersects `range`, in order.
    [[nodiscard]] std::vector<const Clip*> clipsIn(const time::TimeRange& range) const;

    /// First clip starting at or after `t`.
    [[nodiscard]] std::optional<std::size_t> firstIndexAtOrAfter(const time::RationalTime& t) const;

    /// Start of the first clip to end of the last. Empty for an empty track.
    [[nodiscard]] time::TimeRange extent() const;

    /// True when `range` is free of clips, ignoring `ignoring` if given.
    [[nodiscard]] bool isRangeFree(const time::TimeRange& range, ClipId ignoring = ClipId{}) const;

    // --- Mutators. Each keeps the sorted, non-overlapping invariant, and trips
    // --- a check rather than silently producing a corrupt track.

    /// Insert into free space. The caller must have cleared the range first;
    /// this refuses to overwrite rather than choosing a victim for you.
    void insert(Clip clip);

    Clip remove(ClipId id);

    /// Replace a clip in place. The new extent must still be free.
    void replace(ClipId id, Clip clip);

    /// Whether shiftFrom would leave the track valid. A negative delta can
    /// drive the shifted clips into the ones before `from`, and an operation
    /// that would do that has to be refused, not asserted on.
    [[nodiscard]] bool canShiftFrom(const time::RationalTime& from,
                                    const time::RationalTime& delta) const;

    /// Shift every clip starting at or after `from` by `delta`. This is ripple.
    void shiftFrom(const time::RationalTime& from, const time::RationalTime& delta);

    /// Direct access for commands that restore a whole track from a snapshot.
    void setClips(std::vector<Clip> clips);

    friend bool operator==(const Track&, const Track&) = default;

private:
    void checkInvariants() const;

    TrackId id_;
    TrackKind kind_{TrackKind::Video};
    std::string name_;
    std::vector<Clip> clips_;
    std::vector<Transition> transitions_;
    bool muted_{false};
    bool locked_{false};
    /// On by default: tracks following a ripple is the behaviour that keeps a
    /// cut in sync, and it should have to be turned off deliberately.
    bool syncLocked_{true};
    bool soloed_{false};
    AudioEq eq_;
    Compressor compressor_;
    double gainDb_{0.0};
    double pan_{0.0};
};

}  // namespace zaro::model
