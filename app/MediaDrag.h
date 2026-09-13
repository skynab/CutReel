// What a drag out of the media pane carries, and how the timeline reads it.
//
// One file so the two ends of the gesture cannot disagree about the format.
// The bin writes it and the timeline reads it; nothing else needs to know how
// it is spelled.
#pragma once

#include <QStringList>
#include <optional>
#include <string>

#include "zaro/core/model/Ids.h"

class QMimeData;

namespace zaro::app {

/// A file being dragged out of the bin: which media, and which part of it.
///
/// Ids rather than a path, because both ends of this drag are inside one
/// project. The timeline has to end up with a clip pointing at the media
/// reference the bin already holds; a path would mean importing the file a
/// second time to get one, and the cut would then be against a different entry
/// than the bin is showing.
struct MediaDrag {
    model::MediaRefId media;
    /// Invalid unless the row dragged was a subclip, in which case the clip
    /// takes the subclip's range instead of the whole file.
    model::SubclipId subclip;

    /// Set instead of `media` when what is being dragged is a title.
    ///
    /// A title has no media to point at -- it generates its picture -- so the
    /// drag carries which preset it came from and the timeline builds the
    /// graphic on landing. One payload for both because it is one gesture: the
    /// pane hands something over, the timeline decides where it goes, and only
    /// the last step differs.
    std::string titlePreset;

    [[nodiscard]] bool isTitle() const noexcept { return !titlePreset.empty(); }
    [[nodiscard]] bool isSomething() const noexcept { return media.isValid() || isTitle(); }
};

/// How a media drag spells itself on the clipboard.
[[nodiscard]] const char* mediaDragMimeType();

/// The payload for one. The caller owns it until Qt takes the drag over.
[[nodiscard]] QMimeData* encodeMediaDrag(const MediaDrag& dragged);

/// What a drag carries, if it is one of ours and names something.
[[nodiscard]] std::optional<MediaDrag> decodeMediaDrag(const QMimeData* mime);

/// The media files a drag out of the file manager carries, if it carries any.
///
/// By extension, the way the media browser lists by extension: the answer is
/// needed while the pointer is still moving, and opening every file under the
/// cursor to be sure is not something that can happen at that speed. A folder
/// counts, because a folder of rushes is the usual thing to let go of.
///
/// Shared by the bin and the timeline, which both take footage from the file
/// manager and must agree to the letter about what counts as some -- a drag
/// the timeline offers to take and the bin then refuses to import is a drop
/// that appears to do nothing.
[[nodiscard]] QStringList droppedMediaPaths(const QMimeData* mime);

/// The project file a drag out of the file manager carries, if that is what it
/// is carrying. Empty otherwise.
///
/// Here rather than in the window, because the window is not where the drop
/// lands. Qt hands a drop to the widget under the pointer that accepts drops
/// and stops there -- a drag event a child refuses does not fall through to
/// its parent -- so the bin and the timeline, which cover nearly the whole
/// window, each have to recognise a project and pass it up. One reader so
/// that they cannot come to disagree about what counts.
///
/// **One file, and only a project.** A drag of several is somebody importing
/// rushes, and a project among them would replace the window they were
/// importing into: an answer to a question nobody asked, and the surest way
/// to lose the last few minutes of work. A folder is likewise an import.
[[nodiscard]] std::string droppedProjectPath(const QMimeData* mime);

}  // namespace zaro::app
