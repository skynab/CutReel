#include "ColorPalette.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QListView>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "zaro/core/edit/Operations.h"
#include "zaro/core/model/ColorCorrection.h"

#include "ColorWheel.h"
#include "Icons.h"
#include "Theme.h"

namespace zaro::app {
namespace {

// How far a puck at the rim moves each knob. Ranges that make a full deflection
// a strong grade rather than a broken picture: a slope of 1.5 is a bright shot,
// a slope of 4 is a white frame.
constexpr double kOffsetRange = 0.20;
constexpr double kPowerRange = 0.50;
constexpr double kSlopeRange = 0.50;

/// A balance and a master, as three channel deviations.
///
/// The three are 120 degrees apart and sum to zero, so moving the puck changes
/// the colour of a knob without changing its level -- which is what makes a
/// wheel feel like a wheel. The master is what changes the level, and it is
/// added to all three equally.
struct Deviation {
    double r;
    double g;
    double b;
};

Deviation deviationOf(double x, double y, double master) {
    constexpr double kSin120 = 0.8660254037844386;
    return Deviation{x + master, (-0.5 * x) + (kSin120 * y) + master,
                     (-0.5 * x) - (kSin120 * y) + master};
}

/// The inverse: read a puck position back out of three channel values.
///
/// Needed because the panel is driven from the model rather than from its own
/// widgets -- after an undo, the wheels have to be able to show what the clip
/// now says, and that means turning three numbers back into a point.
void balanceOf(const Deviation& deviation, double& x, double& y, double& master) {
    constexpr double kSqrt3 = 1.7320508075688772;
    master = (deviation.r + deviation.g + deviation.b) / 3.0;
    const double r = deviation.r - master;
    const double g = deviation.g - master;
    const double b = deviation.b - master;
    x = r;
    y = (g - b) / kSqrt3;
}

/// 0..1 across a range, and back. The sliders are all fractions; what the
/// fraction means is written once, here.
double toFraction(double value, double low, double high) {
    return std::clamp((value - low) / (high - low), 0.0, 1.0);
}
double fromFraction(double fraction, double low, double high) {
    return low + (fraction * (high - low));
}

}  // namespace

// --- GradientSlider -------------------------------------------------------

GradientSlider::GradientSlider(QString label, QColor from, QColor middle, QColor to,
                               QWidget* parent)
    : QWidget{parent}, label_{std::move(label)}, from_{from}, middle_{middle}, to_{to} {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setToolTip(label_ + " — double-click to reset");
}

QSize GradientSlider::sizeHint() const {
    return QSize{190, 24};
}

QRect GradientSlider::trackRect() const {
    return QRect{0, height() - 6, width(), 4};
}

void GradientSlider::setFraction(double fraction) {
    fraction_ = std::clamp(fraction, 0.0, 1.0);
    update();
}

void GradientSlider::setReadout(const QString& text) {
    readout_ = text;
    update();
}

void GradientSlider::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing, true);

    QFont label = font();
    label.setPointSizeF(8.0);
    painter.setFont(label);
    painter.setPen(theme::textAt(0.55));
    const QRect row{0, 0, width(), height() - 9};
    painter.drawText(row, Qt::AlignLeft | Qt::AlignVCenter, label_);

    QFont readout{QStringLiteral("Menlo")};
    readout.setStyleHint(QFont::Monospace);
    readout.setPointSizeF(7.5);
    painter.setFont(readout);
    painter.drawText(row, Qt::AlignRight | Qt::AlignVCenter, readout_);

    const QRect track = trackRect();
    QLinearGradient ramp{QPointF{static_cast<double>(track.left()), 0.0},
                         QPointF{static_cast<double>(track.right()), 0.0}};
    ramp.setColorAt(0.0, from_);
    if (middle_.isValid()) {
        ramp.setColorAt(0.5, middle_);
    }
    ramp.setColorAt(1.0, to_);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ramp);
    painter.drawRoundedRect(track, 2, 2);

    const double at = track.left() + (fraction_ * track.width());
    painter.setBrush(theme::text());
    painter.setPen(QPen{QColor{0, 0, 0, 150}, 1.0});
    painter.drawEllipse(QPointF{at, track.center().y() + 0.5}, 5.0, 5.0);
}

void GradientSlider::take(const QPoint& where) {
    const QRect track = trackRect();
    setFraction(track.width() > 0 ? static_cast<double>(where.x() - track.left()) / track.width()
                                  : 0.0);
}

void GradientSlider::mousePressEvent(QMouseEvent* event) {
    dragging_ = true;
    take(event->pos());
    emit changed(false);
}

