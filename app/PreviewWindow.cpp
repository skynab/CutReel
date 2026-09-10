// The preview window's construction and its dialogs.
//
// Two things live here, and they are here for the same reason. The constructor
// was four hundred and seventy-seven lines: every panel the window owns, the
// splitters they sit in, and the hundred-odd connections between them, in one
// function that grew by a dozen lines every time a panel was added. It is now
// six, named for the phases it already had -- make the panels, lay out the
// viewer, wire the workspace, lay out the window, wire the editing, start the
// clocks -- in that order, because the order was never arbitrary: a layout
// cannot add a widget that does not exist yet, and a connection cannot name a
// panel that has not been made.
//
// The dialogs are the other half. Each is the same shape -- ask for a path or
// a choice, call into commands:: or edit::, say what happened -- and none of
// them is about the window beyond the fact that a dialog needs a parent. They
// were in the header only because they were methods, and they were methods
// only because that is where the state they read happens to be kept.
//
// What stays in the header is what a caller needs to see: the declarations,
// and the small forwarding methods that are easier to read than to find.

#include "PreviewWindow.h"

#include <QContextMenuEvent>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <zaro/Version.h>

#include "MediaDrag.h"

namespace zaro::app {
namespace {

QString appName() {
    return QString::fromUtf8(kAppName.data(), static_cast<qsizetype>(kAppName.size()));
}

QString versionText() {
    return QString::fromUtf8(kVersion.data(), static_cast<qsizetype>(kVersion.size()));
}

}  // namespace

PreviewWindow::PreviewWindow(model::Project project, io::LoadedProject loaded, std::string path) {
    // What was just loaded is what is on disk, by definition, and adopt
    // marks it so.
    document_.adopt(std::move(project), std::move(loaded), std::move(path));
    sequenceId_ = document_.project().activeSequence();

    createPanels();

    // Everything the window can do lives in a menu. It used to live in a
    // row of eighteen buttons under the picture, which was fine while there
    // were four of them: a menu bar is the structure that says which of
    // them belong together, and it costs the transport nothing.
    // The keymap first: menus read their shortcuts from it as they are
    // built, so a customised binding is on the item the first time it is
    // drawn rather than after a refresh nobody triggers.
    loadKeymap();
    // Every command says what it does before anything shows one: the menus
    // and the bars ask the router for an action by id, and an id nothing is
    // bound to is a menu item that does nothing.
    bindCommands();
    bindPlaybackActions();
    buildMenus();

    buildViewerLayout();
    wireWorkspacePanels();
    buildWindowLayout();
    wireEditingSignals();
    startTimers();

    // Last, so that nothing can be let go of over a window that is still being
    // put together. A project dropped anywhere on it opens, which is the
    // shortest route there is from "this is the cut" to having it on screen.
    setAcceptDrops(true);
}

void PreviewWindow::createPanels() {
    monitor_ = new app::ProgramMonitor(this);
    // Narrow enough that a source and a program fit side by side in a
    // window somebody has not maximised, without the splitter having to
    // take the difference out of the parameter column and clip it. Only the
    // width gives: two monitors sit beside each other, so they share the
    // well's height rather than dividing it, and a monitor too short to
    // judge a frame on is the thing this floor exists to prevent.
    monitor_->setMinimumSize(240, 270);

    // The mask handles live on a transparent widget over the picture, so
    // the renderer never has to know about them and the widget that draws
    // them is the one that receives the clicks.
    maskOverlay_ = new app::MaskOverlay(monitor_, monitor_);
    monitor_->installEventFilter(this);
    connect(maskOverlay_, &app::MaskOverlay::edited, this, [this] {
        renderCache_.clear();
        monitor_->update();
        timeline_->update();
        effects_->refresh();
        updateCacheBar();
        updateTitle();
    });

    // The title's box, on the same terms as the mask handles: a transparent
    // widget over the picture, which is where the answer to "is it in the
    // right place" is. Only one of the two is ever up, since a clip is either
    // a title or something with a mask on it.
    titleOverlay_ = new app::TitleOverlay(monitor_, monitor_);
    connect(titleOverlay_, &app::TitleOverlay::edited, this, [this] {
        renderCache_.clear();
        monitor_->update();
        timeline_->update();
        effects_->refresh();
        updateCacheBar();
        updateTitle();
    });

    // The burn-in: what the frame is, over the frame. Below the mask
    // handles, and transparent to the mouse, so it never takes a click the
    // mask editor wanted.
    viewerOverlay_ = new app::ViewerOverlay(monitor_, monitor_);
    maskOverlay_->raise();
    titleOverlay_->raise();

    bars_.timecode = new QLabel(this);
    bars_.timecode->setObjectName("timecode-big");
    // Ask the system for its fixed-width family rather than naming one:
    // a missing family costs a slow alias lookup and silently falls back.
    QFont monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monospace.setPointSize(17);
    bars_.timecode->setFont(monospace);
    bars_.remaining = new QLabel(this);
    QFont smallMonospace = monospace;
    smallMonospace.setPointSize(11);
    bars_.remaining->setFont(smallMonospace);
    bars_.remaining->setProperty("muted", true);
    bars_.playButton = new QPushButton(kPlayGlyph, this);
    bars_.playButton->setToolTip("Play or pause (Space)");
    bars_.playButton->setProperty("accent", true);
    bars_.playButton->setFixedSize(46, 30);
    bars_.scrubber = new QSlider(Qt::Horizontal, this);

    timeline_ = adopting(new app::TimelineWidget(this));
    thumbnails_ = new app::ThumbnailCache(this);
    timeline_->setThumbnailCache(thumbnails_);
    effects_ = adopting(new app::EffectControls(this));
    connect(effects_, &app::EffectControls::drawMaskRequested, maskOverlay_,
            &app::MaskOverlay::setDrawing);
    connect(maskOverlay_, &app::MaskOverlay::drawingChanged, effects_,
            &app::EffectControls::setDrawingMask);
    connect(effects_, &app::EffectControls::trackMaskRequested, this, [this] { trackMask(); });
    connect(effects_, &app::EffectControls::pinRequested, this, [this] {
        if (auto pinned = pinToClipBelow(); !pinned) {
            app::say(this, "Pin", QString::fromStdString(pinned.error().message()));
        }
    });
    connect(effects_, &app::EffectControls::unpinRequested, this,
            [this] { static_cast<void>(pinTo(model::ClipId{})); });
    connect(effects_, &app::EffectControls::stabiliseRequested, this, [this] { stabilise(); });
    connect(effects_, &app::EffectControls::reframeRequested, this, [this] {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        auto framed = reframeClip();
        QApplication::restoreOverrideCursor();
        if (!framed) {
            app::say(this, "Auto-reframe", QString::fromStdString(framed.error().message()));
            return;
        }
        QString said = QString("Reframed %1 frames, scaled to %2%.")
                           .arg(framed->measured)
                           .arg(framed->scale * 100.0, 0, 'f', 0);
        if (!framed->reason.empty()) {
            said += QString("\n%1").arg(QString::fromStdString(framed->reason));
        }
        app::say(this, "Auto-reframe", said);
    });
    connect(effects_, &app::EffectControls::clearStabilisationRequested, this,
            [this] { clearStabilisation(); });
    scopes_ = new app::ScopesPanel(this);
    mixer_ = adopting(new app::MixerPanel(this));
    // Asked, not guessed.
    //
    // This was 250, then 300, each time to stop the splitter squeezing the
    // column until the value fields ran off the edge of it -- and each time a
    // number chosen by eye rather than measured. The rows want 383, so the cap
    // of 330 cut every field short at every window size: "0.0 p" for "0.0 px",
    // a rotation with its degree sign gone. `EffectControls` measures its own
    // widest row now, so the only thing left to decide here is how much room
    // above that the splitter may give it.
    effects_->setMaximumWidth(effects_->minimumWidth() + 30);

    // Monitor and parameters side by side, transport under them, timeline
    // across the bottom.
    bin_ = adopting(new app::ProjectBin(this));
    // The same frames the timeline draws on its clips, from the same worker:
    // a bin row and a filmstrip cell of one file are the same decode.
    bin_->setThumbnailCache(thumbnails_);
    // The width the design fixes the media pane at. Fixed rather than a
    // range because the row inside it is fixed too -- a 64-pixel thumbnail,
    // two lines of type and a dot -- and the pane has nothing that would
    // use the extra space if it were given any.
    bin_->setFixedWidth(296);

    thumb_ = new app::FrameThumb(this);
    loudness_ = new app::LoudnessPanel(this);
    stems_ = adopting(new app::StemsPanel(this));
    channel_ = adopting(new app::ChannelPanel(this));
    channel_->setMinimumWidth(280);
    channel_->setMaximumWidth(320);

    gallery_ = new app::GalleryPanel(this);
    clipStrip_ = adopting(new app::ClipStrip(this));
    nodes_ = new app::GradeNodes(this);
    palette_ = adopting(new app::ColorPalette(this));

    source_ = new app::SourceMonitor(this);
    // Its own row of buttons -- In, Out, Subclip, Insert, Over -- is what sets
    // this, not the picture: below this width the labels start losing letters,
    // and a button reading "nsert" is worse than a narrow picture.
    //
    // So ask the row rather than guess at it. 280 was 93 pixels under what
    // those buttons measure, which left the splitter free to do the exact
    // thing the paragraph above says must not happen.
    source_->setMinimumWidth(source_->minimumSizeHint().width());
}

void PreviewWindow::buildViewerLayout() {
    // Source and program side by side, each shown or not on its own.
    //
    // Two toggles rather than two tabs, as the design draws them: comparing
    // the clip you are about to place against the cut you are placing it
    // into is the whole reason both monitors exist, and a stack can only
    // ever answer "which one" -- never "both". Either can be off, including
    // both, because a window given over to the timeline is a real way to
    // work and the well is the largest thing to reclaim.
    bars_.viewerWell = new QWidget(this);
    bars_.viewerWell->setObjectName("viewer-well");
    bars_.viewerWell->setAttribute(Qt::WA_StyledBackground, true);
    auto* wellRow = new QHBoxLayout(bars_.viewerWell);
    wellRow->setContentsMargins(12, 12, 12, 12);
    wellRow->setSpacing(12);
    wellRow->addWidget(source_, 1);
    wellRow->addWidget(monitor_, 1);
    // Scopes beside the picture rather than off in the parameter column,
    // which is where the design puts them and where they are read: an
    // instrument is compared against the frame it measures, and a glance
    // that crosses the whole window is a glance nobody takes.
    scopes_->setFixedWidth(330);
    wellRow->addWidget(scopes_);
    bars_.noMonitorLabel = new QLabel("No monitor shown \u2014 turn on Source or Program", this);
    bars_.noMonitorLabel->setAlignment(Qt::AlignCenter);
    bars_.noMonitorLabel->setProperty("muted", true);
    wellRow->addWidget(bars_.noMonitorLabel, 1);

    auto* programColumn = new QWidget(this);
    auto* programLayout = new QVBoxLayout(programColumn);
    programLayout->setContentsMargins(0, 0, 0, 0);
    programLayout->setSpacing(0);
    bars_.viewerBar = buildViewerBar();
    programLayout->addWidget(bars_.viewerBar);
    programLayout->addWidget(bars_.viewerWell, 1);
    // The console takes the centre in Audio, where the picture is in every
    // other workspace: a mix is read across ten channels at once, and a
    // column of strips squeezed into a side panel is a column nobody can
    // find a fader in.
    programLayout->addWidget(mixer_, 1);
    programLayout->addWidget(clipStrip_);
    programLayout->addWidget(buildTransportBar());

    topSplitter_ = new QSplitter(Qt::Horizontal, this);
    topSplitter_->setHandleWidth(1);
    auto* leftColumn = new QWidget(this);
    leftColumn->setObjectName("audio-side");
    leftColumn->setFixedWidth(262);
    auto* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(thumb_);
    leftLayout->addWidget(loudness_);
    leftLayout->addWidget(stems_);
    leftLayout->addStretch(1);
    bars_.audioSide = leftColumn;

    topSplitter_->addWidget(bars_.audioSide);
    // The grading palette is the Color room's left column: the wheels, the
    // bars and the ramps, stacked. It used to run along the bottom, which is
    // where the timeline belongs -- a colourist reads the cut across and the
    // controls down, not the other way about.
    topSplitter_->addWidget(palette_);
    palette_->setFixedWidth(310);
    // The gallery is not a pane of its own: its two panels hang off the
    // palette's tab strip, beside Wheels and Bars, so that everything a
    // colourist reaches for while grading is in one column behind one row of
    // tabs. The panel itself stays alive as the owner of the stills and the
    // look folder.
    palette_->addPage(tr("Gallery"), app::icons::Glyph::Camera, gallery_->stillsPage());
    palette_->addPage(tr("LUTs"), app::icons::Glyph::FolderOpen, gallery_->lutsPage());
    gallery_->hide();
    topSplitter_->addWidget(bin_);
    topSplitter_->addWidget(programColumn);
    // Scopes share the parameter column: they are read while grading, and
    // grading is done with the parameters in reach.
    // The grade chain sits on top of the parameters it navigates, in one
    // column rather than as a splitter pane: the node strip is a fixed
    // 118 pixels and giving it a drag handle would invite somebody to
    // squash a picture that has nothing to gain from being shorter.
    auto* gradeColumn = new QWidget(this);
    auto* gradeLayout = new QVBoxLayout(gradeColumn);
    gradeLayout->setContentsMargins(0, 0, 0, 0);
    gradeLayout->setSpacing(0);
    bars_.nodesBox = new QWidget(gradeColumn);
    bars_.nodesBox->setObjectName("grade-nodes-box");
    auto* nodesLayout = new QVBoxLayout(bars_.nodesBox);
    nodesLayout->setContentsMargins(12, 10, 12, 10);

    // Grading: which of the two grades the wheels are driving. Above the node
    // strip because it is the question that comes first -- a node chain is a
    // chain *on something*, and choosing the shot or the file decides what
    // every control below this line is going to write to.
    auto* targetRow = new QWidget(bars_.nodesBox);
    auto* targetLayout = new QHBoxLayout(targetRow);
    targetLayout->setContentsMargins(0, 0, 0, 6);
    targetLayout->setSpacing(6);
    auto* targetCaption = new QLabel(tr("Grading"), targetRow);
    targetCaption->setObjectName("section-label");
    targetLayout->addWidget(targetCaption);
    auto* targetGroup = new QWidget(targetRow);
    targetGroup->setObjectName("tab-group");
    targetGroup->setFixedHeight(26);
    auto* targetTabs = new QHBoxLayout(targetGroup);
    targetTabs->setContentsMargins(2, 2, 2, 2);
    targetTabs->setSpacing(2);
    gradeClipTab_ = chrome::button(targetGroup, tr("Timeline clip"),
                                   tr("Grade this instance only — other uses of "
                                      "the same file are untouched."),
                                   true);
    gradeMediaTab_ = chrome::button(targetGroup, tr("Media file"),
                                    tr("Grade the source file — every instance in "
                                       "the timeline inherits it."),
                                    true);
    for (QPushButton* tab : {gradeClipTab_, gradeMediaTab_}) {
        tab->setFixedHeight(22);
        targetTabs->addWidget(tab);
    }
    targetLayout->addWidget(targetGroup);
    targetLayout->addStretch(1);
    nodesLayout->addWidget(targetRow);

    // What the choice above resolves to right now, spelled out. "Media file"
    // alone does not say *which* file, and grading the wrong one is not visible
    // until somebody looks at another cut of it.
    gradeTargetLabel_ = new QLabel(bars_.nodesBox);
    gradeTargetLabel_->setObjectName("muted-label");
    gradeTargetLabel_->setTextFormat(Qt::PlainText);
    nodesLayout->addWidget(gradeTargetLabel_);

    connect(gradeClipTab_, &QPushButton::clicked, this,
            [this] { setGradeTarget(app::ColorPalette::GradeTarget::TimelineClip); });
    connect(gradeMediaTab_, &QPushButton::clicked, this,
            [this] { setGradeTarget(app::ColorPalette::GradeTarget::MediaFile); });

    nodesLayout->addWidget(nodes_);

    // Colour management, under the chain rather than in it: these are what the
    // grade is done *between* -- the transform the footage arrives through and
    // the curve it is delivered as -- and both were previously reachable only
    // from a menu somebody had to remember was there.
    auto* managementCaption = new QLabel(tr("Colour management"), bars_.nodesBox);
    managementCaption->setObjectName("section-label");
    nodesLayout->addSpacing(8);
    nodesLayout->addWidget(managementCaption);

    auto* management = new QWidget(bars_.nodesBox);
    auto* managementGrid = new QGridLayout(management);
    managementGrid->setContentsMargins(0, 4, 0, 0);
    managementGrid->setHorizontalSpacing(8);
    managementGrid->setVerticalSpacing(4);
    managementGrid->setColumnStretch(1, 1);
    const auto field = [&](int row, const QString& text, const QString& tip) {
        auto* label = chrome::mutedLabel(management, text);
        label->setToolTip(tip);
        managementGrid->addWidget(label, row, 0);
        auto* box = new QComboBox(management);
        box->setToolTip(tip);
        box->setFocusPolicy(Qt::StrongFocus);
        managementGrid->addWidget(box, row, 1);
        return box;
    };
    inputLut_ = field(0, tr("Input LUT"),
                      tr("The .cube this footage is read through, before any grade. "
                         "It belongs to the file, so every clip that reads the file "
                         "gets it. Open a folder of looks in the LUTs list to fill this."));
    colorSpace_ = field(1, tr("Color space"),
                        tr("The curve this sequence is delivered as. The scopes and the "
                           "curve editor are drawn against it, so it is also what a grade "
                           "is being judged on."));
    toneMapping_ = field(2, tr("Tone mapping"),
                         tr("Where highlights start rolling off, in linear light. "
                            "Clipping is what this program did before there was a choice."));
    nodesLayout->addWidget(management);

    connect(inputLut_, &QComboBox::currentIndexChanged, this,
            [this](int index) { applyInputLut(index); });
    connect(colorSpace_, &QComboBox::currentIndexChanged, this, [this] { applyDelivery(); });
    connect(toneMapping_, &QComboBox::currentIndexChanged, this, [this] { applyDelivery(); });
    gradeLayout->addWidget(bars_.nodesBox);
    gradeLayout->addWidget(effects_, 1);

    auto* rightColumn = new QSplitter(Qt::Vertical, this);
    rightColumn->setHandleWidth(1);
    rightColumn->addWidget(gradeColumn);
    rightColumn->addWidget(channel_);
    rightColumn->setStretchFactor(0, 4);
    rightColumn->setStretchFactor(1, 4);
    // Floors, so no panel can be squeezed to a sliver by its neighbours. A
    // scope four pixels tall or a mixer with no meter is worse than one
    // that pushes the window wider: it looks like a broken panel rather
    // than a small one.
    effects_->setMinimumHeight(220);
    scopes_->setMinimumHeight(150);
    mixer_->setMinimumHeight(190);
    topSplitter_->addWidget(rightColumn);
    // By index-of rather than by a literal: the panes either side of the
    // viewer have been reordered twice now, and a hard-coded 3 silently
    // stretches whichever pane has drifted into that slot.
    topSplitter_->setStretchFactor(topSplitter_->indexOf(programColumn), 3);
}

void PreviewWindow::wireWorkspacePanels() {
    // Picking a shot in the strip is the Color workspace's way of moving
    // about: it selects the clip and puts the playhead inside it, so the
    // monitor, the scopes and every parameter panel are all looking at the
    // same shot.
    connect(clipStrip_, &app::ClipStrip::chosen, this,
            [this](zaro::model::TrackId track, zaro::model::ClipId clip) {
                const model::Sequence* sequence = liveSequence();
                const model::Track* found =
                    sequence != nullptr ? sequence->findTrack(track) : nullptr;
                const model::Clip* shot = found != nullptr ? found->find(clip) : nullptr;
                if (shot == nullptr) {
                    return;
                }
                timeline_->selectOnly(track, clip);
                setPosition(shot->timelineRange.start());
            });

    connect(mixer_, &app::MixerPanel::pickedChanged, this,
            [this](zaro::model::TrackId track) { channel_->setTrack(track); });
    connect(channel_, &app::ChannelPanel::edited, this, [this] {
        mixer_->refresh();
        timeline_->update();
        updateTitle();
    });
    connect(loudness_, &app::LoudnessPanel::measureRequested, this, [this] { measureProgramme(); });

    // Picking a stem is how somebody asks "where is the music": it goes to
    // the first clip carrying that role and selects it, so the mixer and
    // the channel panel are looking at the same sound.
    connect(
        stems_, &app::StemsPanel::stemChosen, this,
        [this](zaro::model::TrackId track, zaro::model::ClipId clip, zaro::time::RationalTime at) {
            timeline_->selectOnly(track, clip);
            setPosition(at);
        });

    connect(nodes_, &app::GradeNodes::stageChosen, this,
            [this](int stage) { effects_->revealStage(stage); });

    connect(palette_, &app::ColorPalette::edited, this, [this] {
        renderCache_.clear();
        monitor_->update();
        timeline_->update();
        effects_->refresh();
        clipStrip_->refresh();
        refreshGradeChain();
        refreshInstruments();
        updateTitle();
    });

    // A still is a reference frame: grabbing one records where it came
    // from, and picking one points the split compare at that moment.
    connect(gallery_, &app::GalleryPanel::grabRequested, this, [this] { grabStill(); });
    connect(gallery_, &app::GalleryPanel::stillChosen, this, [this](zaro::time::RationalTime at) {
        setCompareMode(render::CompareMode::Split);
        setComparing(true, at);
        monitor_->update();
    });
    connect(gallery_, &app::GalleryPanel::lutChosen, this,
            [this](const QString& lut) { applyLookToSelection(lut); });

    connect(bin_, &app::ProjectBin::openRequested, this, [this](zaro::model::MediaRefId id) {
        // In Color, with the file as the target, opening one is also choosing
        // which file the wheels are about to grade. Elsewhere it just means
        // "let me look at this", which is what it has always meant.
        if (workspace_ == "Color" &&
            palette_->target() == app::ColorPalette::GradeTarget::MediaFile) {
            palette_->setMedia(id);
            syncGradeTarget();
            syncColorManagement();
        }
        if (const model::MediaRef* ref = document_.project().findMedia(id)) {
            source_->load(*ref);
            // Opening a clip is a request to look at it.
            setSourceShown(true);
        }
    });
    connect(bin_, &app::ProjectBin::openSubclipRequested, this, [this](zaro::model::SubclipId id) {
        const model::Subclip* subclip = document_.project().findSubclip(id);
        if (subclip == nullptr) {
            return;
        }
        if (const model::MediaRef* ref = document_.project().findMedia(subclip->source)) {
            source_->loadMarked(*ref, subclip->range);
            setSourceShown(true);
        }
    });
    connect(bin_, &app::ProjectBin::colorChanged, this, [this] {
        // The media source resolved each file's curve when it opened, so
        // correcting one means reopening -- the same swap the proxy toggle
        // makes, for the same reason.
        renderCache_.clear();
        if (Status reopened = reopenMedia(); !reopened) {
            app::warn(this, "Interpret", QString::fromStdString(reopened.error().toString()));
        }
        monitor_->update();
        updateCacheBar();
    });
    connect(bin_, &app::ProjectBin::replaceRequested, this,
            [this](zaro::model::MediaRefId id) { replaceSelectedSource(id); });
    connect(source_, &app::SourceMonitor::subclipRequested, this, [this] { makeSubclip(); });
    connect(source_, &app::SourceMonitor::insertRequested, this,
            [this] { placeFromSource(edit::PlaceMode::Insert); });
    connect(source_, &app::SourceMonitor::overwriteRequested, this,
            [this] { placeFromSource(edit::PlaceMode::Overwrite); });
    connect(bin_, &app::ProjectBin::edited, this, [this] {
        bars_.scrubber->setRange(0, static_cast<int>(liveSequence()->duration().frames()));
        // A file can now leave the project while the source viewer is showing
        // it. Left up, it is a monitor scrubbing through something the project
        // no longer has -- and every edit offered under it, Insert and Over
        // included, is refused by an operation that cannot find the media.
        if (source_->media().isValid() &&
            document_.project().findMedia(source_->media()) == nullptr) {
            source_->clear();
            setSourceShown(false);
        }
        // Through `afterEdit`, like every other thing that changes the project.
        //
        // This redrew the panels and stopped there, so the one fact it did not
        // carry was that there was now something to save: double-clicking a
        // file in the bin appended it to the cut and left the window saying
        // "Saved", with no dot beside the name and no star in the title bar.
        // Nothing was lost -- closing writes the recovery file either way --
        // but the readout somebody uses to decide whether to press Save was
        // telling them they had already pressed it.
        afterEdit();
        refresh();
    });
    // A file imported into a session already running has no decoder until the
    // media source is opened again: it resolves every path in the project once,
    // when it opens, and knows nothing of what arrives after. Without this the
    // import lands in the pane and both monitors stay black on it -- the file
    // is in the project, and nothing in the program can read a frame of it.
    //
    // The same call the media browser's own import makes, for the same reason;
    // it is only the Import button and a drop onto the pane that arrived here
    // without it.
    connect(bin_, &app::ProjectBin::addTitleRequested, this,
            [this](const QString& presetId) { addTitle(presetId.toStdString()); });
    // Either pane may be the one a project lands on; both hand it here.
    connect(bin_, &app::ProjectBin::projectDropped, this,
            [this](const QString& path) { openDropped(path); });
    connect(bin_, &app::ProjectBin::mediaImported, this, [this] {
        if (Status reopened = openMedia(); !reopened) {
            app::warn(this, "Import", QString::fromStdString(reopened.error().toString()));
        }
        bin_->refresh();
        // Quietly: somebody who has just chosen files in a dialog knows where
        // they are, and the mark on the row is enough to say if one has gone
        // since. The set has changed, so the marks have to be recomputed.
        checkMissingMedia();
        updateTitle();
    });
}

void PreviewWindow::buildWindowLayout() {
    mainSplitter_ = new QSplitter(Qt::Vertical, this);
    mainSplitter_->setHandleWidth(1);
    mainSplitter_->addWidget(topSplitter_);
    bars_.timelinePane = buildTimelinePane();
    mainSplitter_->addWidget(bars_.timelinePane);
    mainSplitter_->setStretchFactor(0, 3);
    mainSplitter_->setStretchFactor(1, 2);

    // Deliver is not a rearrangement of the edit panels, it is a different
    // screen: presets, settings and a render queue, with no timeline. So it
    // is a page rather than a workspace layout, and the workspace tabs
    // switch between them.
    deliver_ = adopting(new app::DeliverPanel(this));
    connect(deliver_, &app::DeliverPanel::queueChanged, this, [this] { updateChrome(); });

    bars_.workspaceStack = new QStackedWidget(this);
    bars_.workspaceStack->addWidget(mainSplitter_);
    bars_.workspaceStack->addWidget(deliver_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildTitleBar());
    layout->addWidget(buildToolBar());
    layout->addWidget(bars_.workspaceStack, 1);
    layout->addWidget(buildStatusBar());
}

void PreviewWindow::wireEditingSignals() {
    // The whole selection, not just the primary: the parameter panel is the
    // one thing that has something to say about the rest of it.
    connect(timeline_, &app::TimelineWidget::selectionSetChanged, effects_,
            qOverload<const std::vector<edit::ClipRef>&>(&app::EffectControls::setSelection));
    // Picking a track's header shows the track instead. The panel treats the
    // two as exclusive, as the timeline does.
    connect(timeline_, &app::TimelineWidget::trackSelected, effects_,
            &app::EffectControls::setTrackSelection);
    // And picking the span across a cut shows the transition, which is the
    // third of the three and the same rule again.
    connect(timeline_, &app::TimelineWidget::transitionSelected, effects_,
            &app::EffectControls::setTransitionSelection);
    // Removing one goes through the timeline rather than the panel, because
    // the timeline owns the selection: taking the transition off behind its
    // back would leave it outlining a span the model no longer holds. This is
    // the same path Delete takes.
    connect(effects_, &app::EffectControls::removeTransitionRequested, this,
            [this] { timeline_->removeSelected(false); });
    // Kept here too: syncing acts on the clip somebody has picked, and the
    // timeline is where picking happens.
    connect(timeline_, &app::TimelineWidget::selectionChanged, this,
            [this](model::TrackId track, model::ClipId clip) {
                selectedTrack_ = track;
                selectedClip_ = clip;
                palette_->setSelection(track, clip);
                clipStrip_->setSelection(track, clip);
                syncGradeTarget();
                syncColorManagement();
                refreshGradeChain();
                maskOverlay_->setTarget(&document_.project(), sequenceId_, track, clip,
                                        &document_.commands());
                titleOverlay_->setTarget(&document_.project(), sequenceId_, track, clip,
                                         &document_.commands());
            });
    connect(scopes_, &app::ScopesPanel::measurementNeeded, this, [this] { refreshInstruments(); });
    connect(mixer_, &app::MixerPanel::edited, this, [this] {
        // Mute and solo change the picture as well as the sound: a muted
        // video track stops being composited.
        monitor_->update();
        timeline_->update();
        refreshInstruments();
    });
    connect(effects_, &app::EffectControls::keyframesChanged, this,
            [this] { timeline_->update(); });
    // A nested clip's way in. Which sequence the window is showing is a
    // decision about the whole window, so the panel asks rather than does.
    connect(effects_, &app::EffectControls::openSequenceRequested, this,
            [this](model::SequenceId id) { setActiveSequence(id); });
    connect(effects_, &app::EffectControls::edited, this, [this] {
        // A parameter change alters the picture at the current playhead.
        monitor_->update();
        timeline_->update();
        refreshInstruments();
    });

    // The two panels drive each other: scrubbing the timeline moves the
    // picture, and playback moves the playhead.
    connect(timeline_, &app::TimelineWidget::playheadMoved, this,
            [this](const time::RationalTime& position) {
                stop();
                setPosition(position);
            });
    connect(timeline_, &app::TimelineWidget::viewChanged, this, [this] {
        updateCacheBar();
        updateChrome();
    });
    // The analysis needs a progress dialog and a way to cancel it, both of
    // which are the window's, so the timeline asks rather than acts.
    connect(timeline_, &app::TimelineWidget::addTitleRequested, this, [this] { addTitle(); });
    connect(timeline_, &app::TimelineWidget::detectScenesRequested, this,
            [this] { static_cast<void>(detectScenes()); });
    connect(timeline_, &app::TimelineWidget::projectDropped, this,
            [this](const QString& path) { openDropped(path); });
    connect(timeline_, &app::TimelineWidget::mediaDropped, this,
            [this](const QStringList& paths, const QPoint& at) { importDropped(paths, at); });
    connect(timeline_, &app::TimelineWidget::toolChanged, this, [this] { updateChrome(); });
    connect(timeline_, &app::TimelineWidget::snapChanged, this, [this] { updateChrome(); });
    connect(timeline_, &app::TimelineWidget::edited, this, [this] {
        // Undo can change a clip's parameters as well as its position, so
        // the panel has to re-read rather than trust what it last wrote.
        effects_->refresh();
        mixer_->refresh();
        // An edit can change the duration, and can change what is under the
        // playhead, so both the scrubber and the picture need refreshing.
        bars_.scrubber->setRange(0, static_cast<int>(liveSequence()->duration().frames()));
        monitor_->update();
        refresh();
        // An edit invalidates whatever it shows on, so the bar has to be
        // re-read rather than left claiming a range that no longer renders
        // to what is cached.
        updateCacheBar();
        updateTitle();
    });

    connect(bars_.playButton, &QPushButton::clicked, this, [this] { togglePlay(); });
    connect(bars_.scrubber, &QSlider::sliderMoved, this, [this](int value) {
        // Scrubbing stops playback: the playhead is being driven by hand,
        // and having the clock fight it is what makes scrubbing feel loose.
        stop();
        setPosition(time::RationalTime{value, liveSequence()->frameRate()});
    });

    // The clock drives the playhead, and the play button follows the clock
    // rather than the click: pressing play when the device will not open
    // leaves the transport stopped, and a button that already said "pause"
    // would be describing something that is not happening.
    connect(&playback_, &PlaybackController::moved, this, [this](const time::RationalTime& at) {
        // The clock is polled far faster than the sequence has frames --
        // deliberately, so the playhead is never waiting on the timer --
        // so most ticks arrive with the playhead still inside the frame
        // it was already on. setPosition is per-frame work: formatting
        // timecode, laying out the overlay, repainting the timeline. Run
        // at the polling rate it spends the GUI thread on answers nobody
        // can see, and it is the same thread that has to get the next
        // frame drawn.
        //
        // The guard belongs here rather than inside setPosition, which
        // every edit also calls to bring the preview back in step at a
        // playhead that has not moved.
        if (at == position_) {
            return;
        }
        setPosition(at);
    });
    connect(&playback_, &PlaybackController::playingChanged, this, [this](bool playing) {
        if (bars_.playButton != nullptr) {
            bars_.playButton->setText(playing ? kPauseGlyph : kPlayGlyph);
        }
        // Said at the moment somebody presses play and nothing happens, which
        // is when the question is being asked. Once per session: it is a fact
        // about the machine, and repeating it every press would be nagging
        // about something they cannot fix from here.
        if (playing && playback_.audioDeviceMissing() && !warnedNoAudioDevice_) {
            warnedNoAudioDevice_ = true;
            app::say(this, "Playback",
                     "No audio device would open, so the playhead has nothing to keep time "
                     "by and will not move.\n\n" +
                         playback_.audioDeviceError());
        }
        updateChrome();
    });
}

void PreviewWindow::startTimers() {
    // Meters at twenty a second. Faster is invisible on a meter with a peak
    // hold, and each tick is a mix of a short block when the transport is
    // stopped.
    meterTimer_ = new QTimer(this);
    meterTimer_->setInterval(50);
    connect(meterTimer_, &QTimer::timeout, this, [this] { updateMeters(); });
    meterTimer_->start();

    updateTitle();
    // Every thirty seconds, and only when something has changed. A timer
    // that writes regardless would rewrite an untouched project all day;
    // one that writes on every edit would stall a drag on disk.
    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(30000);
    connect(autosaveTimer_, &QTimer::timeout, this, [this] { autosave(); });
    autosaveTimer_->start();
    restoreWorkspace();
    refresh();

    // The project handed to the constructor never went through `adopt`, which
    // is where every *later* project is checked -- so a cut opened from the
    // command line, or by double-clicking it, arrived with its footage missing
    // and nothing said so. Once round the event loop rather than here, because
    // the window has not been shown yet and a dialog parented to a window
    // nobody can see is a program that appears to have hung on start-up.
    QTimer::singleShot(0, this, [this] { checkMissingMedia(true); });
}

void PreviewWindow::exportDialog() {
    if (liveSequence() == nullptr) {
        return;
    }
    stop();
    app::ExportDialog dialog{document_.project(), liveSequence()->id(), this};
    dialog.exec();
}

void PreviewWindow::exportOtio() {
    if (liveSequence() == nullptr) {
        return;
    }
    // Export only, from the window. Importing an OTIO file produces a
    // project of its own, and replacing the open one needs a "save first?"
    // that does not exist yet -- so that direction lives in zaro-otio,
    // where there is nothing to lose.
    const QString path = QFileDialog::getSaveFileName(this, "Export OpenTimelineIO",
                                                      "timeline.otio", "OpenTimelineIO (*.otio)");
    if (path.isEmpty()) {
        return;
    }
    if (Status saved = io::saveOtio(document_.project(), liveSequence()->id(), path.toStdString());
        !saved) {
        app::warn(this, "OpenTimelineIO", QString::fromStdString(saved.error().toString()));
    }
}

void PreviewWindow::exportPremiere() {
    if (liveSequence() == nullptr) {
        return;
    }
    // Named for the program rather than for the format. "FCP7 XML" is what the
    // file is; "the one Premiere opens" is what somebody came here for, and the
    // menu already said Premiere.
    const QString path = QFileDialog::getSaveFileName(this, "Export Premiere XML", "timeline.xml",
                                                      "FCP7 XML (*.xml)");
    if (path.isEmpty()) {
        return;
    }
    if (Status saved =
            io::savePremiereXml(document_.project(), liveSequence()->id(), path.toStdString());
        !saved) {
        app::warn(this, "Premiere XML", QString::fromStdString(saved.error().toString()));
    }
}

void PreviewWindow::importPremiere() {
    const QString path =
        QFileDialog::getOpenFileName(this, "Import Premiere XML", {}, "FCP7 XML (*.xml)");
    if (path.isEmpty()) {
        return;
    }
    auto imported = io::loadPremiereXml(path.toStdString());
    if (!imported) {
        app::warn(this, "Premiere XML", QString::fromStdString(imported.error().toString()));
        return;
    }
    adoptImported(std::move(*imported), "Premiere XML",
                  "Grades, effects, transitions and keyframes");
}

void PreviewWindow::exportFinalCut() {
    if (liveSequence() == nullptr) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Final Cut Pro XML", "timeline.fcpxml", "Final Cut Pro XML (*.fcpxml)");
    if (path.isEmpty()) {
        return;
    }
    if (Status saved =
            io::saveFcpXml(document_.project(), liveSequence()->id(), path.toStdString());
        !saved) {
        app::warn(this, "Final Cut Pro XML", QString::fromStdString(saved.error().toString()));
    }
}

void PreviewWindow::importFinalCut() {
    // The bundle as well as the file. Final Cut 10.6.6 began writing a
    // `.fcpxmld` directory whose `Info.fcpxml` is the document, and what
    // somebody picks in this dialog is the bundle -- so it has to be offered,
    // and `loadFcpXml` looks inside.
    const QString path = QFileDialog::getOpenFileName(this, "Import Final Cut Pro XML", {},
                                                      "Final Cut Pro XML (*.fcpxml *.fcpxmld)");
    if (path.isEmpty()) {
        return;
    }
    auto imported = io::loadFcpXml(path.toStdString());
    if (!imported) {
        app::warn(this, "Final Cut Pro XML", QString::fromStdString(imported.error().toString()));
        return;
    }
    adoptImported(std::move(*imported), "Final Cut Pro XML",
                  "Grades, effects, transitions, keyframes and track mute and lock");
}

void PreviewWindow::adoptImported(model::Project imported, const QString& format,
                                  const QString& lost) {
    // Read out before the move below, not after it.
    const model::Sequence& sequence = imported.sequences().front();
    const QString name = QString::fromStdString(sequence.name());
    const std::size_t tracks = sequence.videoTracks().size() + sequence.audioTracks().size();
    const std::size_t media = imported.media().size();

    // Adopted with no path, exactly as New does: what came in is a cut, not a
    // project file, and letting Save write straight over the interchange file
    // would replace it with something the other program can no longer read.
    adopt(std::move(imported), io::LoadedProject{}, {});
    updateTitle();

    // Said rather than assumed. Only the cut crosses these formats -- what did
    // not is the sort of thing somebody finds an hour later, in a grade that is
    // not there.
    app::say(this, format,
             QString("Imported \u201c%1\u201d: %2 tracks, %3 media file%4.\n\n"
                     "%5 do not cross this format and were not read. "
                     "Save to keep this as a project.")
                 .arg(name)
                 .arg(tracks)
                 .arg(media)
                 .arg(media == 1 ? "" : "s")
                 .arg(lost));
}

void PreviewWindow::trackMask() {
    // Every frame of the rest of the clip gets composited, which is not
    // instant on a long one.
    // A dialog rather than a wait cursor. This composites every frame of the
    // rest of the clip, and on a long one a frozen window with a spinning
    // cursor and no way out is indistinguishable from a hang.
    QProgressDialog progress("Tracking the mask…", "Stop", 0, 1, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    auto tracked = trackMaskForward([&](std::int64_t done, std::int64_t total) {
        progress.setMaximum(static_cast<int>(total));
        progress.setValue(static_cast<int>(done));
        QCoreApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.reset();
    if (!tracked) {
        app::say(this, "Track mask", QString::fromStdString(tracked.error().message()));
        return;
    }
    QString said = QString("Tracked %1 frame%2, weakest match %3.")
                       .arg(tracked->frames)
                       .arg(tracked->frames == 1 ? "" : "s")
                       .arg(tracked->confidence, 0, 'f', 2);
    if (!tracked->stopped.empty()) {
        // Said plainly, with the keyframes kept: where a track gave up is
        // exactly where somebody needs to look.
        said += QString("\nStopped early: %1").arg(QString::fromStdString(tracked->stopped));
    }
    app::say(this, "Track mask", said);
}

void PreviewWindow::stabilise() {
    QProgressDialog progress("Measuring the camera move…", "Stop", 0, 1, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    auto held = stabiliseClip([&](std::int64_t done, std::int64_t total) {
        progress.setMaximum(static_cast<int>(total));
        progress.setValue(static_cast<int>(done));
        QCoreApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.reset();
    if (!held) {
        app::say(this, "Stabilise", QString::fromStdString(held.error().message()));
        return;
    }
    QString said = QString("Stabilised %1 frames, zoomed in %2%.")
                       .arg(held->measured)
                       .arg((held->zoom - 1.0) * 100.0, 0, 'f', 1);
    if (!held->stopped.empty()) {
        said += QString("\nStopped early: %1").arg(QString::fromStdString(held->stopped));
    }
    app::say(this, "Stabilise", said);
}

void PreviewWindow::clearStabilisation() {
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr || !selectedClip_.isValid()) {
        return;
    }
    auto built = edit::makeStabilise(document_.project(), {sequence->id(), selectedTrack_},
                                     selectedClip_, model::Curve{}, model::Curve{}, 1.0);
    if (!built) {
        return;
    }
    document_.commands().execute(document_.project(), std::move(*built));
    document_.commands().breakMerge();
    renderCache_.clear();
    monitor_->update();
    effects_->refresh();
    updateCacheBar();
    updateTitle();
}

void PreviewWindow::checkMissingMedia(bool announce) {
    const std::vector<model::MediaRefId> gone = io::missingMedia(document_.project());
    bin_->setMissingMedia(gone);
    if (gone.empty() || !announce) {
        return;
    }

    // A dialog rather than a line in the status bar, and only on the way in.
    // Every clip of a file that is not there draws nothing, decodes nothing and
    // exports nothing -- the timeline looks entirely normal and the picture is
    // simply black, which is a puzzle rather than a fault until somebody is
    // told what it is. The bin marks the rows; this is what points at them.
    QString said = QString("%1 file%2 the project uses %3 not where it left %4:")
                       .arg(gone.size())
                       .arg(gone.size() == 1 ? "" : "s")
                       .arg(gone.size() == 1 ? "is" : "are")
                       .arg(gone.size() == 1 ? "it" : "them");
    // A few names, not all of them: a project whose drive is unmounted is a
    // list of every file in it, and a dialog nobody can read is a dialog
    // nobody reads.
    constexpr std::size_t kNamed = 6;
    for (std::size_t i = 0; i < gone.size() && i < kNamed; ++i) {
        const model::MediaRef* ref = document_.project().findMedia(gone[i]);
        if (ref != nullptr) {
            said += "\n    " + QString::fromStdString(ref->path);
        }
    }
    if (gone.size() > kNamed) {
        said += QString("\n    and %1 more").arg(gone.size() - kNamed);
    }
    if (app::isQuiet()) {
        // Said, not asked. Quiet mode exists so a scripted run does not stop on
        // a sentence, and this one would stop it on the way in -- before the
        // window has finished opening, which is the worst place for it.
        app::say(this, "Missing media", said);
        return;
    }
    said += "\n\nThey are marked in the media pane. Look for them now?";
    const auto answer = QMessageBox::question(this, "Missing media", said,
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) {
        relinkDialog();
    }
}

void PreviewWindow::relinkDialog() {
    const QString root = QFileDialog::getExistingDirectory(this, "Look for missing media in");
    if (root.isEmpty()) {
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto report = relinkMedia(root.toStdString());
    QApplication::restoreOverrideCursor();
    if (!report) {
        app::warn(this, "Relink", QString::fromStdString(report.error().message()));
        return;
    }
    std::size_t byName = 0;
    for (const zaro::io::RelinkMatch& match : report->matches) {
        if (!match.byContent) {
            ++byName;
        }
    }
    QString said = QString("Relinked %1 of %2 missing file%3, from %4 looked at.")
                       .arg(report->matches.size())
                       .arg(report->matches.size() + report->stillMissing.size())
                       .arg(report->matches.size() + report->stillMissing.size() == 1 ? "" : "s")
                       .arg(report->examined);
    if (byName > 0) {
        // Said plainly: a file matched only by its name is one somebody
        // should look at before trusting the cut.
        said += QString("\n%1 matched by name only — check they are the right takes.").arg(byName);
    }
    // Whatever was found is no longer missing, and whatever was not still is.
    // Without this the bin keeps the marks it was given before the search.
    checkMissingMedia();
    app::say(this, "Relink", said);
}

void PreviewWindow::consolidateDialog() {
    const QString into = QFileDialog::getExistingDirectory(this, "Gather the project's media into");
    if (into.isEmpty()) {
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto report = consolidateMedia(into.toStdString());
    QApplication::restoreOverrideCursor();
    if (!report) {
        app::warn(this, "Consolidate", QString::fromStdString(report.error().message()));
        return;
    }
    QString said = QString("Gathered %1 file%2, %3 MB copied.")
                       .arg(report->files.size())
                       .arg(report->files.size() == 1 ? "" : "s")
                       .arg(static_cast<double>(report->bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    if (!report->missing.empty()) {
        // Named, because a consolidate that quietly left files behind is
        // an archive somebody will discover is incomplete much later.
        said += QString("\n%1 could not be found — relink them first.").arg(report->missing.size());
    }
    app::say(this, "Consolidate", said);
}

void PreviewWindow::saveTemplateDialog() {
    const QString path = QFileDialog::getSaveFileName(this, "Save graphic as template", {},
                                                      "Graphic template (*.cutreelgraph)");
    if (path.isEmpty()) {
        return;
    }
    if (Status saved = saveGraphicTemplate(path.toStdString()); !saved) {
        app::warn(this, "Template", QString::fromStdString(saved.error().message()));
    }
}

void PreviewWindow::addTitle(std::string_view presetId) {
    // Five seconds, which is a title somebody can read and a length they can
    // trim. The alternative -- as long as whatever is under the playhead -- is
    // right for a lower third and wrong for a card on black, and this is the
    // one that is easy to change by dragging an edge.
    constexpr double kTitleSeconds = 5.0;
    const time::RationalTime length = time::RationalTime::fromSeconds(
        time::Rational::approximate(kTitleSeconds), liveSequence()->frameRate());

    // An id from a build that knew about a preset this one does not still gets
    // a title rather than nothing: the point of the action is a text clip at
    // the playhead, and where it sits is the part that can be wrong quietly.
    const TitlePreset* preset = findTitlePreset(presetId);
    const TitlePreset& chosen = preset != nullptr ? *preset : titlePresets().front();

    auto made = commands::addTitle(editContext(), chosen, length);
    if (!made) {
        app::warn(this, "Title", QString::fromStdString(made.error().toString()));
        return;
    }
    // Selected, because a title arrives saying nothing but "Title" and the next
    // thing anybody does is type over it -- which happens in the inspector, on
    // whatever is selected.
    timeline_->selectOnly(made->track, made->clip);
    bars_.scrubber->setRange(0, static_cast<int>(liveSequence()->duration().frames()));
    afterEdit();
    refresh();
}

void PreviewWindow::animateTitle(commands::TitleMotion motion) {
    if (Status animated = commands::animateTitle(editContext(), motion); !animated) {
        app::warn(this, "Title", QString::fromStdString(animated.error().toString()));
        return;
    }
    effects_->refresh();
    timeline_->update();
    afterEdit();
}

void PreviewWindow::placeTemplateDialog() {
    const QString path = QFileDialog::getOpenFileName(this, "Place graphic template", {},
                                                      "Graphic template (*.cutreelgraph)");
    if (path.isEmpty()) {
        return;
    }
    auto placed = placeGraphicTemplate(path.toStdString(), time::RationalTime{});
    if (!placed) {
        app::warn(this, "Template", QString::fromStdString(placed.error().message()));
    }
}

void PreviewWindow::exportReviewNotes() {
    const QString chosen =
        QFileDialog::getSaveFileName(this, "Export review notes", {}, "Markdown (*.md)");
    if (chosen.isEmpty()) {
        return;
    }
    if (Status written = writeReviewNotes(chosen.toStdString()); !written) {
        app::warn(this, "Review", QString::fromStdString(written.error().message()));
    }
}

void PreviewWindow::matchShot() {
    auto match = matchToReference();
    if (!match) {
        app::say(this, "Match", QString::fromStdString(match.error().message()));
        return;
    }
    if (!match->usable) {
        // Said, not applied. The person looking at both frames decides.
        app::say(this, "Match", QString::fromStdString(match->reason));
        return;
    }
    app::say(this, "Match",
             QString("Matched: the two shots were %1 apart and are now %2.")
                 .arg(match->before, 0, 'f', 3)
                 .arg(match->after, 0, 'f', 3));
}

void PreviewWindow::proxyMenu() {
    std::vector<chrome::ProxyEntry> entries;
    entries.reserve(document_.project().media().size());
    for (const model::MediaRef& media : document_.project().media()) {
        entries.push_back({media.id, QString::fromStdString(media.name), !media.proxyPath.empty()});
    }
    const chrome::ProxyChoice chosen =
        chrome::proxyMenu(entries, document_.project().usingProxies());
    switch (chosen.kind) {
        case chrome::ProxyChoice::Kind::ToggleUsingProxies: {
            document_.project().setUsingProxies(!document_.project().usingProxies());
            // The media source resolved its paths when it opened, so
            // switching means reopening. Cheaper than deciding per read,
            // and it is the only moment the decision changes.
            if (Status reopened = openMedia(); !reopened) {
                app::warn(this, "Proxies", QString::fromStdString(reopened.error().toString()));
            }
            monitor_->update();
            refresh();
            break;
        }
        case chrome::ProxyChoice::Kind::Build: {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            auto made = buildProxy(chosen.media);
            QApplication::restoreOverrideCursor();
            if (!made) {
                app::warn(this, "Proxies", QString::fromStdString(made.error().message()));
                return;
            }
            app::say(
                this, "Proxies",
                QString("Made a %1x%2 proxy of %3 frames, %4% of the size.")
                    .arg(made->width)
                    .arg(made->height)
                    .arg(made->frames)
                    .arg(made->sourceBytes == 0 ? 0.0
                                                : (100.0 * static_cast<double>(made->proxyBytes) /
                                                   static_cast<double>(made->sourceBytes)),
                         0, 'f', 1));
            break;
        }
        case chrome::ProxyChoice::Kind::Attach: {
            const QString path = QFileDialog::getOpenFileName(this, "Choose a proxy file");
            if (path.isEmpty()) {
                return;
            }
            for (model::MediaRef& media : document_.project().mediaMutable()) {
                if (media.id == chosen.media) {
                    media.proxyPath = path.toStdString();
                }
            }
            break;
        }
        case chrome::ProxyChoice::Kind::None:
            break;
    }
}

void PreviewWindow::multicamMenu() {
    const model::Clip* clip = editContext().selectedClip();
    const bool multicam = clip != nullptr && clip->isMulticam();
    switch (chrome::multicamMenu(multicam, media_ != nullptr)) {
        case chrome::MulticamChoice::ByAudio:
            syncAngles(true);
            break;
        case chrome::MulticamChoice::ByTimecode:
            syncAngles(false);
            break;
        case chrome::MulticamChoice::None:
            break;
    }
}

void PreviewWindow::renderMenu() {
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr || media_ == nullptr) {
        return;
    }
    const time::TimeRange visible = timeline_->layout().visibleRange(sequence->frameRate());
    switch (chrome::renderMenu(visible.isEmpty() ? 0 : visible.duration().frames(),
                               renderCache_.count(), renderCache_.byteSize())) {
        case chrome::RenderChoice::ClearCache:
            renderCache_.clear();
            updateCacheBar();
            break;
        case chrome::RenderChoice::RenderVisible:
            renderVisibleRange();
            break;
        case chrome::RenderChoice::None:
            break;
    }
}

void PreviewWindow::loudnessMenu() {
    if (liveSequence() == nullptr || media_ == nullptr) {
        return;
    }
    // Stop the transport first. Playback decodes on a thread of its own -- see
    // PlaybackController::pumpAudio -- through the very media source this is
    // about to read, and nothing guards one decoder against two threads: doing
    // both at once faults inside avcodec. `stop` joins that thread, so past
    // this line the decoder is this thread's alone.
    //
    // Stopped rather than locked, and deliberately. A lock would have to be
    // held for a whole-programme decode, which is exactly as long as the wait
    // cursor below is up -- the realtime path would starve and the device would
    // drop out. Nothing was going to be heard through a modal wait anyway.
    playback_.stop();
    render::AudioGraph mixer{*media_};
    const time::TimeRange whole{time::RationalTime{0, liveSequence()->frameRate()},
                                liveSequence()->duration()};
    auto measured = mixer.measureLoudness(*liveSequence(), whole);
    if (!measured) {
        app::warn(this, "Loudness", QString::fromStdString(measured.error().toString()));
        return;
    }

    constexpr double kTarget = -23.0;  // EBU R128
    const double gain = measured->gainToReach(kTarget);
    if (!chrome::loudnessMenu(measured->integratedLufs, measured->samplePeakDbfs, kTarget)) {
        return;
    }
    // Applied to every audio track's fader rather than to a master gain,
    // which does not exist: the balance between tracks is a decision
    // somebody made, and moving them all by the same amount keeps it.
    for (const model::Track& track : liveSequence()->audioTracks()) {
        edit::TrackState state;
        state.muted = track.isMuted();
        state.soloed = track.isSoloed();
        state.gainDb = track.gainDb() + gain;
        state.pan = track.pan();
        if (auto built = edit::makeSetTrackState(document_.project(), liveSequence()->id(),
                                                 track.id(), state)) {
            document_.commands().execute(document_.project(), std::move(*built));
        }
    }
    document_.commands().breakMerge();
    mixer_->refresh();
}

void PreviewWindow::captionsMenu() {
    if (liveSequence() == nullptr) {
        return;
    }
    const model::CaptionTrack& captions = liveSequence()->captions();
    switch (chrome::captionsMenu(captions.size(), captions.isBurnedIn())) {
        case chrome::CaptionChoice::Import: {
            const QString path = QFileDialog::getOpenFileName(
                this, "Open subtitles", {}, "Subtitles (*.srt *.vtt);;All files (*)");
            if (path.isEmpty()) {
                return;
            }
            auto loaded = io::loadSubtitles(path.toStdString());
            if (!loaded) {
                app::warn(this, "Subtitles", QString::fromStdString(loaded.error().toString()));
                return;
            }
            // The style and the burn-in setting belong to the sequence, not
            // to the file: importing a new script should not silently turn
            // burn-in off or lose a typeface somebody chose.
            model::CaptionTrack merged = *loaded;
            merged.setStyle(liveSequence()->captions().style());
            merged.setBurnedIn(liveSequence()->captions().isBurnedIn());
            applyCaptions(merged);
            break;
        }
        case chrome::CaptionChoice::Export: {
            const QString path = QFileDialog::getSaveFileName(
                this, "Save subtitles", "captions.srt", "SubRip (*.srt);;WebVTT (*.vtt)");
            if (path.isEmpty()) {
                return;
            }
            const auto format = io::formatForPath(path.toStdString());
            if (Status saved =
                    io::saveSubtitles(liveSequence()->captions(), path.toStdString(), format);
                !saved) {
                app::warn(this, "Subtitles", QString::fromStdString(saved.error().toString()));
            }
            break;
        }
        case chrome::CaptionChoice::ToggleBurnIn: {
            model::CaptionTrack changed = liveSequence()->captions();
            changed.setBurnedIn(!changed.isBurnedIn());
            applyCaptions(changed);
            break;
        }
        case chrome::CaptionChoice::None:
            break;
    }
}

void PreviewWindow::applyCaptions(const model::CaptionTrack& captions) {
    auto built = edit::makeSetCaptions(document_.project(), liveSequence()->id(), captions);
    if (!built) {
        return;
    }
    document_.commands().execute(document_.project(), std::move(*built));
    document_.commands().breakMerge();
    monitor_->update();
    timeline_->update();
    refreshInstruments();
}

void PreviewWindow::setActiveSequence(model::SequenceId id) {
    if (document_.project().findSequence(id) == nullptr) {
        return;
    }
    document_.project().setActiveSequence(id);
    sequenceId_ = id;
    position_ = time::RationalTime{0, document_.project().findSequence(id)->frameRate()};
    selectedTrack_ = model::TrackId{};
    selectedClip_ = model::ClipId{};
    renderCache_.clear();
    rebindSequence();
    refresh();
}

void PreviewWindow::rebindSequence() {
    const ui::SequenceBinding binding{&document_.project(), sequenceId_, &document_.commands()};
    for (ui::SequenceBound* panel : bound_) {
        if (panel != nullptr) {
            panel->bind(binding);
        }
    }
    monitor_->setSource(liveSequence(), media_.get());
    playback_.setSource(liveSequence(), media_.get());
    monitor_->setNesting(&document_.project(), media_.get());
    monitor_->setRenderCache(&renderCache_);
    monitor_->setTextRasterizer(&text_);
    monitor_->update();
}

commands::Context PreviewWindow::editContext() {
    return commands::Context{
        ui::SequenceBinding{&document_.project(), sequenceId_, &document_.commands()},
        selectedTrack_,
        selectedClip_,
        position_,
        media_.get(),
        &renderCache_,
        &text_};
}

void PreviewWindow::afterEdit() {
    document_.commands().breakMerge();
    renderCache_.clear();
    monitor_->update();
    timeline_->update();
    effects_->refresh();
    updateCacheBar();
    updateTitle();
}

void PreviewWindow::afterMediaChange() {
    // Reopened here rather than by the operation that changed the paths:
    // the source belongs to the window, and an operation that reached in to
    // reopen it was an operation that could only be called from one.
    if (Status reopened = openMedia(); !reopened) {
        app::warn(this, "Media", QString::fromStdString(reopened.error().toString()));
    }
    renderCache_.clear();
    monitor_->update();
    timeline_->update();
    bin_->refresh();
    updateCacheBar();
    updateTitle();
}

void PreviewWindow::updateMeters() {
    if (mixer_ == nullptr || liveSequence() == nullptr || !mixer_->isVisible()) {
        return;
    }
    if (playback_.isPlaying()) {
        mixer_->setMeters(playback_.meters());
        return;
    }
    if (media_ == nullptr) {
        return;
    }
    render::AudioGraph meterMix{*media_};
    const auto& audioRate = liveSequence()->audioSampleRate();
    constexpr std::int64_t kBlock = 1024;
    if (meterMix.mix(*liveSequence(), position_.rescaledTo(audioRate), kBlock, 2)) {
        mixer_->setMeters(meterMix.meters());
    }
}

void PreviewWindow::waitForWaveforms() {
    if (waveformThread_.joinable()) {
        waveformThread_.join();
    }
    // The results are handed over through the event loop, so they are not
    // on screen until it has been given a chance to run.
    QApplication::processEvents();
}

const model::Sequence* PreviewWindow::liveSequence() const {
    return document_.project().findSequence(sequenceId_);
}

Result<render::ShotMatch> PreviewWindow::matchToReference() {
    if (!comparing_) {
        return Error{ErrorCode::InvalidData, "hold a frame to match against first"};
    }
    auto matched = commands::matchToReference(editContext(), referenceAt_);
    if (matched && matched->usable) {
        afterEdit();
    }
    return matched;
}

Result<commands::MaskTrack> PreviewWindow::trackMaskForward(const commands::Progress& tell) {
    auto tracked = commands::trackMaskForward(editContext(), tell);
    if (tracked) {
        afterEdit();
    }
    return tracked;
}

Result<render::StabiliseResult> PreviewWindow::stabiliseClip(const commands::Progress& tell) {
    auto steadied = commands::stabiliseClip(editContext(), tell);
    if (steadied) {
        afterEdit();
    }
    return steadied;
}

Result<io::RelinkReport> PreviewWindow::relinkMedia(const std::string& root) {
    auto report = commands::relinkMedia(editContext(), root);
    if (report) {
        afterMediaChange();
    }
    return report;
}

Result<platform::ffmpeg::ProxySummary> PreviewWindow::buildProxy(model::MediaRefId mediaId,
                                                                 std::int32_t width) {
    auto built = commands::buildProxy(editContext(), mediaId, width);
    if (built) {
        afterMediaChange();
    }
    return built;
}

Result<bool> PreviewWindow::toggleCommentHere() {
    auto toggled = commands::toggleCommentHere(editContext());
    if (toggled) {
        timeline_->update();
        updateTitle();
    }
    return toggled;
}

Status PreviewWindow::writeReviewNotes(const std::string& path) {
    return commands::writeReviewNotes(editContext(), path);
}

app::MediaBrowser* PreviewWindow::browseMedia() {
    if (browser_ == nullptr) {
        browser_ = adopting(new app::MediaBrowser(this));
        connect(browser_, &app::MediaBrowser::imported, this, [this] {
            if (Status reopened = openMedia(); !reopened) {
                app::warn(this, "Import", QString::fromStdString(reopened.error().toString()));
            }
            bin_->refresh();
            updateTitle();
        });
    }
    if (browser_->folder().empty()) {
        std::string start = std::filesystem::current_path().string();
        for (const model::MediaRef& media : document_.project().media()) {
            const std::filesystem::path parent = std::filesystem::path{media.path}.parent_path();
            if (!parent.empty() && std::filesystem::is_directory(parent)) {
                start = parent.string();
            }
        }
        static_cast<void>(browser_->showFolder(start));
    }
    browser_->show();
    browser_->raise();
    return browser_;
}

app::Hotkeys* PreviewWindow::showHotkeys() {
    if (hotkeys_ == nullptr) {
        hotkeys_ = new app::Hotkeys(actions_.keymap(), this);
        hotkeys_->setOnChanged([this] {
            applyKeymap();
            saveKeymap();
        });
    }
    hotkeys_->refresh();
    hotkeys_->show();
    hotkeys_->raise();
    return hotkeys_;
}

Result<io::ConsolidateReport> PreviewWindow::consolidateMedia(const std::string& destination) {
    auto report = commands::consolidateMedia(editContext(), destination);
    if (report) {
        afterMediaChange();
    }
    return report;
}

app::Transcript* PreviewWindow::showTranscript() {
    if (transcript_ == nullptr) {
        transcript_ = adopting(new app::Transcript(this));
        connect(transcript_, &app::Transcript::edited, this, [this] {
            renderCache_.clear();
            timeline_->update();
            monitor_->update();
            updateCacheBar();
            updateTitle();
            refresh();
        });
        connect(transcript_, &app::Transcript::scrubbed, this,
                [this](time::RationalTime at) { setPosition(at); });
    }
    transcript_->show();
    transcript_->raise();
    return transcript_;
}

Result<render::RemixPlan> PreviewWindow::remixSelectedTo(double targetSeconds) {
    auto plan = commands::remixSelectedTo(editContext(), targetSeconds);
    if (plan) {
        afterEdit();
    }
    return plan;
}

Result<render::ReframeResult> PreviewWindow::reframeClip() {
    auto framed = commands::reframeClip(editContext());
    if (framed) {
        afterEdit();
    }
    return framed;
}

Result<model::ClipId> PreviewWindow::pinTo(model::ClipId host) {
    auto pinned = commands::pinTo(editContext(), host);
    if (pinned) {
        afterEdit();
    }
    return pinned;
}

Result<model::ClipId> PreviewWindow::pinToClipBelow() {
    auto pinned = commands::pinToClipBelow(editContext());
    if (pinned) {
        afterEdit();
    }
    return pinned;
}

Status PreviewWindow::saveGraphicTemplate(const std::string& path) {
    return commands::saveGraphicTemplate(editContext(), path);
}

Result<model::ClipId> PreviewWindow::placeGraphicTemplate(const std::string& path,
                                                          const time::RationalTime& at) {
    auto placed = commands::placeGraphicTemplate(editContext(), path, at);
    if (placed) {
        afterEdit();
    }
    return placed;
}

void PreviewWindow::setComparing(bool on, const time::RationalTime& reference) {
    comparing_ = on;
    referenceAt_ = reference;
    monitor_->setComparison(on, referenceAt_, compareMode_, compareSplit_);
    monitor_->update();
}

void PreviewWindow::setCompareMode(render::CompareMode mode) {
    compareMode_ = mode;
    monitor_->setComparison(comparing_, referenceAt_, compareMode_, compareSplit_);
    monitor_->update();
}

void PreviewWindow::setCompareSplit(double split) {
    compareSplit_ = std::clamp(split, 0.0, 1.0);
    monitor_->setComparison(comparing_, referenceAt_, compareMode_, compareSplit_);
    monitor_->update();
}

void PreviewWindow::copySelection() {
    const model::Sequence* sequence = liveSequence();
    const std::vector<edit::ClipRef>& picked = timeline_->selection();
    if (sequence == nullptr || picked.empty()) {
        app::say(this, "Copy", "Select a clip on the timeline first.");
        return;
    }

    // Measured from the earliest clip in the copy, not from the playhead: what
    // is being remembered is the shape of the selection, and where the playhead
    // happened to be when somebody pressed Ctrl+C is not part of it.
    std::vector<Copied> gathered;
    std::optional<time::RationalTime> earliest;
    for (const edit::ClipRef& ref : picked) {
        const model::Track* track = sequence->findTrack(ref.track);
        const model::Clip* clip = track != nullptr ? track->find(ref.clip) : nullptr;
        if (clip == nullptr) {
            continue;
        }
        if (!earliest || clip->start() < *earliest) {
            earliest = clip->start();
        }
        gathered.push_back(Copied{ref.track, *clip, {}});
    }
    if (gathered.empty() || !earliest) {
        app::say(this, "Copy", "Those clips are no longer on the timeline.");
        return;
    }
    for (Copied& copied : gathered) {
        copied.offset = copied.clip.start() - *earliest;
    }
    clipboard_ = std::move(gathered);
    updateChrome();
}

void PreviewWindow::cutSelection() {
    const std::size_t before = clipboard_.size();
    copySelection();
    if (clipboard_.size() == before && clipboard_.empty()) {
        return;  // copy said why
    }
    // Lift, not extract: the gap stays. Delete does the same thing unmodified,
    // and a cut that silently closed the gap would move every clip after it --
    // which is a different edit from the one somebody asked for.
    timeline_->removeSelected(false);
}

void PreviewWindow::pasteAtPlayhead() {
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr) {
        return;
    }
    if (clipboard_.empty()) {
        app::say(this, "Paste", "Nothing has been copied yet.");
        return;
    }

    // Fresh ids for everything, allocated here because only the project can:
    // the command runs against a sequence. Links are remapped rather than
    // reused, so a pasted picture-and-sound pair is linked to each other and
    // not to the clips they were copied from.
    std::map<std::uint64_t, model::LinkId> relinked;
    std::vector<edit::PastedClip> placing;
    placing.reserve(clipboard_.size());
    for (const Copied& copied : clipboard_) {
        model::Clip fresh = copied.clip;
        fresh.id = document_.project().ids().next<model::ClipTag>();
        if (copied.clip.link.isValid()) {
            auto found = relinked.find(copied.clip.link.value());
            if (found == relinked.end()) {
                found = relinked
                            .emplace(copied.clip.link.value(),
                                     document_.project().ids().next<model::LinkTag>())
                            .first;
            }
            fresh.link = found->second;
        }
        const time::RationalTime at = position_ + copied.offset;
        fresh.timelineRange = time::TimeRange{at, copied.clip.timelineRange.duration()};
        placing.push_back(edit::PastedClip{copied.track, std::move(fresh)});
    }

    auto built = edit::makePasteClips(document_.project(), sequence->id(), placing);
    if (!built) {
        app::warn(this, "Paste", QString::fromStdString(built.error().toString()));
        return;
    }
    document_.commands().execute(document_.project(), std::move(*built));
    document_.commands().breakMerge();
    renderCache_.clear();
    afterEdit();
}

void PreviewWindow::largestSourceSize(std::int32_t& width, std::int32_t& height) const {
    width = 0;
    height = 0;
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr) {
        return;
    }
    for (const model::Track& track : sequence->videoTracks()) {
        for (const model::Clip& clip : track.clips()) {
            if (clip.nested.isValid() || clip.graphic.isSet()) {
                continue;  // generated, so it has no size of its own to match
            }
            const model::MediaRef* ref = document_.project().findMedia(clip.activeSource());
            if (ref == nullptr) {
                continue;
            }
            const media::VideoStreamInfo* video = ref->info.primaryVideo();
            if (video == nullptr) {
                continue;
            }
            if (static_cast<std::int64_t>(video->width) * video->height >
                static_cast<std::int64_t>(width) * height) {
                width = video->width;
                height = video->height;
            }
        }
    }
}

void PreviewWindow::applyFrameSize(std::int32_t width, std::int32_t height) {
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr) {
        return;
    }

    // Both zero is the "Custom…" entry, which is a request to be asked rather
    // than a size. Asked here and not in the chrome: the chrome knows what the
    // list contains, not how to prompt for something that is not on it.
    if (width <= 0 || height <= 0) {
        bool ok = false;
        const QString typed = QInputDialog::getText(
            this, "Frame size",
            "Width × height, in pixels — for example 1920x1080:", QLineEdit::Normal,
            QString("%1x%2").arg(sequence->width()).arg(sequence->height()), &ok);
        if (!ok) {
            return;
        }
        const QStringList parts =
            typed.trimmed().split(QRegularExpression("[^0-9]+"), Qt::SkipEmptyParts);
        if (parts.size() != 2) {
            app::say(this, "Frame size",
                     "That did not read as a size. Two numbers, width first — 1920x1080.");
            return;
        }
        width = parts[0].toInt();
        height = parts[1].toInt();
    }

    if (width == sequence->width() && height == sequence->height()) {
        // Picking the size it already is is not a mistake worth a dialog: the
        // dropdown shows the current size selected, so choosing it again is the
        // most ordinary thing somebody can do with an open list.
        return;
    }

    auto built = edit::makeResizeSequence(document_.project(), sequence->id(), width, height);
    if (!built) {
        app::warn(this, "Frame size", QString::fromStdString(built.error().toString()));
        updateChrome();
        return;
    }
    document_.commands().execute(document_.project(), std::move(*built));
    document_.commands().breakMerge();
    // Every cached frame was composited at the old size.
    renderCache_.clear();
    afterEdit();
}

void PreviewWindow::frameSizeMenu() {
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr) {
        return;
    }

    std::int32_t sourceWidth = 0;
    std::int32_t sourceHeight = 0;
    largestSourceSize(sourceWidth, sourceHeight);

    const chrome::FrameSizeChoice chosen =
        chrome::frameSizeMenu(sequence->width(), sequence->height(), sourceWidth, sourceHeight);
    if (!chosen.chosen) {
        return;
    }
    // Custom is spelled as a zero size, which is what applyFrameSize prompts
    // for -- so the menu and the dropdown reach the same code by the same
    // route.
    applyFrameSize(chosen.custom ? 0 : chosen.width, chosen.custom ? 0 : chosen.height);
}

void PreviewWindow::frameRateMenu() {
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr) {
        return;
    }

    bool hasClips = false;
    for (const auto* list : {&sequence->videoTracks(), &sequence->audioTracks()}) {
        for (const model::Track& track : *list) {
            if (!track.isEmpty()) {
                hasClips = true;
                break;
            }
        }
        if (hasClips) {
            break;
        }
    }

    // The rate of the biggest picture on the timeline, the same clip
    // frameSizeMenu would offer to match -- so "match the footage" means the
    // same footage in both menus. Only asked for on an empty sequence: once
    // there are clips the menu is explaining a refusal, not offering a rate.
    time::Rational sourceRate{};
    if (!hasClips) {
        std::int64_t sourceArea = 0;
        for (const model::Track& track : sequence->videoTracks()) {
            for (const model::Clip& clip : track.clips()) {
                if (clip.nested.isValid() || clip.graphic.isSet()) {
                    continue;
                }
                const model::MediaRef* ref = document_.project().findMedia(clip.activeSource());
                const media::VideoStreamInfo* video =
                    ref != nullptr ? ref->info.primaryVideo() : nullptr;
                if (video == nullptr) {
                    continue;
                }
                const auto area = static_cast<std::int64_t>(video->width) * video->height;
                if (area > sourceArea) {
                    sourceArea = area;
                    sourceRate = video->frameRate;
                }
            }
        }
    }

    const chrome::FrameRateChoice chosen =
        chrome::frameRateMenu(sequence->frameRate(), sourceRate, hasClips);
    if (!chosen.chosen) {
        return;
    }

    auto built = edit::makeConformSequence(document_.project(), sequence->id(), chosen.rate,
                                           sequence->width(), sequence->height());
    if (!built) {
        app::warn(this, "Frame rate", QString::fromStdString(built.error().toString()));
        return;
    }
    document_.commands().execute(document_.project(), std::move(*built));
    document_.commands().breakMerge();
    renderCache_.clear();
    afterEdit();
}

void PreviewWindow::deliveryMenu() {
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr) {
        return;
    }
    const model::Sequence::Output current = sequence->output();
    const chrome::DeliveryChoice chosen =
        chrome::deliveryMenu(current.transfer, current.highlightKnee);
    model::Sequence::Output wanted = current;
    if (chosen.transfer.has_value()) {
        wanted.transfer = *chosen.transfer;
    } else if (chosen.highlightKnee.has_value()) {
        wanted.highlightKnee = *chosen.highlightKnee;
    } else {
        return;
    }
    setDelivery(wanted);
}

bool PreviewWindow::setDelivery(const model::Sequence::Output& output) {
    if (Status set = commands::setDelivery(editContext(), output); !set) {
        app::warn(this, "Delivery", QString::fromStdString(set.error().toString()));
        return false;
    }
    // Curves, secondaries and LUTs are all baked against the delivery
    // curve, so every cached frame was made for the old one.
    afterEdit();
    return true;
}

std::int32_t PreviewWindow::detectScenes() {
    // Checked here as well as in the command, because the command answers zero
    // to both "nothing to analyse" and "nothing found" and those need
    // different sentences. Only a piece of decoded picture has scene changes
    // in it: a shape, a title and a nested sequence are generated rather than
    // shot, and a sound has no picture at all.
    const commands::Context context = editContext();
    const model::Sequence* sequence = context.sequence();
    const model::Track* on = sequence != nullptr ? sequence->findTrack(context.track) : nullptr;
    const model::Clip* selected = context.selectedClip();
    if (selected == nullptr || on == nullptr || on->kind() != model::TrackKind::Video ||
        selected->nested.isValid() || selected->graphic.isSet()) {
        app::say(this, "Detect cuts", "Select a video clip to look for cuts in first.");
        return 0;
    }

    // The dialog is the window's, and the operation's only view of it is a
    // question it asks once a frame: keep going?
    QProgressDialog progress("Looking for scene changes\u2026", "Cancel", 0, 1, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    const std::int32_t cuts =
        commands::detectScenes(editContext(), [&](std::int64_t done, std::int64_t total) {
            progress.setMaximum(static_cast<int>(total));
            progress.setValue(static_cast<int>(done));
            QCoreApplication::processEvents();
            return !progress.wasCanceled();
        });
    const bool canceled = progress.wasCanceled();
    progress.reset();
    if (cuts > 0) {
        afterEdit();
        app::say(this, "Detect cuts",
                 cuts == 1 ? QStringLiteral("Made 1 cut in this clip.")
                           : QStringLiteral("Made %1 cuts in this clip.").arg(cuts));
        return cuts;
    }
    // Nothing happened, and from a menu that is indistinguishable from a menu
    // item that does not work. The two ways of getting here mean different
    // things, so they are not given the same sentence -- and a cancel is
    // somebody's own decision, which needs no dialog to confirm it back to
    // them.
    if (!canceled) {
        app::say(this, "Detect cuts", "No scene changes were found in this clip.");
    }
    return cuts;
}

Status PreviewWindow::openProject(const std::string& path, Sharing sharing) {
    auto opened = document_.read(path, sharing);
    if (!opened) {
        return opened.error();
    }
    document_.releaseLock();
    const bool readOnly = opened->readOnly;
    model::Project project = opened->loaded.project;
    adopt(std::move(project), std::move(opened->loaded), path, readOnly);
    document_.takeLock();
    updateTitle();
    return {};
}

void PreviewWindow::newProject() {
    model::Project fresh = model::newProject();
    adopt(std::move(fresh), io::LoadedProject{}, {});
}

void PreviewWindow::adopt(model::Project project, io::LoadedProject loaded, std::string path,
                          bool readOnly) {
    document_.autosave();
    stop();
    document_.adopt(std::move(project), std::move(loaded), std::move(path));
    document_.setReadOnly(readOnly);
    sequenceId_ = document_.project().activeSequence();
    position_ = time::RationalTime{0, document_.project().findSequence(sequenceId_)->frameRate()};
    selectedTrack_ = model::TrackId{};
    selectedClip_ = model::ClipId{};
    // A frame cached from the project that has gone would be served for the
    // new one: the recipe covers what is in a sequence, not which project
    // it came from.
    renderCache_.clear();
    // Bound before the media is opened as well as after, so that a project
    // whose files cannot be read still leaves every panel pointed at it.
    // Otherwise the failure below shows a window full of panels describing
    // the project that has gone.
    rebindSequence();
    if (Status opened = openMedia(); !opened) {
        // The project is loaded and the window is bound to it; what failed
        // is reading its files. Said plainly rather than swallowed: a
        // timeline of clips that draw nothing is a puzzle.
        app::warn(this, "Open", QString::fromStdString(opened.error().toString()));
    }
    effects_->setSelection(model::TrackId{}, model::ClipId{});
    timeline_->setCachedSpans({});
    updateTitle();
    refresh();
    monitor_->update();
    // Last, and announced: the panels are bound and the marks have somewhere
    // to land. A project that opens onto a black monitor because its footage
    // has moved should say so rather than leave it to be worked out.
    checkMissingMedia(true);
}

bool PreviewWindow::save() {
    if (document_.path().empty()) {
        return saveAs();
    }
    if (Status written = document_.save(); !written) {
        if (document_.isReadOnly()) {
            // Said on stderr and in the title bar, never in a dialog.
            //
            // This one was the worst offender: a project with a stale lock
            // beside it -- left by a killed process, or by a test -- turned
            // every Ctrl+S into a box somebody had to dismiss before they
            // could carry on. The window title already carries
            // "[read only]", which is where a state belongs; a refusal does
            // not also need to interrupt.
            std::fprintf(stderr, "zaro: %s\n", written.error().message().c_str());
            return false;
        }
        app::warn(this, "Save", QString::fromStdString(written.error().toString()));
        return false;
    }
    updateTitle();
    return true;
}

Result<std::string> PreviewWindow::saveNewVersion() {
    auto next = document_.saveNewVersion();
    if (next) {
        updateTitle();
    }
    return next;
}

void PreviewWindow::openVersionMenu() {
    if (document_.path().empty()) {
        app::say(this, "Version", "This project has not been saved yet.");
        return;
    }
    QMenu menu;
    std::map<QAction*, std::string> paths;
    for (const std::string& version : io::versionsOf(document_.path())) {
        const bool current = version == document_.path();
        QAction* action = menu.addAction(
            QString::fromStdString(std::filesystem::path{version}.filename().string()));
        action->setCheckable(true);
        action->setChecked(current);
        action->setEnabled(!current);
        paths.emplace(action, version);
    }
    if (paths.empty()) {
        menu.addAction("No other versions")->setEnabled(false);
    }
    QAction* chosen = menu.exec(QCursor::pos());
    const auto found = paths.find(chosen);
    if (found == paths.end()) {
        return;
    }
    if (Status opened = openProject(found->second); !opened) {
        app::warn(this, "Open", QString::fromStdString(opened.error().toString()));
    }
}

void PreviewWindow::openDialog() {
    const QString chosen =
        QFileDialog::getOpenFileName(this, "Open project", {}, "CutReel projects (*.cutreel)");
    if (chosen.isEmpty()) {
        return;
    }
    openChosen(chosen.toStdString());
}

void PreviewWindow::openChosen(const std::string& path) {
    // Somebody else's lock is a question, not a refusal: often enough the
    // answer is "let me look at it anyway", and often enough the other
    // machine went home hours ago.
    if (auto held = io::readLock(path); held && !io::isOurs(*held) && !io::isStale(*held)) {
        QMessageBox ask{this};
        ask.setWindowTitle("Open project");
        ask.setText(
            QString("%1 has this project open on %2.")
                .arg(QString::fromStdString(held->user), QString::fromStdString(held->host)));
        ask.setInformativeText(
            "Opening it read-only lets you look without saving over "
            "their work.");
        QPushButton* readOnly = ask.addButton("Open Read-Only", QMessageBox::AcceptRole);
        QPushButton* takeOver = ask.addButton("Take Over", QMessageBox::DestructiveRole);
        ask.addButton(QMessageBox::Cancel);
        ask.exec();
        Sharing sharing = Sharing::Exclusive;
        if (ask.clickedButton() == readOnly) {
            sharing = Sharing::ReadOnly;
        } else if (ask.clickedButton() == takeOver) {
            sharing = Sharing::TakeOver;
        } else {
            return;
        }
        if (Status opened = openProject(path, sharing); !opened) {
            app::warn(this, "Open", QString::fromStdString(opened.error().toString()));
        }
        return;
    }
    if (Status opened = openProject(path); !opened) {
        app::warn(this, "Open", QString::fromStdString(opened.error().toString()));
    }
}

bool PreviewWindow::saveAs() {
    const QString chosen = QFileDialog::getSaveFileName(
        this, "Save project",
        QString::fromStdString(document_.path().empty() ? "project.cutreel" : document_.path()),
        "CutReel projects (*.cutreel)");
    if (chosen.isEmpty()) {
        return false;
    }
    if (Status written = document_.saveAs(chosen.toStdString()); !written) {
        app::warn(this, "Save", QString::fromStdString(written.error().toString()));
        return false;
    }
    updateTitle();
    return true;
}

void PreviewWindow::setProjectPath(std::string path) {
    document_.setPath(std::move(path));
    updateTitle();
}

void PreviewWindow::updateTitle() {
    const QString name = document_.path().empty()
                             ? QString{"Untitled"}
                             : QFileInfo(QString::fromStdString(document_.path())).fileName();
    // Said in the title, because read-only is a fact about the whole
    // window and finding out at the moment of saving is finding out too
    // late.
    setWindowTitle(QString("%1%2%3 — %4 %5")
                       .arg(name, document_.commands().isModified() ? "*" : "",
                            document_.isReadOnly() ? " [read only]" : "", appName(),
                            versionText()));
    if (bars_.statusLeft != nullptr) {
        updateChrome();
    }
}

void PreviewWindow::matchFrame() {
    auto found = commands::frameToMatch(editContext());
    if (!found) {
        return;
    }
    const model::MediaRef* ref = document_.project().findMedia(found->media);
    if (ref == nullptr) {
        return;
    }
    source_->showFrame(*ref, found->at);
    setSourceShown(true);
}

void PreviewWindow::makeSubclip() {
    const auto range = source_->markedRange();
    if (!range || !source_->media().isValid()) {
        return;
    }
    if (commands::makeSubclip(editContext(), source_->media(), *range)) {
        bin_->refresh();
    }
}

void PreviewWindow::replaceSelectedSource(model::MediaRefId media) {
    if (Status replaced = commands::replaceSelectedSource(editContext(), media); !replaced) {
        app::warn(this, "Replace footage", QString::fromStdString(replaced.error().toString()));
        return;
    }
    afterEdit();
}

void PreviewWindow::syncAngles(bool byEar) {
    auto report = commands::syncAngles(editContext(), byEar);
    if (!report) {
        app::warn(this, "Multicam", QString::fromStdString(report.error().toString()));
        return;
    }
    if (report->synced > 0) {
        afterEdit();
    }
    lastSyncCount_ = report->synced;
    lastSyncSkipped_ = static_cast<std::int32_t>(report->skipped.size());
    if (report->skipped.empty()) {
        return;
    }
    QStringList lines;
    for (const std::string& line : report->skipped) {
        lines.append(QString::fromStdString(line));
    }
    app::say(this, "Multicam",
             QString("Synced %1 of %2 angles.\n\nNot synced:\n%3")
                 .arg(report->synced)
                 .arg(report->total)
                 .arg(lines.join("\n")));
}

void PreviewWindow::updateCacheBar() {
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr) {
        return;
    }
    const time::TimeRange visible = timeline_->layout().visibleRange(sequence->frameRate());
    timeline_->setCachedSpans(render::cachedSpans(renderCache_, &document_.project(), *sequence,
                                                  visible, timeline_->width()));
}

void PreviewWindow::renderVisibleRange() {
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr || media_ == nullptr) {
        return;
    }
    const time::TimeRange visible = timeline_->layout().visibleRange(sequence->frameRate());
    if (visible.isEmpty()) {
        return;
    }
    QProgressDialog progress("Rendering…", "Cancel", 0,
                             static_cast<int>(visible.duration().frames()), this);
    progress.setWindowModality(Qt::WindowModal);
    // A pre-render of a few frames is not worth a dialog appearing and
    // vanishing; one of a few hundred is.
    progress.setMinimumDuration(400);
    render::RenderGraph graph{*media_};
    graph.setProject(&document_.project());
    graph.setTextRasterizer(&text_);
    graph.setRenderCache(&renderCache_);
    auto stats = render::prerender(graph, renderCache_, &document_.project(), *sequence, visible,
                                   [&progress](std::int32_t done, std::int32_t total) {
                                       progress.setMaximum(total);
                                       progress.setValue(done);
                                       QCoreApplication::processEvents();
                                       return !progress.wasCanceled();
                                   });
    progress.reset();
    if (!stats) {
        app::warn(this, "Render", QString::fromStdString(stats.error().toString()));
        return;
    }
    updateCacheBar();
}

Status PreviewWindow::openMedia() {
    auto opened = platform::ffmpeg::ProjectMediaSource::open(document_.project());
    if (!opened) {
        return opened.error();
    }
    media_ = std::move(*opened);
    // The project this opened media for may be a different one -- adopt
    // calls through here -- so the panels are pointed at it again, along
    // with the monitor.
    rebindSequence();
    effects_->setAudioSource(media_.get());
    source_->setProvider(media_.get());
    startWaveforms();
    bars_.scrubber->setRange(0, static_cast<int>(liveSequence()->duration().frames()));
    refresh();
    return {};
}

void PreviewWindow::setPosition(const time::RationalTime& position) {
    const std::int64_t last = std::max<std::int64_t>(0, liveSequence()->duration().frames() - 1);
    const std::int64_t clamped = std::clamp<std::int64_t>(position.frames(), 0, last);
    position_ = time::RationalTime{clamped, liveSequence()->frameRate()};
    monitor_->setPosition(position_);
    timeline_->setPlayhead(position_);
    // The panel shows values at the playhead and writes keyframes there, so
    // it has to know where the playhead is.
    effects_->setPosition(position_);
    refreshInstruments();
    refresh();
}

void PreviewWindow::step(std::int64_t frames) {
    stop();
    setPosition(position_ + time::RationalTime{frames, liveSequence()->frameRate()});
}

std::string PreviewWindow::keystrokeOf(const QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Control:
        case Qt::Key_Shift:
        case Qt::Key_Alt:
        case Qt::Key_Meta:
            return {};  // a modifier on its own is not a keystroke
        default:
            break;
    }
    const QKeySequence sequence{event->keyCombination()};
    auto normalised =
        ui::normaliseShortcut(sequence.toString(QKeySequence::PortableText).toStdString());
    return normalised ? *normalised : std::string{};
}

void PreviewWindow::keyPressEvent(QKeyEvent* event) {
    // Whatever the keymap says this keystroke is, whether or not it has a
    // menu item behind it. There used to be a table here of "the bindings
    // that are not menu items" and a switch for playback underneath it,
    // which meant three places knew what a key did and only one of them
    // could be changed. Now there is one: the keymap.
    const std::string keystroke = keystrokeOf(event);
    const std::string wanted =
        keystroke.empty() ? std::string{} : actions_.keymap().actionFor(keystroke);
    if (!wanted.empty() && trigger(wanted)) {
        return;
    }
    QWidget::keyPressEvent(event);
}

bool PreviewWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == monitor_ && event->type() == QEvent::Resize) {
        maskOverlay_->setGeometry(monitor_->rect());
        titleOverlay_->setGeometry(monitor_->rect());
        viewerOverlay_->setGeometry(monitor_->rect());
    }
    // Right-click on the picture. Filtered here rather than given to the
    // monitor because the monitor renders and knows nothing about the project
    // or where a file should go, and everything this menu does is the window's.
    if (watched == monitor_ && event->type() == QEvent::ContextMenu) {
        showProgramMenu(static_cast<QContextMenuEvent*>(event)->globalPos());
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void PreviewWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (droppedProjectPath(event->mimeData()).empty()) {
        // Footage let go of over the window's furniture rather than over the
        // bin. Refused rather than imported: the bin is where files go, it is
        // the largest thing on the left of the window, and a drop that landed
        // on the tool bar by half a pixel should miss rather than do something
        // slightly different.
        event->ignore();
        return;
    }
    // Copy rather than move: the project file stays where it is. A move would
    // ask the file manager to delete it once this returns.
    event->setDropAction(Qt::CopyAction);
    event->acceptProposedAction();
}

void PreviewWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (droppedProjectPath(event->mimeData()).empty()) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->acceptProposedAction();
}

void PreviewWindow::dropEvent(QDropEvent* event) {
    const std::string path = droppedProjectPath(event->mimeData());
    if (path.empty()) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    openDropped(QString::fromStdString(path));
}

void PreviewWindow::openDropped(const QString& path) {
    // On the next turn of the event loop, never inside the handler. This runs
    // in the middle of the window system's drag session -- the file manager is
    // blocked waiting for the drop to come back -- and opening a project tears
    // down and rebinds every panel in the window, one of which is very often
    // the widget whose handler is still on the stack.
    QTimer::singleShot(0, this, [this, path] { openChosen(path.toStdString()); });
}

void PreviewWindow::importDropped(const QStringList& paths, const QPoint& at) {
    // Past the end of the drag session, like a project. More so, in fact: an
    // import probes every file, and the file manager is blocked until this
    // handler comes back.
    QTimer::singleShot(0, this, [this, paths, at] {
        // The import and the placement under one group: letting go of a folder
        // of rushes is one gesture, and a person who did it and changed their
        // mind should press undo once, not once per file plus once per import.
        //
        // It still comes out as two steps rather than one, and that is a
        // property of the history rather than of this call: importing snapshots
        // the project, placing snapshots the sequence, and two different kinds
        // of snapshot cannot fold into each other. Two is the floor; without
        // the group it was two per file.
        const edit::CommandStack::Group step{document_.commands()};
        const app::ProjectBin::Imported brought = bin_->importMedia(paths);
        if (brought.media.empty()) {
            // Nothing readable in what was let go of. Said where an import
            // says everything else, rather than in a dialog: it is the answer
            // to a gesture, and a gesture does not deserve a box to dismiss.
            bin_->note(QStringLiteral("Nothing to import"));
            return;
        }
        timeline_->placeImported(brought.media, at);
        // The set of media changed, so which of it is missing has to be
        // recomputed -- quietly, as an import does from the pane.
        checkMissingMedia();
        updateTitle();
    });
}

