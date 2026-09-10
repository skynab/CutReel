#pragma once

#include <QString>
#include <QWidget>
#include <array>
#include <cstdint>
#include <vector>

#include "zaro/core/Error.h"
#include "zaro/core/edit/CommandStack.h"
#include "zaro/core/model/Project.h"

class QLabel;
class QListWidget;
class QStackedWidget;

#include "zaro/ui/SequenceBinding.h"

namespace zaro::app {

class ColorWheel;

/// A slider whose track is the thing it changes.
///
/// Temperature runs blue to orange, tint green to magenta, saturation grey to
/// colour. Drawn that way rather than as a bar with a number beside it: the
/// question a colourist asks of one of these is "which way is warmer", and a
/// track that answers it is worth more than a label that has to be read.
class GradientSlider : public QWidget {
    Q_OBJECT

public:
    /// `from`, `middle` and `to` paint the track. A middle of an invalid colour
    /// makes it a two-stop ramp, which is what saturation wants.
    GradientSlider(QString label, QColor from, QColor middle, QColor to, QWidget* parent = nullptr);

    /// The position, 0..1. What that maps to is the owner's business.
    void setFraction(double fraction);
    [[nodiscard]] double fraction() const noexcept { return fraction_; }
    /// The number shown at the right of the label row.
    void setReadout(const QString& text);
    /// Where a double-click puts it back to.
    void setNeutral(double fraction) { neutral_ = fraction; }

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void changed(bool committed);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] QRect trackRect() const;
    void take(const QPoint& where);

    QString label_;
    QString readout_;
    QColor from_;
    QColor middle_;
    QColor to_;
    double fraction_{0.5};
    double neutral_{0.5};
    bool dragging_{false};
};

/// The grading palette along the bottom of the Color workspace.
///
/// Three wheels and three ramps, which between them are the whole of a primary
/// grade: what the picture's black, midtone and white do, and where its white
/// balance and saturation sit. Everything here writes through the command
/// stack, so a drag is one undo step and nothing can hold a value the stack has
/// never seen -- the panel re-reads the clip after every push.
class ColorPalette : public QWidget, public ui::SequenceBound {
    Q_OBJECT

public:
    /// Which of the two grades these wheels are driving.
    ///
    /// The same controls serve both, deliberately: a colourist balancing a shot
    /// reaches for the same wheels whether the answer is going to live on this
    /// cut of it or on the file every cut reads. What changes is where the edit
    /// lands, and that is a choice made once at the top of the room rather than
    /// a second set of wheels to learn.
    enum class GradeTarget : std::uint8_t {
        /// This instance only. Other uses of the same file are untouched.
        TimelineClip,
        /// The file itself, under every clip that reads it.
        MediaFile,
    };

    explicit ColorPalette(QWidget* parent = nullptr);

    void bind(const ui::SequenceBinding& binding) override;
    /// Whose grade is being shown. An invalid clip empties the panel.
    void setSelection(model::TrackId track, model::ClipId clip);
    /// Grade the instance or the file. Re-reads, so the wheels move to whatever
    /// the newly-chosen target already says.
    void setTarget(GradeTarget target);
    [[nodiscard]] GradeTarget target() const noexcept { return target_; }
    /// The file being graded when the target is `MediaFile`. Invalid means
    /// "whatever the selected clip reads", which is the usual case.
    void setMedia(model::MediaRefId media);
    [[nodiscard]] model::MediaRefId media() const noexcept { return media_; }
    /// Re-read the clip. Called after an edit from anywhere, and after undo.
    void refresh();

    /// Put this clip's primary grade back to neutral. Public because Reset is
    /// also a button in the node panel, and both should be the same action.
    void resetGrade();

signals:
    /// The grade changed, so the monitor and the scopes must be redrawn.
    void edited();

private:
    [[nodiscard]] const model::Clip* selectedClip() const;
    /// The file whose grade is being edited: the one explicitly chosen, else
    /// the one the selected clip reads. Null when neither resolves.
    [[nodiscard]] const model::MediaRef* selectedMedia() const;
    /// Whether there is anything for the wheels to drive at all.
    [[nodiscard]] bool haveTarget() const;
    /// The grade currently on the target, whichever it is.
    [[nodiscard]] model::ColorCorrection currentColor() const;
    [[nodiscard]] model::ColorWheels currentWheels() const;
    void pushWheels(bool committed);
    void pushCorrection(bool committed);
    /// Send one edit to the stack, merging while a drag is in flight.
    void push(Result<edit::CommandPtr> built, bool committed);

    model::Project* project_{nullptr};
    model::SequenceId sequenceId_;
    model::TrackId track_;
    model::ClipId clip_;
    model::MediaRefId media_;
    GradeTarget target_{GradeTarget::TimelineClip};
    edit::CommandStack* commands_{nullptr};
    /// True while the widgets are being written from the model, so the writes
    /// do not read as somebody moving them.
    bool updating_{false};

    /// Lift, Gamma, Gain -- the ASC CDL's offset, power and slope.
    std::array<ColorWheel*, 3> wheels_{};
    GradientSlider* temperature_{nullptr};
    GradientSlider* tint_{nullptr};
    GradientSlider* saturation_{nullptr};
    QListWidget* palettes_{nullptr};
    QStackedWidget* pages_{nullptr};
    std::vector<GradientSlider*> bars_;
    QLabel* empty_{nullptr};
};

}  // namespace zaro::app
