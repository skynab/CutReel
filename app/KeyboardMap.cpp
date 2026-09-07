#include "KeyboardMap.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

#include "Theme.h"

namespace zaro::app {
namespace {

constexpr double kGap = 5.0;
constexpr double kKeyHeight = 44.0;
constexpr double kRadius = 5.0;

#ifdef Q_OS_MACOS
constexpr const char* kCapsFace = "⇪";
#else
constexpr const char* kCapsFace = "Caps";
#endif

/// The same for the keys that have a symbol rather than a name.
///
/// The arrows and the punctuation are the same everywhere; the four keys with
/// a glyph on a Mac and a word on a PC are the four printed that way on the
/// hardware, and a map of a keyboard that disagrees with the keyboard is a map
/// of somebody else's.
QString keyFace(const QString& key) {
    static const QHash<QString, QString> kFaces{
        {"Space", QStringLiteral("Space")},    {"Left", QString::fromUtf8("←")},
        {"Down", QString::fromUtf8("↓")},      {"Right", QString::fromUtf8("→")},
        {"Up", QString::fromUtf8("↑")},        {"Comma", QStringLiteral(",")},
        {"Period", QStringLiteral(".")},       {"Slash", QStringLiteral("/")},
        {"Backslash", QStringLiteral("\\")},   {"Semicolon", QStringLiteral(";")},
        {"Home", QStringLiteral("Home")},      {"End", QStringLiteral("End")},
#ifdef Q_OS_MACOS
        {"Backspace", QString::fromUtf8("⌫")}, {"Tab", QString::fromUtf8("⇥")},
        {"Return", QString::fromUtf8("↵")},    {"Delete", QString::fromUtf8("⌦")},
#else
        {"Backspace", QStringLiteral("Bksp")}, {"Tab", QStringLiteral("Tab")},
        {"Return", QStringLiteral("Enter")},   {"Delete", QStringLiteral("Del")},
#endif
    };
    return kFaces.value(key, key);
}

}  // namespace

QString KeyboardMap::faceFor(const QString& modifier) {
#ifdef Q_OS_MACOS
    static const QHash<QString, QString> kGlyphs{{"Ctrl", QString::fromUtf8("⌃")},
                                                 {"Alt", QString::fromUtf8("⌥")},
                                                 {"Shift", QString::fromUtf8("⇧")},
                                                 {"Meta", QString::fromUtf8("⌘")}};
    return kGlyphs.value(modifier, modifier);
#else
    return modifier == "Meta" ? QStringLiteral("Win") : modifier;
#endif
}

KeyboardMap::KeyboardMap(QWidget* parent) : QWidget{parent} {
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);

    // The rows, in the order fingers meet them. Widths are units of one letter
    // key, which is how a keyboard is actually proportioned -- the row then
    // fills whatever width the window gives it, so the map stays a keyboard
    // rather than a grid that happens to have letters in it.
    const auto row = [this](std::initializer_list<Cap> caps) {
        rowStarts_.push_back(static_cast<int>(caps_.size()));
        caps_.insert(caps_.end(), caps.begin(), caps.end());
    };
    const auto letter = [](const char* key) {
        return Cap{QString::fromUtf8(key), QString::fromUtf8(key), 1.0, false};
    };
    const auto named = [](const char* key, double units) {
        return Cap{keyFace(QString::fromUtf8(key)), QString::fromUtf8(key), units, false};
    };
    const auto mod = [](const char* modifier, double units) {
        return Cap{faceFor(QString::fromUtf8(modifier)), QString::fromUtf8(modifier), units, true};
    };
    // A cap with no key: it is on the keyboard, nothing can be bound to it, and
    // leaving it out would put the letters in the wrong place.
    const auto dead = [](const char* face, double units) {
        return Cap{QString::fromUtf8(face), QString{}, units, false};
    };