void PreviewWindow::closeEvent(QCloseEvent* event) {
    // Handed back on the way out, so the next person is not told a machine
    // that has gone home still has it.
    releaseLock();
    // Never a dialog on the way out.
    //
    // "You have unsaved changes -- save?" is a question whose answer is
    // almost always yes, asked at the moment somebody has already decided
    // to leave. Writing the recovery file instead means quitting is always
    // instant and never loses anything: the next open finds the autosave
    // and offers it back. It also keeps the promise that the file somebody
    // last chose to save stays as they left it.
    autosave();
    saveWorkspace();
    shutDown();
    QWidget::closeEvent(event);
}

QAction* PreviewWindow::menuItem(QMenu* menu, const char* actionId) {
    QAction* action = actions_.action(actionId);
    menu->addAction(action);
    return action;
}

void PreviewWindow::bindPlaybackActions() {
    bindAction("play-pause", [this] { togglePlay(); });
    bindAction("shuttle-back", [this] {
        playback_.transport().pressJ();
        startIfPlaying();
    });
    bindAction("shuttle-stop", [this] {
        playback_.transport().pressK();
        stop();
    });
    bindAction("shuttle-forward", [this] {
        playback_.transport().pressL();
        startIfPlaying();
    });
    bindAction("step-back", [this] { step(-1); });
    bindAction("step-forward", [this] { step(1); });
    bindAction("go-to-start", [this] {
        stop();
        setPosition(time::RationalTime{0, liveSequence()->frameRate()});
    });
    bindAction("go-to-end", [this] {
        stop();
        setPosition(liveSequence()->duration());
    });
    bindAction("mark-in", [this] { doMarkIn(); });
    bindAction("mark-out", [this] { doMarkOut(); });
    bindAction("insert-from-source", [this] { doInsert(); });
    bindAction("overwrite-from-source", [this] { doOverwrite(); });
    bindAction("source-back", [this] { doSourceBack(); });
    bindAction("source-forward", [this] { doSourceForward(); });
}

