// Running one long operation off the UI thread, behind a progress dialog.

#include "BackgroundWork.h"

#include <QEventLoop>
#include <QObject>
#include <QProgressDialog>
#include <QWidget>
#include <atomic>
#include <chrono>
#include <exception>
#include <thread>

namespace zaro::app {
namespace {

/// How often progress is allowed to reach the dialog.
///
/// The tracker reports once a frame. A queued call per frame is enough to keep
/// the UI thread doing nothing but redrawing a bar, which starves the thread
/// doing the actual work -- the Deliver queue hit exactly this and settled on
/// the same answer; see DeliverPanel::jobProgress. Throttled on the worker side
/// rather than the dialog side so the call is never posted at all.
constexpr auto kProgressInterval = std::chrono::milliseconds{50};

}  // namespace

bool runWithProgress(QWidget* parent, const QString& message, const QString& stopLabel,
                     const std::function<void(const Reporter&)>& work) {
    QProgressDialog dialog{message, stopLabel, 0, 1, parent};
    dialog.setWindowModality(Qt::WindowModal);
    // Below this, nothing is shown at all: an analysis over a two-second clip
    // should not flash a dialog on its way past.
    dialog.setMinimumDuration(400);
    // Both off, because the dialog's life is this function's and not the
    // progress value's. Reaching the maximum should not close it, and being
    // stopped should not quietly reset it to the beginning.
    dialog.setAutoClose(false);
    dialog.setAutoReset(false);
    // Starts the timer behind setMinimumDuration; without a value the dialog
    // never decides whether it is worth showing.
    dialog.setValue(0);

    std::atomic<bool> stop{false};
    QEventLoop loop;

    // Progress and completion arrive as queued calls on this, which lives on
    // the UI thread. A local is safe: nothing is posted after the join below,
    // and ~QObject drops anything still queued.
    QObject relay;
    QObject::connect(&dialog, &QProgressDialog::canceled, &relay,
                     [&stop] { stop.store(true, std::memory_order_relaxed); });

    // Read and written only by the worker, which is the only thread that calls
    // the reporter.
    auto lastPosted = std::chrono::steady_clock::now() - kProgressInterval;
    const Reporter report = [&](std::int64_t done, std::int64_t total) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastPosted >= kProgressInterval) {
            lastPosted = now;
            QMetaObject::invokeMethod(
                &relay,
                [&dialog, done, total] {
                    dialog.setMaximum(static_cast<int>(total));
                    dialog.setValue(static_cast<int>(done));
                },
                Qt::QueuedConnection);
        }
        return !stop.load(std::memory_order_relaxed);
    };

    std::exception_ptr failure;
    std::thread worker{[&] {
        // Whatever happens in there, the loop has to be told about it, or the
        // dialog stays up over a window that never gets its event loop back.
        try {
            work(report);
        } catch (...) {
            failure = std::current_exception();
        }
        QMetaObject::invokeMethod(&relay, [&loop] { loop.quit(); }, Qt::QueuedConnection);
    }};

    loop.exec();
    worker.join();

    if (failure) {
        std::rethrow_exception(failure);
    }
    return !stop.load(std::memory_order_relaxed);
}

}  // namespace zaro::app