    // The nav keys on the right -- Delete, Home, End, and the arrow cluster --
    // are here because the catalogue binds every one of them: Delete and
    // Shift+Delete are lift and ripple delete, Home and End are the ends of the
    // sequence, and Up and Down step the source. A map that left them off would
    // draw five of the commands people use most as nowhere at all, which is the
    // one thing this widget exists to stop. Up sits at the end of the Shift row
    // because that is where it is on the hardware, above Down.
    row({letter("`"), letter("1"), letter("2"), letter("3"), letter("4"), letter("5"), letter("6"),
         letter("7"), letter("8"), letter("9"), letter("0"), letter("-"), letter("="),
         named("Backspace", 1.8), named("Delete", 1.0)});
    row({named("Tab", 1.5), letter("Q"), letter("W"), letter("E"), letter("R"), letter("T"),
         letter("Y"), letter("U"), letter("I"), letter("O"), letter("P"), letter("["), letter("]"),
         named("Backslash", 1.3), named("Home", 1.0)});
    row({dead(kCapsFace, 1.8), letter("A"), letter("S"), letter("D"), letter("F"), letter("G"),
         letter("H"), letter("J"), letter("K"), letter("L"), named("Semicolon", 1.0), letter("'"),
         named("Return", 1.9), named("End", 1.0)});
    row({mod("Shift", 2.3), letter("Z"), letter("X"), letter("C"), letter("V"), letter("B"),
         letter("N"), letter("M"), named("Comma", 1.0), named("Period", 1.0), named("Slash", 1.0),
         mod("Shift", 2.3), named("Up", 1.0)});
    row({dead("fn", 1.0), mod("Ctrl", 1.0), mod("Alt", 1.0), mod("Meta", 1.3), named("Space", 6.0),
         mod("Meta", 1.3), mod("Alt", 1.0), named("Left", 1.0), named("Down", 1.0),
         named("Right", 1.0)});
    rowStarts_.push_back(static_cast<int>(caps_.size()));
}

void KeyboardMap::setBindings(QHash<QString, Binding> bindings) {
    bindings_ = std::move(bindings);
    update();
}

void KeyboardMap::setHeld(QSet<QString> held) {
    held_ = std::move(held);
    update();
}

bool KeyboardMap::hasKey(const QString& key) const {
    return std::any_of(caps_.begin(), caps_.end(),
                       [&key](const Cap& cap) { return cap.key == key; });
}

void KeyboardMap::setSelected(const QString& actionId) {
    if (selected_ == actionId) {
        return;
    }
    selected_ = actionId;
    update();
}

QSize KeyboardMap::sizeHint() const {
    const int rows = static_cast<int>(rowStarts_.size()) - 1;
    const auto height = static_cast<int>(rows * kKeyHeight + (rows - 1) * kGap);
    return {760, height};
}

std::vector<QRectF> KeyboardMap::geometry() const {
    std::vector<QRectF> boxes(caps_.size());
    const double available = width();
    double top = 0.0;
    for (std::size_t r = 0; r + 1 < rowStarts_.size(); ++r) {
        const int from = rowStarts_[r];
        const int to = rowStarts_[r + 1];
        double units = 0.0;
        for (int i = from; i < to; ++i) {
            units += caps_[static_cast<std::size_t>(i)].units;
        }
        // The row fills the width whatever its unit total, which is why five
        // rows of different key counts still line up down the sides.
        const double spare = available - kGap * (to - from - 1);
        double left = 0.0;
        for (int i = from; i < to; ++i) {
            const double span = spare * caps_[static_cast<std::size_t>(i)].units / units;
            boxes[static_cast<std::size_t>(i)] = QRectF{left, top, span, kKeyHeight};
            left += span + kGap;
        }
        top += kKeyHeight + kGap;
    }
    return boxes;
}