void PreviewWindow::bindCommands() {
    actions_.bind("new-project", [this] { newProject(); });
    actions_.bind("open-project", [this] { openDialog(); });
    actions_.bind("save-project", [this] { static_cast<void>(save()); });
    actions_.bind("save-project-as", [this] { static_cast<void>(saveAs()); });
    actions_.bind("save-version", [this] {
        auto saved = saveNewVersion();
        if (!saved) {
            app::say(this, "Version", QString::fromStdString(saved.error().message()));
            return;
        }
        // Said out loud: the window title changes too, but a version
        // that appeared to do nothing is one people press twice.
        app::say(
            this, "Version",
            QString("Now working in %1")
                .arg(QString::fromStdString(std::filesystem::path{*saved}.filename().string())));
    });
    actions_.bind("open-version", [this] { openVersionMenu(); });
    actions_.bind("import-media", [this] { bin_->importFiles(); });
    actions_.bind("browse-media", [this] { browseMedia(); });
    actions_.bind("relink-media", [this] { relinkDialog(); });
    actions_.bind("consolidate-media", [this] { consolidateDialog(); });
    actions_.bind("export-sequence", [this] { exportDialog(); });
    actions_.bind("export-otio", [this] { exportOtio(); });
    actions_.bind("export-premiere", [this] { exportPremiere(); });
    actions_.bind("import-premiere", [this] { importPremiere(); });
    actions_.bind("export-finalcut", [this] { exportFinalCut(); });
    actions_.bind("import-finalcut", [this] { importFinalCut(); });
    actions_.bind("save-template", [this] { saveTemplateDialog(); });
    actions_.bind("place-template", [this] { placeTemplateDialog(); });
    actions_.bind("close-window", [this] { close(); });
    actions_.bind("undo", [this] { timeline_->undo(); });
    actions_.bind("redo", [this] { timeline_->redo(); });
    actions_.bind("select-all", [this] { timeline_->selectAll(); });
    actions_.bind("detect-scenes", [this] { static_cast<void>(detectScenes()); });
    actions_.bind("match-frame", [this] { matchFrame(); });
    actions_.bind("make-subclip", [this] { makeSubclip(); });
    actions_.bind("proxies", [this] { proxyMenu(); });
    actions_.bind("multicam", [this] { multicamMenu(); });
    actions_.bind("captions", [this] { captionsMenu(); });
    actions_.bind("razor", [this] { timeline_->razorAtPlayhead(); });
    actions_.bind("add-dissolve", [this] { timeline_->addDissolveAtPlayhead(); });
    actions_.bind("render-range", [this] { renderMenu(); });
    // One binding per key, here, rather than a second copy in the timeline's
    // own key handler. Bound on the window so they work wherever the focus is:
    // Delete used to do nothing unless the timeline had been clicked first.
    actions_.bind("copy-clips", [this] { copySelection(); });
    actions_.bind("cut-clips", [this] { cutSelection(); });
    actions_.bind("paste-clips", [this] { pasteAtPlayhead(); });
    actions_.bind("tool-select", [this] { timeline_->setTool(TimelineWidget::Tool::Select); });
    actions_.bind("tool-blade", [this] { timeline_->setTool(TimelineWidget::Tool::Blade); });
    actions_.bind("tool-trim", [this] { timeline_->setTool(TimelineWidget::Tool::Trim); });
    actions_.bind("tool-slip", [this] { timeline_->setTool(TimelineWidget::Tool::Slip); });
    actions_.bind("tool-hand", [this] { timeline_->setTool(TimelineWidget::Tool::Hand); });
    actions_.bind("toggle-snap", [this] {
        timeline_->setSnapEnabled(!timeline_->snapEnabled());
        updateChrome();
    });
    actions_.bind("delete-selected", [this] { timeline_->removeSelected(false); });
    actions_.bind("ripple-delete", [this] { timeline_->removeSelected(true); });
    actions_.bind("frame-size", [this] { frameSizeMenu(); });
    actions_.bind("frame-rate", [this] { frameRateMenu(); });
    actions_.bind("delivery", [this] { deliveryMenu(); });
    actions_.bind("loudness", [this] { loudnessMenu(); });
    actions_.bind("show-transcript", [this] { showTranscript(); });
    actions_.bind("fit-music", [this] {
        const model::Sequence* sequence = liveSequence();
        if (sequence == nullptr) {
            return;
        }
        // The picture's length, less wherever the music starts: what
        // has to be filled is what is left after it comes in.
        const model::Track* track = sequence->findTrack(selectedTrack_);
        const model::Clip* clip = track != nullptr ? track->find(selectedClip_) : nullptr;
        const double from = clip != nullptr ? clip->start().toSecondsDouble() : 0.0;
        const double wanted = sequence->duration().toSecondsDouble() - from;
        auto plan = remixSelectedTo(wanted);
        if (!plan) {
            app::say(this, "Fit music", QString::fromStdString(plan.error().message()));
            return;
        }
        app::say(this, "Fit music",
                 QString("Cut %1 beats out at %2s, now %3s long.")
                     .arg(plan->beatsRemoved)
                     .arg(plan->cutAt, 0, 'f', 2)
                     .arg(plan->seconds, 0, 'f', 2));
    });
    actions_.bind("add-title", [this] { addTitle(); });
    actions_.bind("title-fade-in", [this] { animateTitle(commands::TitleMotion::FadeIn); });
    actions_.bind("title-fade-out", [this] { animateTitle(commands::TitleMotion::FadeOut); });
    actions_.bind("title-slide-on", [this] { animateTitle(commands::TitleMotion::SlideOn); });
    actions_.bind("title-typewriter", [this] { animateTitle(commands::TitleMotion::Typewriter); });
    actions_.bind("add-marker", [this] { timeline_->addMarkerAtPlayhead(); });
    actions_.bind("next-marker", [this] { doNextMarker(); });
    actions_.bind("previous-marker", [this] { doPreviousMarker(); });
    actions_.bind("resolve-comment", [this] { static_cast<void>(toggleCommentHere()); });
    actions_.bind("export-review", [this] { exportReviewNotes(); });
    actions_.bindToggle("compare", [this](bool on) {
        // Turning it on takes the frame showing now as the reference. That
        // is the gesture: somebody looks at a shot they like and says
        // "against this" -- asking them to nominate one first would be a
        // step between the thought and the thing.
        setComparing(on, on ? position_ : referenceAt_);
    });
    actions_.bind("match-shot", [this] { matchShot(); });
    actions_.bind("zoom-in", [this] { timeline_->zoomBy(1.4); });
    actions_.bind("zoom-out", [this] { timeline_->zoomBy(1.0 / 1.4); });
    actions_.bind("zoom-fit", [this] { timeline_->zoomToFit(); });
    actions_.bindToggle("safe-guides", [this](bool on) {
        viewerOverlay_->setGuides(on);
        if (bars_.guidesButton != nullptr) {
            bars_.guidesButton->setChecked(on);
        }
    });
    actions_.bind("reset-panels", [this] { setWorkspace(workspace_); });
    actions_.bind("hotkeys", [this] { showHotkeys(); });
    actions_.bind("about", [this] {
        QMessageBox::about(this, QString("About %1").arg(appName()),
                           QString("%1 %2 — a non-linear editor.\n\n"
                                   "C++20, Qt 6, FFmpeg, GPU compositing on Qt RHI.")
                               .arg(appName(), versionText()));
    });
}

