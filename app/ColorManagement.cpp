#include "ColorManagement.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QStringList>
#include <QVariant>

#include "zaro/core/media/ColorInfo.h"

#include "chrome/Widgets.h"

namespace zaro::app {

namespace {

/// Where highlights start rolling off, in linear light.
///
/// Named rather than a number box: these are the four answers anybody wants,
/// and "0.8" is not a thing somebody knows they want. Clipping is what this
/// program did before there was a choice, so it stays first.
struct KneePreset {
    const char* name;
    double value;
};
constexpr KneePreset kKneePresets[] = {
    {"Clip", 1.0},
    {"Roll off gently", 0.9},
    {"Roll off", 0.8},
    {"Roll off hard", 0.65},
};

}  // namespace

ColorManagement::ColorManagement(QWidget* parent) : QWidget{parent} {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(1, 1);

    // Named so the GUI tests can reach them. These three had no test at all
    // while they were anonymous members of the window.
    const auto field = [&](int row, const char* name, const QString& text, const QString& tip) {
        auto* label = chrome::mutedLabel(this, text);
        label->setToolTip(tip);
        grid->addWidget(label, row, 0);
        auto* box = new QComboBox(this);
        box->setObjectName(QString::fromLatin1(name));
        box->setToolTip(tip);
        box->setFocusPolicy(Qt::StrongFocus);
        grid->addWidget(box, row, 1);
        return box;
    };

    inputLut_ = field(0, "input-lut", tr("Input LUT"),
                      tr("The .cube this footage is read through, before any grade. "
                         "It belongs to the file, so every clip that reads the file "
                         "gets it. Open a folder of looks in the LUTs list to fill this."));
    colorSpace_ = field(1, "color-space", tr("Color space"),
                        tr("The curve this sequence is delivered as. The scopes and the "
                           "curve editor are drawn against it, so it is also what a grade "
                           "is being judged on."));
    toneMapping_ = field(2, "tone-mapping", tr("Tone mapping"),
                         tr("Where highlights start rolling off, in linear light. "
                            "Clipping is what this program did before there was a choice."));

    connect(inputLut_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (syncing_ || index < 0) {
            return;
        }
        const QString path = inputLut_->itemData(index).toString();
        // A file that already has this LUT is not a change, and emitting one
        // would put an empty step on the undo stack every time the selection
        // moved between two clips reading the same file.
        if (path == shownLut_) {
            return;
        }
        emit inputLutChosen(path);
    });

    const auto delivery = [this] {
        if (syncing_) {
            return;
        }
        model::Sequence::Output wanted = shown_;
        wanted.transfer = static_cast<media::TransferFunction>(colorSpace_->currentData().toInt());
        wanted.highlightKnee = toneMapping_->currentData().toDouble();
        if (wanted == shown_) {
            return;
        }
        emit deliveryChosen(wanted);
    };
    connect(colorSpace_, &QComboBox::currentIndexChanged, this, delivery);
    connect(toneMapping_, &QComboBox::currentIndexChanged, this, delivery);
}

void ColorManagement::showState(const model::Sequence* sequence, const model::MediaRef* media,
                                const QString& lutFolder) {
    syncing_ = true;
    shown_ = sequence != nullptr ? sequence->output() : model::Sequence::Output{};

    colorSpace_->clear();
    int wantedCurve = 0;
    for (const media::TransferFunction transfer : media::allTransferFunctions()) {
        if (transfer == media::TransferFunction::Unknown) {
            continue;  // no formula, so nothing could encode through it
        }
        colorSpace_->addItem(QString::fromUtf8(media::toString(transfer)),
                             QVariant::fromValue(static_cast<int>(transfer)));
        if (sequence != nullptr && sequence->output().transfer == transfer) {
            wantedCurve = colorSpace_->count() - 1;
        }
    }
    colorSpace_->setCurrentIndex(wantedCurve);

    toneMapping_->clear();
    int wantedKnee = 0;
    for (const KneePreset& preset : kKneePresets) {
        toneMapping_->addItem(QString::fromUtf8(preset.name), preset.value);
        if (sequence != nullptr && sequence->output().highlightKnee == preset.value) {
            wantedKnee = toneMapping_->count() - 1;
        }
    }
    toneMapping_->setCurrentIndex(wantedKnee);

    // The input LUT is the file's, so it follows the selection rather than the
    // sequence. "None" is always first, and is what an unset LUT means.
    inputLut_->clear();
    inputLut_->addItem(tr("None"), QString{});
    shownLut_ = media != nullptr ? QString::fromStdString(media->lut.path) : QString{};
    int wantedLut = 0;
    const QDir folder{lutFolder};
    if (folder.exists()) {
        const QStringList cubes = folder.entryList({"*.cube"}, QDir::Files, QDir::Name);
        for (const QString& file : cubes) {
            const QString path = folder.filePath(file);
            inputLut_->addItem(file, path);
            if (path == shownLut_) {
                wantedLut = inputLut_->count() - 1;
            }
        }
    }
    // A LUT set from somewhere else, or from a folder that is no longer open,
    // still has to be shown -- otherwise the panel says "None" over footage
    // that is being transformed.
    if (!shownLut_.isEmpty() && wantedLut == 0) {
        inputLut_->addItem(QFileInfo(shownLut_).fileName(), shownLut_);
        wantedLut = inputLut_->count() - 1;
    }
    inputLut_->setCurrentIndex(wantedLut);
    inputLut_->setEnabled(media != nullptr);

    syncing_ = false;
}

}  // namespace zaro::app
