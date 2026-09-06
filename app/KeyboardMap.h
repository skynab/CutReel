#pragma once

#include <QColor>
#include <QHash>
#include <QSet>
#include <QString>
#include <QWidget>
#include <functional>
#include <vector>

namespace zaro::app {

/// The keyboard, drawn, with what each key does written on it.
///
/// **Because a list of shortcuts does not answer the question people ask of
/// one.** "What can I still bind?" and "what is under my left hand?" are
/// spatial questions, and a table sorted by command name is the one shape that
/// cannot answer them: a free key is an absence, and an absence is invisible in
/// a list of things that exist. Here it is a blank cap, in the place the finger
/// would go.
///
/// One layer at a time. The map shows the keys as they are with a given set of
/// modifiers held -- unmodified, then Ctrl, then Ctrl+Shift -- because that is
/// how a keyboard actually works, and because sixty keys times sixteen
/// combinations on one picture is not a picture. Clicking a modifier cap holds
/// it, which is the same gesture as pressing it.
///
/// It knows nothing about actions or the keymap: it is given a cap-to-binding
/// table and hands back the key that was clicked. What a binding *means* is the
/// manager's business.
class KeyboardMap : public QWidget {
public:
    /// What is on a cap, on the layer being shown.
    struct Binding {
        /// The action's name, written small under the cap. Empty draws a blank
        /// key, which is the point of the whole widget.
        QString label;
        QColor tint;
        QString actionId;
    };

    explicit KeyboardMap(QWidget* parent = nullptr);

    /// What a modifier is printed as: the glyph a Mac has on the key, the word
    /// everybody else has. Public because the manager's own modifier row has to
    /// say the same thing the caps do.
    [[nodiscard]] static QString faceFor(const QString& modifier);

    /// Keyed by the canonical key name -- "A", "Space", "Comma" -- as `Keymap`
    /// spells it, so the caller does no translating.
    void setBindings(QHash<QString, Binding> bindings);
    /// Which modifiers are held. Spelled "Ctrl", "Alt", "Shift", "Meta".
    void setHeld(QSet<QString> held);
    void setSelected(const QString& actionId);

    /// A key with something on it was clicked.
    void setOnPick(std::function<void(const QString& actionId)> handler) {
        onPick_ = std::move(handler);
    }
    /// A modifier cap was clicked: hold it, or let it go.
    void setOnHold(std::function<void(const QString& modifier)> handler) {
        onHold_ = std::move(handler);
    }

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /// One cap: what is printed on it, what `Keymap` calls it, and how many
    /// units wide it is. A modifier's `key` is its modifier name, which is what
    /// separates the two kinds of click.
    struct Cap {
        QString face;
        QString key;
        double units{1.0};
        bool modifier{false};
    };

    [[nodiscard]] std::vector<QRectF> geometry() const;
    [[nodiscard]] int capAt(QPointF where) const;

    std::vector<Cap> caps_;
    /// Where each row starts in `caps_`, plus the end -- so a row is a span.
    std::vector<int> rowStarts_;
    QHash<QString, Binding> bindings_;
    QSet<QString> held_;
    QString selected_;
    int hovered_{-1};
    std::function<void(const QString&)> onPick_;
    std::function<void(const QString&)> onHold_;
};

}  // namespace zaro::app