void PreviewWindow::buildMenus() {
    bars_.menuBar = chrome::buildMenuBar(
        this, actions_, kWorkspaces, bars_.workspaceActions,
        [this](const QString& name) { setWorkspace(name); },
        [this](QMenuBar* bar) {
            QMenu* window = bar->addMenu("Window");
            panelAction(window, "Project Bin", [this] { return bin_; });
            panelAction(window, "Effect Controls",
                        [this] { return static_cast<QWidget*>(effects_); });
            panelAction(window, "Scopes", [this] { return static_cast<QWidget*>(scopes_); });
            panelAction(window, "Audio Mixer", [this] { return static_cast<QWidget*>(mixer_); });
            window->addSeparator();
            menuItem(window, "reset-panels");
            QMenu* help = bar->addMenu("Help");
            menuItem(help, "hotkeys");
            menuItem(help, "about");
        });
}

chrome::Hooks PreviewWindow::chromeHooks() {
    chrome::Hooks hooks;
    hooks.chooseWorkspace = [this](const QString& name) { setWorkspace(name); };
    hooks.chooseTool = [this](app::TimelineWidget::Tool tool) { timeline_->setTool(tool); };
    hooks.showSource = [this](bool on) { setSourceShown(on); };
    hooks.showProgram = [this](bool on) { setProgramShown(on); };
    hooks.setGuides = [this](bool on) { viewerOverlay_->setGuides(on); };
    hooks.setSnapEnabled = [this](bool on) { timeline_->setSnapEnabled(on); };
    hooks.setZoomFraction = [this](double fraction) { timeline_->setZoomFraction(fraction); };
    hooks.setTrackHeightFraction = [this](double fraction) {
        timeline_->setTrackHeightFraction(fraction);
    };
    hooks.queueRender = [this] { deliver_->queueCurrent(); };
    hooks.chooseFrameSize = [this](std::int32_t width, std::int32_t height) {
        applyFrameSize(width, height);
    };
    hooks.toggleRendering = [this] {
        deliver_->toggleRendering();
        updateChrome();
    };
    return hooks;
}

