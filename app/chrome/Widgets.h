// Small widget factories, and the bars that are only widgets.
#pragma once

#include <QPoint>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <cstdint>
#include <functional>

#include "../ActionRouter.h"
#include "../Icons.h"
#include "../TimelineWidget.h"
#include "Bars.h"

class QAbstractItemView;
class QDockWidget;
class QFrame;
class QLabel;

namespace zaro::app::chrome {

QPushButton* button(QWidget* parent, const QString& text, const QString& tip,
                    bool checkable = false);
QPushButton* iconButton(QWidget* parent, app::icons::Glyph glyph, const QString& tip,
                        bool checkable = false);
QFrame* separator(QWidget* parent);
QLabel* mutedLabel(QWidget* parent, const QString& text = {});

/// A number of bytes, in the unit somebody can read at a glance.
///
/// One of these, because there were three and they had drifted. The media
/// browser stopped at megabytes, so every file under half a megabyte listed as
/// "0.0 MB" -- and the Deliver panel carried a comment saying that "0 MB reads
/// as an arithmetic bug rather than as a small file", having hit exactly that
/// and fixed it in its own copy alone. They also disagreed about the spelling
/// of the unit and how many decimals to show, in the same window.
[[nodiscard]] QString humanSize(double bytes);

/// Put text on a label that shortens itself rather than widening its row.
///
/// A QLabel asks for the width of its whole string and cannot be squeezed
/// below it, so one long line pushes its layout wider than the panel around it
/// and the end is simply cut off -- which is what a finished render's note did
/// to the delivery queue's cards. The full text goes in the tooltip, so nothing
/// is lost by shortening what is drawn.
///
/// Re-elided whenever the label is resized, so it follows a splitter.
void setElidedText(QLabel* label, const QString& text, Qt::TextElideMode mode = Qt::ElideRight);

/// Say what a list is for while it is empty.
///
/// Every pane in this window says what it is for when it has nothing in it --
/// the Effects tab, the Titles tab, the grading palette. The list views were
/// the exception, and an empty one reads as a panel that failed to load rather
/// than one waiting to be given something.
///
/// A label over the viewport rather than a paintEvent, so it needs no subclass
/// per list and takes the same "muted" styling as every other such sentence. It
/// follows the view's model, so a list that fills up hides it without anyone
/// having to remember to.
void setEmptyText(QAbstractItemView* view, const QString& text);

/// The handful of things the bars do that are not commands.
///
/// Everything a bar can do that somebody could also reach from a menu or a
/// keystroke goes through the router by id -- that is most of them. What is
/// left is this: choices about how the window is arranged, which are not
/// commands because there is nothing to put on a menu called "show the source
/// viewer, off". Nine functions, written down, instead of a window pointer that
/// would have let a bar call anything at all.
struct Hooks {
    std::function<void(const QString&)> chooseWorkspace;
    std::function<void(app::TimelineWidget::Tool)> chooseTool;
    std::function<void(bool)> showSource;
    std::function<void(bool)> showProgram;
    std::function<void(bool)> setGuides;
    std::function<void(bool)> setSnapEnabled;
    std::function<void(double)> setZoomFraction;
    std::function<void(double)> setTrackHeightFraction;
    std::function<void()> queueRender;
    std::function<void()> toggleRendering;
    /// A frame size picked from the toolbar's dropdown. Zeroes mean the
    /// "Custom…" entry, which the window answers with a prompt -- the chrome
    /// deliberately does not know how to ask for a number.
    std::function<void(std::int32_t, std::int32_t)> chooseFrameSize;
};

/// A dock widget's header, drawn as the docking mockup's tab strip: the
/// panel's name as a single tab (accent underline while the dock holds the
/// focus, a neutral one otherwise) and a close button on the right. Every
/// dock gets a name, including panels that draw their own tab row inside --
/// the mockup names every card.
///
/// The strip is 30px, and must stay well over 20: a strip thinner than that
/// never starts a drag at all under Qt's own drag-initiation hit-testing --
/// measured, not guessed.
///
/// `setTitleBarWidget` replaces Qt's own title bar wholesale, close button
/// and all, so `onClose` puts one back when it is not empty -- left empty for
/// the one dock (the timeline) that should not be closeable at all.
///
/// `tools`, when given, is seated in the header after the name and takes the
/// remaining width -- the timeline's tool row, so its dock has one bar rather
/// than a header with a toolbar under it.
QWidget* buildDockHeader(QWidget* parent, const QString& title,
                         const std::function<void()>& onClose = {}, QWidget* tools = nullptr);

/// Start dragging `dock` by its header, as if the pointer had just been
/// pressed on it -- for a dock that has just been floated out from under a
/// pointer that is already down (a tab pulled off a strip), so the drag
/// carries on as an ordinary header drag and can be dropped back anywhere.
/// `at` is where on the header the pointer is taken to be, and the dock is
/// moved to put that point under the pointer.
void beginDockDrag(QDockWidget* dock, QPoint at);

/// Detach the first row of a panel's own layout -- its tab strip or toolbar --
/// and hand it back, for `buildDockHeader`'s `tools`. Null, and the panel
/// untouched, when the first item is not a widget. Lifting the row means a
/// panel's dock has one bar instead of a header with a second bar under it.
QWidget* liftFirstRow(QWidget* panel);

QWidget* buildTitleBar(QWidget* parent, Bars& bars);
QWidget* buildStatusBar(QWidget* parent, Bars& bars);
QWidget* buildToolPalette(QWidget* parent, Bars& bars,
                          const std::function<void(app::TimelineWidget::Tool)>& chooseTool);

QWidget* buildToolBar(QWidget* parent, Bars& bars, ActionRouter& router, const Hooks& hooks,
                      const QStringList& workspaces, const QString& supportUrl);
QWidget* buildViewerBar(QWidget* parent, Bars& bars, ActionRouter& router, const Hooks& hooks);
QWidget* buildTransportBar(QWidget* parent, Bars& bars, ActionRouter& router);
/// `timelineWidget` is the timeline itself, which the pane puts under its bar.
QWidget* buildTimelinePane(QWidget* parent, Bars& bars, ActionRouter& router, const Hooks& hooks,
                           QWidget* timelineWidget);

}  // namespace zaro::app::chrome
