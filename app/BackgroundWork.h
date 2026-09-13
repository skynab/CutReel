// Running one long operation off the UI thread, behind a progress dialog.
#pragma once

#include <QString>
#include <cstdint>
#include <functional>

class QWidget;

namespace zaro::app {

/// How work running on a worker thread reports in.
///
/// Answers false once somebody has pressed the dialog's stop button. This is
/// deliberately the same shape as `commands::Progress`, so an operation that
/// already knows how to report and be stopped needs to know nothing about
/// threads to be run on one.
using Reporter = std::function<bool(std::int64_t done, std::int64_t total)>;

/// Run `work` on a worker thread while `parent` shows a progress dialog.
///
/// **The point of this is what it does not do.** The analyses this replaced
/// each ran on the UI thread and drove their dialog by calling
/// `QCoreApplication::processEvents()` once a frame. A pump like that is not
/// just slow, it is re-entrant: every timer, every queued call from the
/// waveform, thumbnail and render threads, and the autosave all run *inside*
/// the analysis, with the model half-read. The dialogs were already window
/// modal, so this was never about the user clicking something -- it was about
/// everything that reaches a window without being clicked.
///
/// With the work on another thread there is no pump. Qt's own event loop runs
/// normally, so the window repaints, the dialog animates, and stopping takes
/// effect on the next frame the work looks at.
///
/// `work` must touch nothing the UI thread owns. It gets a copy of whatever it
/// reads and opens its own decoders; see `commands::AnalysisInput` for why that
/// is not optional.
///
/// Returns true if `work` ran to completion, false if it was stopped. An
/// exception from `work` is carried across and rethrown here, on the thread
/// that can do something about it.
bool runWithProgress(QWidget* parent, const QString& message, const QString& stopLabel,
                     const std::function<void(const Reporter&)>& work);

}  // namespace zaro::app