QWidget* PreviewWindow::buildToolBar() {
    return chrome::buildToolBar(this, bars_, actions_, chromeHooks(), kWorkspaces, kSupportUrl);
}

QWidget* PreviewWindow::buildViewerBar() {
    QWidget* bar = chrome::buildViewerBar(this, bars_, actions_, chromeHooks());
    syncViewers();
    return bar;
}

QWidget* PreviewWindow::buildTimelinePane() {
    return chrome::buildTimelinePane(this, bars_, actions_, chromeHooks(), timeline_);
}

void PreviewWindow::syncViewers() {
    source_->setVisible(sourceShown_);
    monitor_->setVisible(programShown_);
    bars_.noMonitorLabel->setVisible(!sourceShown_ && !programShown_);
    // These do not re-enter: setChecked only emits when the value moves,
    // and by here it already is what it is being set to.
    bars_.sourceTab->setChecked(sourceShown_);
    bars_.programTab->setChecked(programShown_);
    bars_.sourceTab->setIcon(app::icons::toolIcon(
        sourceShown_ ? app::icons::Glyph::CheckCircle : app::icons::Glyph::Circle, 13));
    bars_.programTab->setIcon(app::icons::toolIcon(
        programShown_ ? app::icons::Glyph::CheckCircle : app::icons::Glyph::Circle, 13));
}

