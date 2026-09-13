// What every translation unit in the app layer pays for anyway.
//
// Force-included by CMake rather than by anything here -- no source file names
// this header, and none should. See `target_precompile_headers` in
// app/CMakeLists.txt.
//
// **Why this exists.** app/ is 105 files against Qt Widgets, and the GUI tests
// are eleven more that each reach the whole window through GuiFixture.h. Qt's
// widget headers are tens of thousands of lines apiece after their own
// includes, and every one of those translation units was parsing them from
// scratch: the app layer rebuilt in 113 seconds, most of it spent reading the
// same headers 70-odd times. Parsed once into a precompiled header, that is
// most of the cost of an app-layer build gone for no change to any source file.
//
// **What belongs here, and what does not.** Three tests, all of which have to
// hold:
//
//   1. Widely included. A header two files want is a header two files should
//      include; putting it here makes the other hundred pay to parse it.
//   2. Expensive. Qt and the standard library qualify. A twenty-line header of
//      ours does not, however many files want it.
//   3. Stable. **This is the one that bites.** Editing a header named here
//      invalidates the precompiled header, which rebuilds every object in the
//      target -- so a header under active development belongs nowhere near this
//      list, even when it passes the first two tests.
//
// Rule 3 is why the project's own headers are represented by four of the
// stablest and no more. `model/Project.h` is the single most included of ours
// after `edit/Operations.h` and drags Sequence, Track and Clip in with it --
// and it is also the file somebody edits to add a field to the model, which
// would turn a one-field change into a full rebuild of the app layer.
// `edit/Operations.h` is worse: 91 declarations that grow by one every time a
// feature lands. Both are deliberately absent.
//
// Ids, Error, Rational and RationalTime are the opposite case: they are in
// almost everything, they are settled, and ADR-0001 is what settled the time
// types. They have not changed in months and are not expected to.
#pragma once

// --- The standard library ------------------------------------------------
// The ones that appear in more than a handful of files. Ordered as they are
// counted, not alphabetically, so it is obvious what is here on merit.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// --- Qt ------------------------------------------------------------------
// Widgets, the events that drive them and the classes used to draw them. This
// is the expensive half of the list and the reason the file exists: QWidget
// alone is in 29 of these files, and QPainter in 25.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

// --- Ours, the settled part ----------------------------------------------
// See rule 3 above for why this list is four headers and not the eight the
// include counts would suggest.
#include "zaro/core/Error.h"
#include "zaro/core/model/Ids.h"
#include "zaro/core/time/Rational.h"
#include "zaro/core/time/RationalTime.h"
