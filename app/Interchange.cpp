#include "Interchange.h"

#include <QFileDialog>
#include <utility>

#include "zaro/core/Error.h"
#include "zaro/core/io/FinalCutXml.h"
#include "zaro/core/io/OtioIo.h"
#include "zaro/core/io/PremiereXml.h"

#include "Say.h"

namespace zaro::app::interchange {

namespace {

/// Ask where to write, then write, then say so if it failed.
///
/// The three exports differ in a title, a suggested name, a filter and which
/// `io::save*` to call, and in nothing else. Written once over those four
/// rather than three times: a fix to how a failure is reported -- or to the
/// check that there is a sequence to export at all -- is otherwise a fix that
/// has to be made in three places and will be made in two.
template <typename Save>
void exportThrough(QWidget* parent, const model::Project& project, model::SequenceId sequence,
                   const QString& title, const QString& suggested, const QString& filter,
                   const QString& label, Save&& save) {
    if (project.findSequence(sequence) == nullptr) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(parent, title, suggested, filter);
    if (path.isEmpty()) {
        return;
    }
    if (Status saved = save(project, sequence, path.toStdString()); !saved) {
        app::warn(parent, label, QString::fromStdString(saved.error().toString()));
    }
}

/// Ask for a file, then read it, then say so if it failed.
///
/// The mirror of `exportThrough`, and the same argument for existing.
template <typename Load>
std::optional<Imported> importThrough(QWidget* parent, const QString& title, const QString& filter,
                                      const QString& label, const QString& lost, Load&& load) {
    const QString path = QFileDialog::getOpenFileName(parent, title, {}, filter);
    if (path.isEmpty()) {
        return std::nullopt;
    }
    auto read = load(path.toStdString());
    if (!read) {
        app::warn(parent, label, QString::fromStdString(read.error().toString()));
        return std::nullopt;
    }
    return Imported{std::move(*read), label, lost};
}

}  // namespace

void exportOtio(QWidget* parent, const model::Project& project, model::SequenceId sequence) {
    exportThrough(parent, project, sequence, "Export OpenTimelineIO", "timeline.otio",
                  "OpenTimelineIO (*.otio)", "OpenTimelineIO", io::saveOtio);
}

void exportPremiere(QWidget* parent, const model::Project& project, model::SequenceId sequence) {
    exportThrough(parent, project, sequence, "Export Premiere XML", "timeline.xml",
                  "FCP7 XML (*.xml)", "Premiere XML", io::savePremiereXml);
}

void exportFinalCut(QWidget* parent, const model::Project& project, model::SequenceId sequence) {
    exportThrough(parent, project, sequence, "Export Final Cut Pro XML", "timeline.fcpxml",
                  "Final Cut Pro XML (*.fcpxml)", "Final Cut Pro XML", io::saveFcpXml);
}

std::optional<Imported> importPremiere(QWidget* parent) {
    return importThrough(parent, "Import Premiere XML", "FCP7 XML (*.xml)", "Premiere XML",
                         "Grades, effects, transitions and keyframes", io::loadPremiereXml);
}

std::optional<Imported> importFinalCut(QWidget* parent) {
    // The bundle as well as the file. Final Cut 10.6.6 began writing a
    // `.fcpxmld` directory whose `Info.fcpxml` is the document, and what
    // somebody picks in this dialog is the bundle -- so it has to be offered,
    // and `loadFcpXml` looks inside.
    return importThrough(parent, "Import Final Cut Pro XML",
                         "Final Cut Pro XML (*.fcpxml *.fcpxmld)", "Final Cut Pro XML",
                         "Grades, effects, transitions, keyframes and track mute and lock",
                         io::loadFcpXml);
}

}  // namespace zaro::app::interchange