void GradientSlider::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) {
        return;
    }
    take(event->pos());
    emit changed(false);
}

void GradientSlider::mouseReleaseEvent(QMouseEvent* /*event*/) {
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    emit changed(true);
}

void GradientSlider::mouseDoubleClickEvent(QMouseEvent* /*event*/) {
    setFraction(neutral_);
    emit changed(true);
}

// --- ColorPalette ---------------------------------------------------------

ColorPalette::ColorPalette(QWidget* parent) : QWidget{parent} {
    setObjectName("color-palette");
    setAttribute(Qt::WA_StyledBackground, true);

    // The palette list. Two entries rather than the design's five: Wheels and
    // Bars are two ways of typing the same ASC CDL, which this project has, and
    // Log, Curves and Blur name controls it does not -- a log wheel is a
    // different parameterisation, and a blur is not a colour decision.
    palettes_ = new QListWidget(this);
    palettes_->setObjectName("palette-list");
    palettes_->setFrameShape(QFrame::NoFrame);
    // A row across the top rather than a column down the side. In a 310px
    // column a vertical list would eat half the width the wheels need, and
    // two entries do not want a column of their own.
    palettes_->setFlow(QListView::LeftToRight);
    palettes_->setFixedHeight(30);
    palettes_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    palettes_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (const auto& [name, glyph] : {std::pair{QStringLiteral("Wheels"), icons::Glyph::Circle},
                                      std::pair{QStringLiteral("Bars"), icons::Glyph::Rows}}) {
        auto* item = new QListWidgetItem(name, palettes_);
        item->setIcon(icons::toolIcon(glyph, 14));
    }
    palettes_->setCurrentRow(0);

    // --- wheels ---------------------------------------------------------
    auto* wheelRow = new QWidget(this);
    // Two across rather than three along: the palette lives in a column now,
    // and a 104px disc three abreast needs more width than the column has.
    auto* wheelLayout = new QGridLayout(wheelRow);
    wheelLayout->setContentsMargins(12, 8, 12, 8);
    wheelLayout->setHorizontalSpacing(14);
    wheelLayout->setVerticalSpacing(14);
    static constexpr const char* kWheelNames[] = {"Lift", "Gamma", "Gain"};
    for (std::size_t at = 0; at < wheels_.size(); ++at) {
        wheels_[at] = new ColorWheel{QString::fromUtf8(kWheelNames[at]), wheelRow};
        wheels_[at]->setObjectName(QString("wheel-%1").arg(QString::fromUtf8(kWheelNames[at])));
        wheelLayout->addWidget(wheels_[at], static_cast<int>(at) / 2, static_cast<int>(at) % 2);
        connect(wheels_[at], &ColorWheel::changed, this,
                [this](bool committed) { pushWheels(committed); });
    }
    wheelLayout->setRowStretch(2, 1);

    // --- bars: the same nine numbers, typed one channel at a time --------
    auto* barRow = new QWidget(this);
    auto* barLayout = new QVBoxLayout(barRow);
    barLayout->setContentsMargins(12, 6, 12, 6);
    barLayout->setSpacing(10);
    static constexpr const char* kChannels[] = {"R", "G", "B"};
    for (int knob = 0; knob < 3; ++knob) {
        auto* column = new QWidget(barRow);
        auto* columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(2);
        auto* heading = new QLabel(QString::fromUtf8(kWheelNames[knob]), column);
        heading->setProperty("muted", true);
        columnLayout->addWidget(heading);
        for (int channel = 0; channel < 3; ++channel) {
            // The track is the channel it moves, so a column of three reads as
            // red, green, blue without three more labels to say so.
            static const QColor kInk[] = {QColor{0xd9, 0x6a, 0x6a}, QColor{0x7f, 0xd9, 0x8f},
                                          QColor{0x7f, 0x8f, 0xd9}};
            auto* bar = new GradientSlider{QString::fromUtf8(kChannels[channel]),
                                           theme::neutral(800), QColor{}, kInk[channel], column};
            bar->setMinimumWidth(120);
            bars_.push_back(bar);
            columnLayout->addWidget(bar);
            connect(bar, &GradientSlider::changed, this,
                    [this](bool committed) { pushWheels(committed); });
        }
        barLayout->addWidget(column);
    }
    barLayout->addStretch(1);

    pages_ = new QStackedWidget(this);
    pages_->addWidget(wheelRow);
    pages_->addWidget(barRow);
    connect(palettes_, &QListWidget::currentRowChanged, this, [this](int row) {
        pages_->setCurrentIndex(std::max(0, row));
        // Whether the ramps and the empty message belong on screen depends on
        // which page is showing, so the panel has to be read again.
        refresh();
    });

    // --- temperature, tint, saturation -----------------------------------
    temperature_ = new GradientSlider{"Temperature", QColor{0x6a, 0x8f, 0xd9}, theme::neutral(600),
                                      QColor{0xd9, 0xa8, 0x6a}, this};
    tint_ = new GradientSlider{"Tint", QColor{0x7f, 0xd9, 0x8f}, theme::neutral(600),
                               QColor{0xd9, 0x6a, 0xc7}, this};
    saturation_ = new GradientSlider{"Saturation", theme::neutral(700), QColor{},
                                     QColor{0xd9, 0x6a, 0xc7}, this};
    // Saturation's neutral is a third of the way along, because the scale runs
    // 0..200 with 100 untouched -- not the middle of the track.
    saturation_->setNeutral(0.5);
    for (GradientSlider* slider : {temperature_, tint_, saturation_}) {
        slider->setMinimumWidth(120);
        connect(slider, &GradientSlider::changed, this,
                [this](bool committed) { pushCorrection(committed); });
    }
    auto* ramps = new QWidget(this);
    auto* rampColumn = new QVBoxLayout(ramps);
    rampColumn->setContentsMargins(12, 8, 12, 8);
    rampColumn->setSpacing(8);
    rampColumn->addWidget(temperature_);
    rampColumn->addWidget(tint_);
    rampColumn->addWidget(saturation_);

    // The strip above, not the timeline: the Color workspace hides the timeline
    // entirely -- see `PreviewWindow::setWorkspace` -- so this was telling
    // somebody to use a panel that is not on the screen while they read it.
    empty_ = new QLabel("Select a shot in the strip above to grade it", this);
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setProperty("muted", true);

    // The controls stack down a column, inside a scroller: three wheels, nine
    // bars and three ramps do not fit a short pane, and a panel that clips its
    // last control silently is worse than one that scrolls.
    auto* scrolled = new QWidget(this);
    auto* scrolledColumn = new QVBoxLayout(scrolled);
    scrolledColumn->setContentsMargins(0, 0, 0, 0);
    scrolledColumn->setSpacing(0);
    scrolledColumn->addWidget(pages_);
    scrolledColumn->addWidget(ramps);
    scrolledColumn->addStretch(1);

    scroller_ = new QScrollArea(this);
    scroller_->setObjectName("palette-scroll");
    scroller_->setWidget(scrolled);
    scroller_->setWidgetResizable(true);
    scroller_->setFrameShape(QFrame::NoFrame);
    scroller_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ramps_ = ramps;
    // Everything built so far is a grading control. Anything after this came
    // through `addPage` and lives by different rules.
    gradingPages_ = pages_->count();

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(palettes_);
    column->addWidget(scroller_, 1);
    column->addWidget(empty_, 1);

    refresh();
}