int KeyboardMap::capAt(QPointF where) const {
    const std::vector<QRectF> boxes = geometry();
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        if (boxes[i].contains(where)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void KeyboardMap::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);

    QFont capFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    capFont.setPixelSize(11);
    QFont labelFont = font();
    labelFont.setPixelSize(9);
    const QFontMetrics labelMetrics{labelFont};

    const std::vector<QRectF> boxes = geometry();
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const Cap& cap = caps_[i];
        const QRectF box = boxes[i];
        const Binding binding = cap.key.isEmpty() ? Binding{} : bindings_.value(cap.key);
        const bool bound = !binding.label.isEmpty();
        const bool holding = cap.modifier && held_.contains(cap.key);
        const bool chosen = bound && binding.actionId == selected_;

        // The face of the key: a shade of the ground, lifted towards the
        // group's own colour when something is bound to it, so a layer reads as
        // a pattern before any of the words on it are legible.
        QColor top = theme::neutral(800);
        QColor bottom = theme::neutral(900);
        if (holding) {
            top = theme::mix(theme::neutral(800), theme::accent(), 0.40);
            bottom = theme::mix(theme::neutral(900), theme::accent(), 0.22);
        } else if (bound) {
            top = theme::mix(theme::neutral(800), binding.tint, 0.16);
        }
        QLinearGradient face{box.topLeft(), box.bottomLeft()};
        face.setColorAt(0.0, top);
        face.setColorAt(1.0, bottom);

        QColor border = Qt::transparent;
        if (holding) {
            border = theme::accent(400);
        } else if (chosen) {
            border = theme::accent(300);
        } else if (static_cast<int>(i) == hovered_ && (bound || cap.modifier)) {
            border = theme::mix(theme::bg(), theme::accent(), 0.45);
        }

        painter.setPen(border == QColor(Qt::transparent) ? QPen{Qt::NoPen}
                                                         : QPen{border, chosen ? 1.6 : 1.0});
        painter.setBrush(face);
        painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

        // The bevel every keycap has, one line of it: without it the row is a
        // set of tinted rectangles rather than something to press.
        painter.setPen(QPen{theme::mix(top, theme::text(), 0.10), 1.0});
        painter.drawLine(QPointF{box.left() + kRadius, box.top() + 1.0},
                         QPointF{box.right() - kRadius, box.top() + 1.0});

        const QRectF inner = box.adjusted(5.0, 4.0, -5.0, -4.0);
        painter.setFont(capFont);
        painter.setPen(bound || holding ? theme::text() : theme::textAt(0.45));
        painter.drawText(QRectF{inner.left(), inner.top(), inner.width(), 13.0},
                         Qt::AlignLeft | Qt::AlignTop, cap.face);

        if (holding) {
            painter.setFont(labelFont);
            painter.setPen(theme::accent(200));
            painter.drawText(QRectF{inner.left(), inner.bottom() - 11.0, inner.width(), 11.0},
                             Qt::AlignLeft | Qt::AlignBottom, QStringLiteral("held"));
        } else if (bound) {
            painter.setFont(labelFont);
            painter.setPen(binding.tint);
            painter.drawText(QRectF{inner.left(), inner.bottom() - 11.0, inner.width(), 11.0},
                             Qt::AlignLeft | Qt::AlignBottom,
                             labelMetrics.elidedText(binding.label, Qt::ElideRight,
                                                     static_cast<int>(inner.width())));
        }
    }
}

void KeyboardMap::mousePressEvent(QMouseEvent* event) {
    const int index = capAt(event->position());
    if (index < 0) {
        return;
    }
    const Cap& cap = caps_[static_cast<std::size_t>(index)];
    if (cap.modifier) {
        if (onHold_) {
            onHold_(cap.key);
        }
        return;
    }
    const Binding binding = bindings_.value(cap.key);
    if (!binding.actionId.isEmpty() && onPick_) {
        onPick_(binding.actionId);
    }
}

void KeyboardMap::mouseMoveEvent(QMouseEvent* event) {
    const int index = capAt(event->position());
    if (index == hovered_) {
        return;
    }
    hovered_ = index;
    // The cap is too small for the whole name of a command, so the name it
    // could not fit is one hover away rather than lost.
    if (index >= 0) {
        const Binding binding = bindings_.value(caps_[static_cast<std::size_t>(index)].key);
        setToolTip(binding.label);
    } else {
        setToolTip(QString{});
    }
    update();
}

void KeyboardMap::leaveEvent(QEvent* /*event*/) {
    hovered_ = -1;
    update();
}

}  // namespace zaro::app
