// What the operation files share.
//
// Operations.cpp was three thousand lines and ninety-odd make* functions in
// one flat namespace. It is now six files, split by what the operation does to
// a sequence, and this is the handful of helpers more than one of them needs:
// find the track, refuse a locked one, trim a clip's edges, name a command.
//
// Internal to core/src/edit -- not under include/, and in a `detail` namespace,
// because none of it is API. They were in an anonymous namespace when there was
// one file to be anonymous in; the split is the only reason they have names a
// linker can see.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "zaro/core/edit/Command.h"
#include "zaro/core/edit/Operations.h"
#include "zaro/core/model/Clip.h"
#include "zaro/core/model/Project.h"
#include "zaro/core/model/Sequence.h"
#include "zaro/core/time/RationalTime.h"

namespace zaro::edit::detail {

using model::Clip;
using model::ClipId;
using model::Project;
using model::Sequence;
using model::Track;
using model::TrackId;
using time::RationalTime;
using time::TimeRange;

/// A command whose edit is supplied as a lambda. The operations below differ
/// only in what they do to the sequence, so they share one command type rather
/// than each defining a class that would carry no extra state.
class LambdaCommand final : public SequenceCommand {
public:
    using Body = std::function<void(Sequence&)>;

    LambdaCommand(model::SequenceId sequence, std::string description, std::string mergeKey,
                  Body body)
        : SequenceCommand{sequence, std::move(description), std::move(mergeKey)},
          body_{std::move(body)} {}

protected:
    void mutate(Sequence& sequence) override;

private:
    Body body_;
};

/// Drop every transition on the sequence whose clips have gone.
///
/// Run after *every* edit rather than at the end of the ones that remove
/// clips, and that is the whole point: a transition is a span between two
/// clips, so it does not go away when one of them does, and a dissolve or a
/// fade left over a clip that has been deleted still draws across the cut and
/// still asks for frames of something that is not there. Deleting a sound clip
/// with a fade on it left exactly that.
///
/// Eleven operations can remove a clip -- directly, or through `clearRange`
/// cutting a hole for something being dropped on top -- and remembering this in
/// each of them is remembering it in eleven places. Here it is remembered in
/// one. It costs a null check per track for the tracks that carry no spans,
/// which is nearly all of them.
void dropOrphanTransitions(Sequence& sequence);

/// A command whose edit is a lambda; see the class above.
CommandPtr makeCommand(model::SequenceId sequence, std::string description, std::string mergeKey,
                       LambdaCommand::Body body);

/// A clip id as it appears in a merge key.
std::string idText(ClipId id);

struct Located {
    Sequence* sequence;
    Track* track;
};

/// The sequence and track an edit is aimed at, or why it cannot be found.
Result<Located> locate(Project& project, const EditTarget& target);

/// The clip, or an error naming it. Every operation starts this way.
Result<const Clip*> requireClip(const Track& track, ClipId id);

/// The clip with one edge moved, source range following the timeline range.
Clip trimmedIn(const Clip& clip, const RationalTime& newStart);
Clip trimmedOut(const Clip& clip, const RationalTime& newEnd);

/// Refuses a clip that would read past the end of what its media holds.
Status checkSourceFits(const Project& project, const Clip& clip);

/// The same instant, expressed at another rate.
RationalTime atRate(const RationalTime& t, const time::Rational& rate);

/// Every clip linked to this one, the clip itself included.
std::vector<std::pair<TrackId, ClipId>> linkedGroup(Sequence& sequence, TrackId trackId,
                                                    ClipId clipId);

/// Shared shape of every property change: find the clip, refuse if the track is
/// locked, and rewrite one field in place.
Result<CommandPtr> modifyClip(Project& project, const EditTarget& target, ClipId clipId,
                              std::string description, std::string mergeKey,
                              std::function<void(Clip&)> change);

/// The clip, or why not. Refuses a locked track, the same as every other
/// operation.
Result<const Clip*> lookupClip(Project& project, const EditTarget& target, ClipId id);

}  // namespace zaro::edit::detail