bool ColorPalette::onGradingPage() const {
    return pages_->currentIndex() < gradingPages_;
}

void ColorPalette::addPage(const QString& name, icons::Glyph glyph, QWidget* page) {
    // Into the same stack and the same tab strip as the wheels, so the panel
    // has one row of tabs rather than one row per thing that wanted a tab.
    pages_->addWidget(page);
    auto* item = new QListWidgetItem(name, palettes_);
    item->setIcon(icons::toolIcon(glyph, 14));
    refresh();
}

void ColorPalette::bind(const ui::SequenceBinding& binding) {
    project_ = binding.project;
    sequenceId_ = binding.sequence;
    commands_ = binding.commands;
    refresh();
}

void ColorPalette::setSelection(model::TrackId track, model::ClipId clip) {
    track_ = track;
    clip_ = clip;
    refresh();
}

const model::Clip* ColorPalette::selectedClip() const {
    if (project_ == nullptr || !clip_.isValid()) {
        return nullptr;
    }
    const model::Sequence* sequence = project_->findSequence(sequenceId_);
    if (sequence == nullptr) {
        return nullptr;
    }
    const model::Track* track = sequence->findTrack(track_);
    return track != nullptr ? track->find(clip_) : nullptr;
}

const model::MediaRef* ColorPalette::selectedMedia() const {
    if (project_ == nullptr) {
        return nullptr;
    }
    // An explicit choice wins: the media pool can point this panel at a file
    // that no clip on screen happens to be sitting on.
    if (media_.isValid()) {
        return project_->findMedia(media_);
    }
    const model::Clip* clip = selectedClip();
    if (clip == nullptr || !clip->activeSource().isValid()) {
        return nullptr;
    }
    return project_->findMedia(clip->activeSource());
}