void PreviewWindow::setSourceShown(bool on) {
    sourceShown_ = on;
    syncViewers();
}

void PreviewWindow::setProgramShown(bool on) {
    programShown_ = on;
    syncViewers();
}

QString PreviewWindow::layoutKey(const QString& workspace, const char* which) {
    // -v6: the gallery stopped being a pane of its own -- its panels moved
    // into the palette's tabs -- so the top splitter has one pane fewer.
    // -v5: the grading palette moved from the bottom splitter to the left
    // column, so a saved state restores sizes for a pane that is no longer in
    // that splitter at all.
    // -v4: Color gained the timeline pane. A layout saved before that was
    // stored with the pane hidden, and restoring it over a pane that is now
    // shown gives it zero height -- a timeline that is present, correct and
    // invisible, which reads as the feature not being there at all. Bumping
    // the key retires those layouts instead of restoring them.
    return QString("workspace/%1/%2-v6").arg(workspace, QString::fromUtf8(which));
}

namespace {

/// Where highlights start rolling off. The same four the delivery menu offers,
/// so the panel and the menu cannot come to disagree about what "roll off"
/// means. See chrome::deliveryMenu.
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

void PreviewWindow::syncColorManagement() {
    if (colorSpace_ == nullptr) {
        return;
    }
    const model::Sequence* sequence = liveSequence();
    // Written from the model, never remembered: after an undo the project is
    // the only thing that knows what these say.
    syncingColorManagement_ = true;

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
    const model::MediaRef* media = nullptr;
    if (const model::Sequence* live = sequence; live != nullptr) {
        const model::Track* track = live->findTrack(selectedTrack_);
        const model::Clip* clip = track != nullptr ? track->find(selectedClip_) : nullptr;
        if (clip != nullptr && clip->activeSource().isValid()) {
            media = document_.project().findMedia(clip->activeSource());
        }
    }
    const QString current = media != nullptr ? QString::fromStdString(media->lut.path) : QString{};
    int wantedLut = 0;
    const QDir folder{gallery_->lutFolder()};
    if (folder.exists()) {
        const QStringList cubes = folder.entryList({"*.cube"}, QDir::Files, QDir::Name);
        for (const QString& file : cubes) {
            const QString path = folder.filePath(file);
            inputLut_->addItem(file, path);
            if (path == current) {
                wantedLut = inputLut_->count() - 1;
            }
        }
    }
    // A LUT set from somewhere else, or from a folder that is no longer open,
    // still has to be shown -- otherwise the panel says "None" over footage
    // that is being transformed.
    if (!current.isEmpty() && wantedLut == 0) {
        inputLut_->addItem(QFileInfo(current).fileName(), current);
        wantedLut = inputLut_->count() - 1;
    }
    inputLut_->setCurrentIndex(wantedLut);
    inputLut_->setEnabled(media != nullptr);

    syncingColorManagement_ = false;
}

void PreviewWindow::applyInputLut(int index) {
    if (syncingColorManagement_ || index < 0) {
        return;
    }
    const model::Sequence* sequence = liveSequence();
    const model::Track* track = sequence != nullptr ? sequence->findTrack(selectedTrack_) : nullptr;
    const model::Clip* clip = track != nullptr ? track->find(selectedClip_) : nullptr;
    if (clip == nullptr || !clip->activeSource().isValid()) {
        return;
    }
    const model::MediaRef* media = document_.project().findMedia(clip->activeSource());
    if (media == nullptr) {
        return;
    }
    model::LutRef wanted;
    wanted.path = inputLut_->itemData(index).toString().toStdString();
    if (wanted.path == media->lut.path) {
        return;
    }
    if (auto built = edit::makeSetMediaGrade(document_.project(), media->id, media->color,
                                             media->wheels, wanted)) {
        document_.commands().execute(document_.project(), std::move(*built));
        document_.commands().breakMerge();
    }
    renderCache_.clear();
    monitor_->update();
    clipStrip_->refresh();
    refreshInstruments();
    updateTitle();
}

void PreviewWindow::applyDelivery() {
    if (syncingColorManagement_) {
        return;
    }
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr) {
        return;
    }
    model::Sequence::Output wanted = sequence->output();
    wanted.transfer = static_cast<media::TransferFunction>(colorSpace_->currentData().toInt());
    wanted.highlightKnee = toneMapping_->currentData().toDouble();
    if (wanted == sequence->output()) {
        return;
    }
    setDelivery(wanted);
}

void PreviewWindow::setGradeTarget(app::ColorPalette::GradeTarget target) {
    palette_->setTarget(target);
    // Which pane answers "which one" depends on the target: the shot strip
    // names cuts, the bin names files.
    const bool gradingFile = target == app::ColorPalette::GradeTarget::MediaFile;
    if (workspace_ == "Color") {
        bin_->setVisible(gradingFile);
        clipStrip_->setVisible(!gradingFile);
    }
    // The chain and the scopes are reading the other grade now, so everything
    // that shows a grade has to be asked again.
    refreshGradeChain();
    refreshInstruments();
    syncGradeTarget();
    syncColorManagement();
    // The status line names the target, so it is stale the moment this changes.
    updateChrome();
}

void PreviewWindow::syncGradeTarget() {
    if (gradeClipTab_ == nullptr) {
        return;
    }
    const bool onFile = palette_->target() == app::ColorPalette::GradeTarget::MediaFile;
    // setChecked only emits when the value moves, so this does not re-enter.
    gradeClipTab_->setChecked(!onFile);
    gradeMediaTab_->setChecked(onFile);

    QString caption;
    const model::Sequence* sequence = liveSequence();
    const model::Track* track = sequence != nullptr ? sequence->findTrack(selectedTrack_) : nullptr;
    const model::Clip* clip = track != nullptr ? track->find(selectedClip_) : nullptr;
    if (onFile) {
        // A file can be chosen in the bin without any clip being selected, so
        // this case does not require one: the file picked there wins, and the
        // selected clip's own file is the fallback.
        const model::MediaRefId wanted = palette_->media().isValid() ? palette_->media()
                                         : clip != nullptr           ? clip->activeSource()
                                                                     : model::MediaRefId{};
        const model::MediaRef* media =
            wanted.isValid() ? document_.project().findMedia(wanted) : nullptr;
        if (media == nullptr) {
            caption = tr("Open a file in the bin to grade it");
        } else {
            const QString name = media->name.empty()
                                     ? QFileInfo(QString::fromStdString(media->path)).fileName()
                                     : QString::fromStdString(media->name);
            // How many cuts inherit this edit. The number is the whole reason
            // to grade the file rather than the clip, so it is worth saying
            // before somebody drags a wheel rather than after.
            const int uses = edit::clipsUsingMedia(document_.project(), media->id);
            caption = tr("%1 · source grade · %n use(s)", nullptr, std::max(1, uses)).arg(name);
        }
    } else if (clip == nullptr) {
        caption = tr("Nothing selected");
    } else {
        caption =
            tr("%1 · instance grade")
                .arg(QString::fromStdString(clip->name.empty() ? std::string{"Clip"} : clip->name));
    }
    chrome::setElidedText(gradeTargetLabel_, caption);
}

void PreviewWindow::setWorkspace(const QString& name) {
    if (!kWorkspaces.contains(name)) {
        return;
    }
    // The arrangement of the workspace being left is remembered, so coming
    // back to it finds the splitters where they were.
    if (topSplitter_ != nullptr && !workspace_.isEmpty()) {
        QSettings settings = makeSettings();
        settings.setValue(layoutKey(workspace_, "top"), topSplitter_->saveState());
        settings.setValue(layoutKey(workspace_, "main"), mainSplitter_->saveState());
    }
    workspace_ = name;
    const bool colour = name == "Color";
    const bool audio = name == "Audio";
    const bool deliver = name == "Deliver";
    if (bars_.workspaceStack != nullptr) {
        bars_.workspaceStack->setCurrentIndex(deliver ? 1 : 0);
        bars_.actionStack->setCurrentIndex(deliver ? 1 : 0);
        if (deliver) {
            deliver_->setPlayhead(position_);
        }
    }
    // The bin and the parameter panel are both about picture; Audio has a
    // console in the middle and a channel's chain on the right, and neither
    // of those wants a clip's motion controls beside it.
    // The bin comes back in Color when the target is the file: "grade the media
    // file" needs somewhere to say *which* file, and the bin is already the
    // place this program lists them. Picking one is a double-click, the same
    // gesture that opens a file anywhere else.
    const bool gradingFile =
        colour && palette_->target() == app::ColorPalette::GradeTarget::MediaFile;
    bin_->setVisible((!colour && !deliver && !audio) || gradingFile);
    effects_->setVisible(!deliver && !audio);
    scopes_->setVisible(colour);
    mixer_->setVisible(audio);
    // Color is a different room: the gallery replaces the bin and the grade
    // chain sits over the parameters. The timeline stays, though -- a colourist
    // moves through a reel shot by shot, and the cut is what says which shot
    // comes next and how long it is. The shot strip is the quick way along it
    // and sits under the viewer; the timeline underneath is the cut itself.
    clipStrip_->setVisible(colour && !gradingFile);
    bars_.nodesBox->setVisible(colour);
    palette_->setVisible(colour);
    bars_.timelinePane->setVisible(!deliver);
    // Audio is a console: the mixer takes the centre, the loudness meter
    // and the channel's chain take the sides, and the picture stands down.
    bars_.audioSide->setVisible(audio);
    channel_->setVisible(audio);
    bars_.viewerWell->setVisible(!audio && !deliver);
    bars_.viewerBar->setVisible(!audio && !deliver);
    if (audio) {
        channel_->setTrack(mixer_->picked());
        refreshInstruments();
    }
    if (colour) {
        palette_->setSelection(selectedTrack_, selectedClip_);
        clipStrip_->setSelection(selectedTrack_, selectedClip_);
        refreshGradeChain();
        syncGradeTarget();
        syncColorManagement();
    }
    for (auto entry = bars_.workspaceTabs.constBegin(); entry != bars_.workspaceTabs.constEnd();
         ++entry) {
        entry.value()->setChecked(entry.key() == name);
    }
    for (auto entry = bars_.workspaceActions.constBegin();
         entry != bars_.workspaceActions.constEnd(); ++entry) {
        entry.value()->setChecked(entry.key() == name);
    }
    QSettings settings = makeSettings();
    if (const auto state = settings.value(layoutKey(name, "top")).toByteArray(); !state.isEmpty()) {
        topSplitter_->restoreState(state);
    }
    if (const auto state = settings.value(layoutKey(name, "main")).toByteArray();
        !state.isEmpty()) {
        mainSplitter_->restoreState(state);
    }
    updateChrome();
}

void PreviewWindow::updateChrome() {
    const model::Sequence* sequence = liveSequence();
    static const QString kToolNames[] = {"Select", "Blade", "Trim", "Slip", "Hand", "Zoom"};
    chrome::Status status;
    status.projectName =
        document_.path().empty()
            ? QString{"Untitled"}
            : QFileInfo(QString::fromStdString(document_.path())).completeBaseName();
    status.haveSequence = sequence != nullptr;
    status.modified = document_.commands().isModified();
    status.onDisk = !document_.path().empty();
    if (sequence != nullptr) {
        status.sequenceName = QString::fromStdString(sequence->name());
        status.width = sequence->width();
        status.height = sequence->height();
        largestSourceSize(status.sourceWidth, status.sourceHeight);
        status.frameRate = sequence->frameRate().toDouble();
        const bool dropFrame = time::supportsDropFrame(sequence->frameRate());
        status.durationTimecode =
            QString::fromStdString(time::timecodeFromFrames(sequence->duration().frames(),
                                                            sequence->frameRate(), dropFrame)
                                       .toString());
    }
    status.comparing = monitor_->comparing();
    status.toolIndex = static_cast<std::size_t>(timeline_->tool());
    status.toolName = kToolNames[status.toolIndex];
    status.workspace = workspace_;
    status.binItems = bin_->count();
    status.gradedClips = clipStrip_->gradedCount();
    status.totalClips = clipStrip_->count();
    status.gradeTarget = palette_->target() == app::ColorPalette::GradeTarget::MediaFile
                             ? tr("grading the media file")
                             : tr("grading the timeline clip");
    status.snapEnabled = timeline_->snapEnabled();
    status.zoomFraction = timeline_->zoomFraction();
    status.trackHeightFraction = timeline_->trackHeightFraction();
    status.inDeliver = workspace_ == "Deliver" && deliver_ != nullptr;
    if (status.inDeliver) {
        status.deliverStatus = deliver_->statusSummary();
        status.deliverRange = deliver_->rangeSummary();
        status.rendering = deliver_->rendering();
    }
    status.audioDeviceMissing = playback_.audioDeviceMissing();
    status.platformLabel = kPlatformLabel;
    chrome::refresh(bars_, status);
}

