// What the grade is done *between*: the transform footage arrives through and
// the curve the sequence is delivered as.
//
// Three combo boxes, their contents, and the rule that writing them from the
// model must not read as somebody choosing something. That was three widget
// members, a `syncingColorManagement_` guard and 110 lines of list-building on
// PreviewWindow, which is the largest class in the program and was not made
// larger by any of it: none of this is about the window.
//
// **It reports choices and makes none.** Turning "this .cube, on this file"
// into a command on the stack needs the project, the selection, the render
// cache and four panels to redraw, and all of those are the window's. So this
// says what was picked and stops there -- which is also what keeps the list
// building testable without a project to write into.
//
// Under the node strip rather than in the shared toolbar: these are
// colour-room questions, and both were previously reachable only from a menu
// somebody had to remember was there.
#pragma once

#include <QString>
#include <QWidget>

#include "zaro/core/model/Project.h"
#include "zaro/core/model/Sequence.h"

class QComboBox;

namespace zaro::app {

class ColorManagement : public QWidget {
    Q_OBJECT

public:
    explicit ColorManagement(QWidget* parent = nullptr);

    /// Show what the sequence and the selected file say.
    ///
    /// Written from the model every time rather than remembered: after an undo
    /// the project is the only thing that knows what these should say.
    ///
    /// `media` is the file under the selection, or null when nothing that reads
    /// a file is selected -- which is what greys the LUT box, because a LUT
    /// belongs to a file and there is no file to put one on. `lutFolder` is the
    /// folder of looks currently open, whose `.cube` files fill the list.
    void showState(const model::Sequence* sequence, const model::MediaRef* media,
                   const QString& lutFolder);

signals:
    /// A .cube was chosen for the selected file. Empty path means None.
    ///
    /// Not emitted while `showState` is writing the boxes, and not emitted for
    /// a path the file already has.
    void inputLutChosen(const QString& path);

    /// A delivery curve or highlight rolloff was chosen for the sequence.
    ///
    /// Carries the whole `Output` rather than the one field that changed: the
    /// two boxes write to one value, and a caller that had to merge them would
    /// be the second place that knows how.
    void deliveryChosen(const model::Sequence::Output& output);

private:
    /// True while the boxes are being written from the model, so those writes
    /// do not read as somebody choosing something.
    bool syncing_{false};
    /// What the sequence said when the boxes were last filled, so a choice can
    /// be compared against it without asking the caller for the sequence again.
    model::Sequence::Output shown_{};
    /// The LUT the selected file already has, for the same reason.
    QString shownLut_;

    QComboBox* inputLut_{nullptr};
    QComboBox* colorSpace_{nullptr};
    QComboBox* toneMapping_{nullptr};
};

}  // namespace zaro::app