bool ColorPalette::haveTarget() const {
    return target_ == GradeTarget::MediaFile ? selectedMedia() != nullptr
                                             : selectedClip() != nullptr;
}

model::ColorCorrection ColorPalette::currentColor() const {
    if (target_ == GradeTarget::MediaFile) {
        const model::MediaRef* media = selectedMedia();
        return media != nullptr ? media->color : model::ColorCorrection{};
    }
    const model::Clip* clip = selectedClip();
    return clip != nullptr ? clip->color : model::ColorCorrection{};
}

model::ColorWheels ColorPalette::currentWheels() const {
    if (target_ == GradeTarget::MediaFile) {
        const model::MediaRef* media = selectedMedia();
        return media != nullptr ? media->wheels : model::ColorWheels{};
    }
    const model::Clip* clip = selectedClip();
    return clip != nullptr ? clip->wheels : model::ColorWheels{};
}

void ColorPalette::setTarget(GradeTarget target) {
    if (target_ == target) {
        return;
    }
    target_ = target;
    refresh();
}

void ColorPalette::setMedia(model::MediaRefId media) {
    if (media_ == media) {
        return;
    }
    media_ = media;
    if (target_ == GradeTarget::MediaFile) {
        refresh();
    }
}

void ColorPalette::push(Result<edit::CommandPtr> built, bool committed) {
    if (!built) {
        return;
    }
    commands_->execute(*project_, std::move(*built));
    if (committed) {
        // One undo step per gesture: the merge is broken when the hand comes
        // off, not on every pixel of the drag.
        commands_->breakMerge();
    }
    refresh();
    emit edited();
}

void ColorPalette::refresh() {
    const bool have = haveTarget();
    const bool grading = onGradingPage();
    // The tabs never go away: the Gallery and the LUTs are reachable with
    // nothing selected, and a strip that vanished would take them with it.
    palettes_->setVisible(true);
    // The ramps belong to the grade, so they follow the grading pages.
    ramps_->setVisible(grading);
    // Only the grading pages need something selected. A page that came through
    // `addPage` is shown whatever the timeline is doing.
    const bool blocked = grading && !have;
    scroller_->setVisible(!blocked);
    empty_->setVisible(blocked);
    if (blocked) {
        // Which pane to look in depends on what is being graded, and telling
        // somebody to use the strip while the bin is on screen is worse than
        // saying nothing.
        empty_->setText(target_ == GradeTarget::MediaFile
                            ? tr("Open a file in the bin to grade every instance of it")
                            : tr("Select a shot in the strip above to grade it"));
        return;
    }
    if (!grading) {
        return;  // nothing on this page reads a grade
    }

    // Written from the model, not remembered: after an undo the project is the
    // only thing that knows what the grade is.
    updating_ = true;
    const model::ColorWheels wheels = currentWheels();
    const Deviation knobs[] = {
        {wheels.offsetR, wheels.offsetG, wheels.offsetB},
        {wheels.powerR - 1.0, wheels.powerG - 1.0, wheels.powerB - 1.0},
        {wheels.slopeR - 1.0, wheels.slopeG - 1.0, wheels.slopeB - 1.0},
    };
    const double ranges[] = {kOffsetRange, kPowerRange, kSlopeRange};
    for (std::size_t at = 0; at < wheels_.size(); ++at) {
        const Deviation scaled{knobs[at].r / ranges[at], knobs[at].g / ranges[at],
                               knobs[at].b / ranges[at]};
        double x = 0.0;
        double y = 0.0;
        double master = 0.0;
        balanceOf(scaled, x, y, master);
        wheels_[at]->setBalance(x, y);
        wheels_[at]->setMaster(master);
        for (int channel = 0; channel < 3; ++channel) {
            const double value = channel == 0 ? scaled.r : (channel == 1 ? scaled.g : scaled.b);
            bars_[(at * 3) + static_cast<std::size_t>(channel)]->setFraction(
                toFraction(value, -1.0, 1.0));
            bars_[(at * 3) + static_cast<std::size_t>(channel)]->setReadout(
                QString::number(value, 'f', 2));
        }
    }

    const model::ColorCorrection colour = currentColor();
    temperature_->setFraction(toFraction(colour.temperature, -100.0, 100.0));
    temperature_->setReadout(QString::number(colour.temperature, 'f', 0));
    tint_->setFraction(toFraction(colour.tint, -100.0, 100.0));
    tint_->setReadout(QString::number(colour.tint, 'f', 0));
    saturation_->setFraction(toFraction(colour.saturation, 0.0, 200.0));
    saturation_->setReadout(QString::number(colour.saturation, 'f', 0));
    updating_ = false;
}

