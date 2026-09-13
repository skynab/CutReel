// Handing a cut to another program, and taking one back.
//
// Three formats, five commands, and the knowledge of which file extension and
// which dialog title belongs to which of them. That was six methods on
// PreviewWindow, and with them four `zaro/core/io` headers on a header that
// 105 files and every GUI test already parse. None of it is about the window.
//
// **What is here and what is not.** Reading and writing a file is here.
// *Adopting* what was read -- making it the open project, retitling the window,
// saying what did not cross -- is not: that is the window's business, and it is
// the half that has to know about autosave, the command stack and the lock. So
// an import returns what it read and the window decides what to do with it,
// which is also what makes the reading testable without a window to adopt into.
//
// **Named for the program, not the format.** "FCP7 XML" is what the file is;
// "the one Premiere opens" is what somebody came here for. Four entries rather
// than two with a format picker: the formats have no version in common -- Final
// Cut cannot read what Premiere reads -- so what somebody is choosing is which
// program the cut is going to, and making them pick that twice would be asking
// a question they have already answered.
#pragma once

#include <QString>
#include <optional>

#include "zaro/core/model/Ids.h"
#include "zaro/core/model/Project.h"

class QWidget;

namespace zaro::app::interchange {

/// Write the sequence out, asking where first.
///
/// Each shows its own file dialog and reports its own failure, because there is
/// nothing for the caller to do about either. Silent when cancelled.
///
/// OTIO is export only. Importing one produces a project of its own, and
/// replacing the open one needs a "save first?" that does not exist yet -- so
/// that direction lives in cutreel-otio, where there is nothing to lose.
void exportOtio(QWidget* parent, const model::Project& project, model::SequenceId sequence);
void exportPremiere(QWidget* parent, const model::Project& project, model::SequenceId sequence);
void exportFinalCut(QWidget* parent, const model::Project& project, model::SequenceId sequence);

/// A cut read back from another program, and the plain truth about it.
struct Imported {
    model::Project project;
    /// What to call the format when telling somebody about it.
    QString format;
    /// What this format does not carry, phrased to be read in a sentence.
    ///
    /// Carried with the project rather than looked up by the caller because it
    /// is a fact about the format, and the format is what this file knows. It
    /// is the sort of absence somebody finds an hour later, in a grade that is
    /// not there, so it is not optional and not the window's to remember.
    QString lost;
};

/// Ask for a file and read it.
///
/// Empty when cancelled or unreadable; an unreadable file has already been
/// reported, so an empty return needs no message of its own.
[[nodiscard]] std::optional<Imported> importPremiere(QWidget* parent);
[[nodiscard]] std::optional<Imported> importFinalCut(QWidget* parent);

}  // namespace zaro::app::interchange