void PreviewWindow::goToStart() {
    stop();
    setPosition(time::RationalTime{0, liveSequence()->frameRate()});
}

void PreviewWindow::goToEnd() {
    stop();
    setPosition(liveSequence()->duration());
}

QSettings PreviewWindow::makeSettings() {
    // Two returns rather than a ternary: QSettings is a QObject and so is
    // neither copyable nor movable, and only a returned prvalue elides into
    // the caller's object.
    if (settingsPath_.isEmpty()) {
        return QSettings("CutReel", "CutReel");
    }
    return QSettings(settingsPath_, QSettings::IniFormat);
}

void PreviewWindow::saveWorkspace() {
    QSettings settings = makeSettings();
    settings.setValue("window/geometry", saveGeometry());
    // Per workspace, because the panels differ between them: one saved
    // arrangement restored into a different set of visible panels is a
    // collapsed bin and a mixer four pixels tall.
    settings.setValue("workspace/current", workspace_);
    settings.setValue(layoutKey(workspace_, "top"), topSplitter_->saveState());
    settings.setValue(layoutKey(workspace_, "main"), mainSplitter_->saveState());
}

void PreviewWindow::restoreWorkspace() {
    QSettings settings = makeSettings();
    // Each restored only if it was stored, so a first run gets the
    // stretch factors set above rather than a collapsed layout.
    if (const auto geometry = settings.value("window/geometry").toByteArray();
        !geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    const QString wanted = settings.value("workspace/current", "Edit").toString();
    // Always through setWorkspace, so the panels, the tabs and the splitter
    // states are one decision rather than three that can disagree.
    workspace_.clear();
    setWorkspace(kWorkspaces.contains(wanted) ? wanted : QString{"Edit"});
}

void PreviewWindow::shutDown() {
    shuttingDown_.store(true, std::memory_order_relaxed);
    stop();
    if (waveformThread_.joinable()) {
        waveformThread_.join();
    }
}

void PreviewWindow::doNextMarker() {
    if (const model::Marker* marker = liveSequence()->markerAfter(position_)) {
        stop();
        setPosition(marker->range.start());
    }
}

void PreviewWindow::doPreviousMarker() {
    if (const model::Marker* marker = liveSequence()->markerBefore(position_)) {
        stop();
        setPosition(marker->range.start());
    }
}

void PreviewWindow::placeFromSource(edit::PlaceMode mode) {
    const auto range = source_->markedRange();
    if (!range || !source_->media().isValid()) {
        return;
    }
    const auto& videoTracks = liveSequence()->videoTracks();
    if (videoTracks.empty()) {
        return;
    }
    auto built = edit::makePlaceFromSource(document_.project(),
                                           {liveSequence()->id(), videoTracks.front().id()},
                                           source_->media(), *range, position_, mode);
    if (!built) {
        return;
    }
    document_.commands().execute(document_.project(), std::move(*built));
    document_.commands().breakMerge();
    bars_.scrubber->setRange(0, static_cast<int>(liveSequence()->duration().frames()));
    timeline_->update();
    monitor_->update();
    refresh();
}

void PreviewWindow::startWaveforms() {
    // Reclaim, not correctness: the thumbnail cache is addressed by file path,
    // so frames held for the old project's media are stale rather than wrong,
    // and a switch to proxies re-reads under a different key on its own. This
    // runs whenever the media changes, which is the moment those frames stop
    // being worth the memory.
    if (thumbnails_ != nullptr) {
        thumbnails_->clear();
    }

    // A scan may already be running -- reopening the media does this, which
    // is what switching to proxies does. Assigning over a joinable thread
    // is an immediate std::terminate, and that is exactly how it presented:
    // the application aborted the moment proxies were switched on.
    if (waveformThread_.joinable()) {
        waveformThread_.join();
    }
    const std::filesystem::path cacheDirectory =
        std::filesystem::temp_directory_path() / "zaro" / "waveforms";
    // Every media reference is tried, rather than only those the project
    // file says have audio. That cached info is a cache -- it can be stale,
    // absent, or written by a build that did not record it -- and deciding
    // whether to look at a file based on it means a missing field silently
    // becomes a missing waveform. Files without audio simply fail and are
    // skipped.
    std::vector<std::pair<model::MediaRefId, std::string>> wanted;
    for (const model::MediaRef& ref : document_.project().media()) {
        wanted.emplace_back(ref.id, ref.path);
    }
    if (wanted.empty()) {
        return;
    }
    waveformThread_ = std::thread{[this, wanted, cacheDirectory] {
        platform::ffmpeg::WaveformStore store{cacheDirectory.string()};
        const auto keepGoing = [this] { return !shuttingDown_.load(std::memory_order_relaxed); };
        for (const auto& [id, path] : wanted) {
            if (!keepGoing()) {
                return;
            }
            // Cancellable mid-file, not just between files: one long clip
            // is the common case, so checking only at file boundaries would
            // still make quitting wait for the whole scan.
            auto built = store.get(path, 512, keepGoing);
            if (!built) {
                continue;
            }
            auto shared = std::make_shared<const media::Waveform>(std::move(*built));
            // Back to the UI thread to hand it over: the widget is not
            // thread safe and neither is repainting.
            QMetaObject::invokeMethod(
                this, [this, id, shared] { timeline_->setWaveform(id, shared); },
                Qt::QueuedConnection);
        }
    }};
}

void PreviewWindow::refreshInstruments() {
    if (media_ == nullptr || liveSequence() == nullptr) {
        return;
    }
    const bool wantScopes = scopes_ != nullptr && scopes_->wantsMeasurement();
    const bool wantThumb = thumb_ != nullptr && thumb_->isVisible();
    if (playback_.isPlaying() || (!wantScopes && !wantThumb)) {
        return;
    }
    render::RenderGraph graph{*media_};
    graph.setTextRasterizer(&text_);
    graph.setProject(&document_.project());
    auto frame = graph.composite(*liveSequence(), position_);
    if (!frame) {
        if (wantScopes) {
            scopes_->clear();
        }
        if (wantThumb) {
            thumb_->clearFrame();
        }
        return;
    }
    if (wantScopes) {
        render::ScopeOptions options;
        options.waveformColumns = std::max(64, scopes_->width());
        // Every second row. The shape of a waveform does not change for
        // being measured at half the vertical resolution, and this is
        // running between scrubs.
        options.rowStride = 2;
        scopes_->setScopes(render::measure(*frame, options));
    }
    if (wantThumb) {
        showThumbnail(*frame);
    }
}

void PreviewWindow::showThumbnail(const render::RgbaImage& frame) {
    const int wide = frame.width();
    const int tall = frame.height();
    if (wide <= 0 || tall <= 0) {
        thumb_->clearFrame();
        return;
    }
    const int stride = wide * 3;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(stride) *
                                  static_cast<std::size_t>(tall));
    if (!render::toDisplayRgb24(frame, rgb.data(), stride)) {
        thumb_->clearFrame();
        return;
    }
    // Copied, because the QImage above only borrows the vector, and the
    // vector is gone at the end of this function.
    thumb_->setFrame(QImage{rgb.data(), wide, tall, stride, QImage::Format_RGB888}.copy());
    const model::Sequence* sequence = liveSequence();
    const model::Track* track = sequence != nullptr && !sequence->videoTracks().empty()
                                    ? &sequence->videoTracks().front()
                                    : nullptr;
    const model::Clip* clip = track != nullptr ? track->clipAt(position_) : nullptr;
    const bool dropFrame = sequence != nullptr && time::supportsDropFrame(sequence->frameRate());
    thumb_->setCaption(
        clip != nullptr ? QString::fromStdString(clip->name) : QString{"—"},
        sequence != nullptr
            ? QString::fromStdString(
                  time::timecodeFromFrames(position_.frames(), sequence->frameRate(), dropFrame)
                      .toString())
            : QString{});
}

void PreviewWindow::measureProgramme() {
    const model::Sequence* sequence = liveSequence();
    if (sequence == nullptr) {
        return;
    }
    // Stop the transport first. Playback decodes on a thread of its own -- see
    // PlaybackController::pumpAudio -- through the very media source this is
    // about to read, and nothing guards one decoder against two threads: doing
    // both at once faults inside avcodec. `stop` joins that thread, so past
    // this line the decoder is this thread's alone.
    //
    // Stopped rather than locked, and deliberately. A lock would have to be
    // held for a whole-programme decode, which is exactly as long as the wait
    // cursor below is up -- the realtime path would starve and the device would
    // drop out. Nothing was going to be heard through a modal wait anyway.
    playback_.stop();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    render::AudioGraph graph{*media_};
    const time::TimeRange whole{time::RationalTime{0, sequence->frameRate()}, sequence->duration()};
    auto measured = graph.measureLoudness(*sequence, whole);
    QApplication::restoreOverrideCursor();
    if (!measured) {
        app::warn(this, "Loudness", QString::fromStdString(measured.error().message()));
        return;
    }
    loudness_->setMeasurement(*measured);
}

void PreviewWindow::refreshGradeChain() {
    const model::Sequence* sequence = liveSequence();
    const model::Track* track = sequence != nullptr ? sequence->findTrack(selectedTrack_) : nullptr;
    const model::Clip* clip = track != nullptr ? track->find(selectedClip_) : nullptr;
    nodes_->setEnabledChain(clip != nullptr);
    if (clip == nullptr) {
        nodes_->setOccupied({false, false, false, false});
        return;
    }
    nodes_->setOccupied({!clip->color.isIdentity() || !clip->wheels.isIdentity(),
                         !clip->curves.isIdentity(), clip->secondary.qualifier.enabled,
                         !clip->lut.path.empty()});
}

void PreviewWindow::grabStill() {
    const QImage shot = monitor_->grab().toImage();
    if (shot.isNull()) {
        return;
    }
    const model::Sequence* sequence = liveSequence();
    const bool dropFrame = sequence != nullptr && time::supportsDropFrame(sequence->frameRate());
    const QString name =
        sequence != nullptr
            ? QString::fromStdString(
                  time::timecodeFromFrames(position_.frames(), sequence->frameRate(), dropFrame)
                      .toString())
            : QString{"still"};
    gallery_->addStill(shot, position_, name);
}

void PreviewWindow::exportStill() {
    const model::Sequence* sequence = liveSequence();
    if (media_ == nullptr || sequence == nullptr) {
        app::say(this, "Still", "There is nothing loaded to take a still of.");
        return;
    }

    // Colons are legal in a timecode and not in a Windows file name, so the
    // suggested name uses dashes. It is a suggestion; whatever gets typed over
    // it is what the file is called.
    const bool dropFrame = time::supportsDropFrame(sequence->frameRate());
    QString stamp = QString::fromStdString(
        time::timecodeFromFrames(position_.frames(), sequence->frameRate(), dropFrame).toString());
    stamp.replace(QChar(':'), QChar('-'));
    const QString stem = QString::fromStdString(sequence->name()) + " " + stamp;
    // Beside the project when there is one, since that is where the rest of
    // this piece of work lives.
    const QString folder = document_.path().empty()
                               ? QDir::homePath()
                               : QFileInfo(QString::fromStdString(document_.path())).absolutePath();

    QString path = QFileDialog::getSaveFileName(this, "Export still", QDir(folder).filePath(stem),
                                                "PNG image (*.png);;JPEG image (*.jpg);;TIFF "
                                                "image (*.tif)");
    if (path.isEmpty()) {
        return;
    }
    // A name with no suffix is one QImage cannot pick a format for, and it
    // fails by writing nothing rather than by saying so.
    if (QFileInfo(path).suffix().isEmpty()) {
        path += ".png";
    }

    // Composited here rather than read back off the monitor. The monitor is
    // whatever size the window gave it and is letterboxed inside that, so a
    // cover image taken from it would be the wrong size with black down two
    // edges. This is the same graph the export uses, at the sequence's size.
    render::RenderGraph graph{*media_};
    graph.setTextRasterizer(&text_);
    graph.setProject(&document_.project());
    auto frame = graph.composite(*sequence, position_);
    if (!frame) {
        app::say(this, "Still",
                 QString("The frame could not be rendered — %1")
                     .arg(QString::fromStdString(frame.error().toString())));
        return;
    }

    // The delivery's rolloff, for the same reason the encoder applies it: a
    // still that clipped where the export rolls off is not a picture of what
    // is being delivered.
    if (sequence->output().highlightKnee < 1.0) {
        render::toneMap(*frame, static_cast<float>(sequence->output().highlightKnee));
    }

    const int wide = frame->width();
    const int tall = frame->height();
    const int stride = wide * 3;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(stride) *
                                  static_cast<std::size_t>(tall));
    if (Status written =
            render::toDisplayRgb24(*frame, rgb.data(), stride, sequence->output().transfer);
        !written) {
        app::say(this, "Still",
                 QString("The frame could not be written — %1")
                     .arg(QString::fromStdString(written.error().toString())));
        return;
    }

    // The QImage borrows the vector, and save() reads it before returning.
    const QImage image{rgb.data(), wide, tall, stride, QImage::Format_RGB888};
    if (!image.save(path)) {
        app::say(this, "Still", QString("Nothing could be written to %1.").arg(path));
        return;
    }
    app::say(this, "Still", QString("Written to %1.").arg(QDir::toNativeSeparators(path)));
}

void PreviewWindow::showProgramMenu(const QPoint& where) {
    QMenu menu{this};
    QAction* save = menu.addAction("Export Still Frame…");
    connect(save, &QAction::triggered, this, [this] { exportStill(); });
    QAction* grab = menu.addAction("Grab Still to Gallery");
    connect(grab, &QAction::triggered, this, [this] { grabStill(); });
    save->setEnabled(liveSequence() != nullptr && media_ != nullptr);
    grab->setEnabled(save->isEnabled());
    menu.exec(where);
}

void PreviewWindow::applyLookToSelection(const QString& path) {
    if (!selectedClip_.isValid()) {
        app::say(this, "Look", "Pick a shot first — a look goes on a clip.");
        return;
    }
    model::LutRef look;
    look.path = path.toStdString();
    look.amount = 1.0;
    auto built =
        edit::makeSetLut(document_.project(), {sequenceId_, selectedTrack_}, selectedClip_, look);
    if (!built) {
        return;
    }
    document_.commands().execute(document_.project(), std::move(*built));
    document_.commands().breakMerge();
    renderCache_.clear();
    effects_->refresh();
    clipStrip_->refresh();
    refreshGradeChain();
    monitor_->update();
    refreshInstruments();
    updateTitle();
}

void PreviewWindow::refresh() {
    if (liveSequence() == nullptr) {
        return;
    }
    const bool dropFrame = time::supportsDropFrame(liveSequence()->frameRate());
    const time::Timecode code =
        time::timecodeFromFrames(position_.frames(), liveSequence()->frameRate(), dropFrame);
    bars_.timecode->setText(QString::fromStdString(code.toString()));
    if (!bars_.scrubber->isSliderDown()) {
        bars_.scrubber->setValue(static_cast<int>(position_.frames()));
    }
    if (deliver_ != nullptr) {
        deliver_->setPlayhead(position_);
    }
    const time::Timecode left = time::timecodeFromFrames(
        std::max<std::int64_t>(0, liveSequence()->duration().frames() - position_.frames()),
        liveSequence()->frameRate(), dropFrame);
    bars_.remaining->setText("-" + QString::fromStdString(left.toString()));
    const model::Track* track = liveSequence()->findTrack(selectedTrack_);
    const model::Clip* clip = track != nullptr ? track->find(selectedClip_) : nullptr;
    viewerOverlay_->setInfo(
        clip != nullptr ? QString::fromStdString(clip->name) : QString{},
        QString::fromStdString(code.toString()),
        QString("%1×%2").arg(liveSequence()->width()).arg(liveSequence()->height()),
        track != nullptr ? QString::fromStdString(track->name()) : QString{});
}

}  // namespace zaro::app