void ColorPalette::pushWheels(bool committed) {
    if (updating_ || commands_ == nullptr || project_ == nullptr || !haveTarget()) {
        return;
    }
    // Which control moved decides which reading is authoritative: the wheels
    // and the bars are the same nine numbers, and reading the page that is not
    // on screen would throw away what somebody just did.
    const bool fromBars = pages_->currentIndex() == 1;
    model::ColorWheels wheels;
    double* const channels[3][3] = {
        {&wheels.offsetR, &wheels.offsetG, &wheels.offsetB},
        {&wheels.powerR, &wheels.powerG, &wheels.powerB},
        {&wheels.slopeR, &wheels.slopeG, &wheels.slopeB},
    };
    const double ranges[] = {kOffsetRange, kPowerRange, kSlopeRange};
    const double neutral[] = {0.0, 1.0, 1.0};
    for (std::size_t knob = 0; knob < 3; ++knob) {
        Deviation scaled{0.0, 0.0, 0.0};
        if (fromBars) {
            scaled.r = fromFraction(bars_[knob * 3]->fraction(), -1.0, 1.0);
            scaled.g = fromFraction(bars_[(knob * 3) + 1]->fraction(), -1.0, 1.0);
            scaled.b = fromFraction(bars_[(knob * 3) + 2]->fraction(), -1.0, 1.0);
        } else {
            scaled = deviationOf(wheels_[knob]->balanceX(), wheels_[knob]->balanceY(),
                                 wheels_[knob]->master());
        }
        // A power or a slope of zero is a black frame that no further move can
        // recover from, so the floor is small rather than nothing.
        const double floor = knob == 0 ? -1.0 : 0.01;
        *channels[knob][0] = std::max(floor, neutral[knob] + (scaled.r * ranges[knob]));
        *channels[knob][1] = std::max(floor, neutral[knob] + (scaled.g * ranges[knob]));
        *channels[knob][2] = std::max(floor, neutral[knob] + (scaled.b * ranges[knob]));
    }

    if (target_ == GradeTarget::MediaFile) {
        const model::MediaRef* media = selectedMedia();
        if (media == nullptr) {
            return;
        }
        push(edit::makeSetMediaGrade(*project_, media->id, media->color, wheels, media->lut),
             committed);
        return;
    }
    push(edit::makeSetWheels(*project_, {sequenceId_, track_}, clip_, wheels), committed);
}

void ColorPalette::pushCorrection(bool committed) {
    if (updating_ || commands_ == nullptr || project_ == nullptr || !haveTarget()) {
        return;
    }
    // Exposure and contrast are not on this bar, so they are carried through
    // rather than defaulted -- a temperature drag must not silently flatten an
    // exposure somebody set elsewhere.
    model::ColorCorrection colour = currentColor();
    colour.temperature = fromFraction(temperature_->fraction(), -100.0, 100.0);
    colour.tint = fromFraction(tint_->fraction(), -100.0, 100.0);
    colour.saturation = fromFraction(saturation_->fraction(), 0.0, 200.0);

    if (target_ == GradeTarget::MediaFile) {
        const model::MediaRef* media = selectedMedia();
        if (media == nullptr) {
            return;
        }
        push(edit::makeSetMediaGrade(*project_, media->id, colour, media->wheels, media->lut),
             committed);
        return;
    }
    push(edit::makeSetColorCorrection(*project_, {sequenceId_, track_}, clip_, colour), committed);
}

void ColorPalette::resetGrade() {
    if (commands_ == nullptr || project_ == nullptr || !haveTarget()) {
        return;
    }
    // Reset clears the grade somebody is looking at, not both of them: a file
    // balanced last week should survive somebody resetting the shot in front of
    // them, and the other way round.
    if (target_ == GradeTarget::MediaFile) {
        const model::MediaRef* media = selectedMedia();
        if (media == nullptr) {
            return;
        }
        if (auto built = edit::makeSetMediaGrade(*project_, media->id, model::ColorCorrection{},
                                                 model::ColorWheels{}, media->lut)) {
            commands_->execute(*project_, std::move(*built));
        }
    } else {
        if (auto built = edit::makeSetWheels(*project_, {sequenceId_, track_}, clip_,
                                             model::ColorWheels{})) {
            commands_->execute(*project_, std::move(*built));
        }
        if (auto built = edit::makeSetColorCorrection(*project_, {sequenceId_, track_}, clip_,
                                                      model::ColorCorrection{})) {
            commands_->execute(*project_, std::move(*built));
        }
    }
    commands_->breakMerge();
    refresh();
    emit edited();
}

}  // namespace zaro::app
