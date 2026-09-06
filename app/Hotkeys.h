#pragma once

#include <QDialog>
#include <QHash>
#include <QSet>
#include <QString>
#include <functional>
#include <string>

#include "zaro/ui/Keymap.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTreeWidget;

namespace zaro::app {

class KeyboardMap;

/// Change what the keys do.
///
/// **A keyboard, then a list.** This was a three-column table and two buttons,
/// which is the smallest thing that lets somebody rebind a command and the
/// worst thing to look at when the question is "what is free?" or "what does
/// this key already do?". The map at the top answers both without reading:
/// every cap carries the command on it, tinted by the group it belongs to,
/// and an empty cap is an empty key. Holding a modifier -- by pressing it, or
/// by clicking it -- swaps the whole map to that layer, because that is what
/// holding a modifier does.
///
/// The list underneath is still the way to find a command by name, and the
/// panel on the right is where a binding is actually changed. All three read
/// the same selection, so picking a key lights up its row and picking a row
/// lights up its key.
///
/// It edits the window's keymap directly and calls back when anything changes,
/// so what is shown and what the keys do cannot drift apart.
class Hotkeys : public QDialog {
    Q_OBJECT

public:
    explicit Hotkeys(ui::Keymap& keymap, QWidget* parent = nullptr);

    /// Called after every change, to re-apply the bindings and save them.
    void setOnChanged(std::function<void()> handler) { onChanged_ = std::move(handler); }

    void refresh();

    /// Bind the selected action, as though somebody had pressed these keys.
    /// Public because it is the action, and the capture button is one way of
    /// asking for it.
    [[nodiscard]] Status assign(const std::string& actionId, const std::string& keystroke);
    [[nodiscard]] Status resetOne(const std::string& actionId);
    void selectAction(const std::string& actionId);
    [[nodiscard]] std::string selectedAction() const;

protected:
    /// While recording, every key press is the binding rather than a shortcut.
    void keyPressEvent(QKeyEvent* event) override;

private:
    /// The four panes, each rebuilt from the keymap rather than patched: a
    /// rebind can move a command between layers, categories and rows at once,
    /// and every way of patching that in place has a case it gets wrong.
    void fillKeyboard();
    void fillCategories();
    void fillList();
    void fillSelection();
    void fillStatus();

    void changed();
    void setRecording(bool on);
    /// The modifiers currently held, in the spelling `Keymap` normalises to --
    /// "Ctrl+Alt+", or empty for the unmodified layer.
    [[nodiscard]] std::string layer() const;
    /// Which of the twelve tints this group of commands gets. Assigned in
    /// catalogue order, so a category keeps its colour between runs.
    [[nodiscard]] QColor tintFor(std::string_view category) const;

    void importKeymap();
    void exportKeymap();

    ui::Keymap& keymap_;
    std::function<void()> onChanged_;

    KeyboardMap* keyboard_{nullptr};
    QLineEdit* search_{nullptr};
    QHash<QString, QPushButton*> modifiers_;
    QLabel* layerLabel_{nullptr};
    QLabel* boundLabel_{nullptr};
    QTreeWidget* categories_{nullptr};
    QTableWidget* table_{nullptr};

    QLabel* selName_{nullptr};
    QLabel* selWhere_{nullptr};
    QPushButton* binding_{nullptr};
    QPushButton* record_{nullptr};
    QWidget* clash_{nullptr};
    QLabel* clashText_{nullptr};
    QLabel* status_{nullptr};

    QSet<QString> held_;
    QString category_{"All"};
    std::string selected_;
    /// A keystroke somebody asked for that another command already holds. Kept
    /// so the warning can offer to take it rather than only refusing.
    std::string wanted_;
    std::string wantedHolder_;
    bool recording_{false};
    /// Set while a pane is being refilled, so the signals that refill sets off
    /// do not refill it again.
    bool filling_{false};
};

}  // namespace zaro::app
