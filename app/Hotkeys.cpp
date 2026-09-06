#include "Hotkeys.h"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <vector>

#include "zaro/ui/Actions.h"

#include "Icons.h"
#include "KeyboardMap.h"
#include "Theme.h"
#include "chrome/FlowLayout.h"

namespace zaro::app {
namespace {

QString text(std::string_view view) {
    return QString::fromUtf8(view.data(), static_cast<int>(view.size()));
}

/// The modifiers, in the order `normaliseShortcut` writes them. Everything here
/// that takes a keystroke apart or puts one together walks this list, so the
/// order lives in one place rather than in four loops that have to agree.
const QStringList& modifierOrder() {
    static const QStringList kOrder{"Ctrl", "Alt", "Shift", "Meta"};
    return kOrder;
}

/// Every category, in the order the catalogue first mentions one.
///
/// The order matters because it decides the colours: a category that keeps its
/// place keeps its tint, and somebody who has learned that green is Clip should
/// not have it move because a command was added to a different group.
const QStringList& categoryOrder() {
    static const QStringList kOrder = [] {
        QStringList found;
        for (const ui::ActionInfo& action : ui::allActions()) {
            const QString category = text(action.category);
            if (!found.contains(category)) {
                found.append(category);
            }
        }
        return found;
    }();
    return kOrder;
}

/// A colour, as the small square that stands for a category in three places.
QPixmap swatch(const QColor& colour, int size = 8) {
    const auto ratio = qApp->devicePixelRatio();
    QPixmap pixmap{QSize{size, size} * ratio};
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(QRectF{0, 0, static_cast<double>(size), static_cast<double>(size)}, 2,
                            2);
    return pixmap;
}

/// The modifiers in a keystroke, and the key left over.
///
/// "Ctrl+Shift+S" is the Shift layer of the Ctrl layer with S pressed; the map
/// needs those separately, and so does the list of what is bound where.
struct Split {
    QSet<QString> modifiers;
    QString key;
};

Split split(const std::string& shortcut) {
    Split out;
    QString rest = QString::fromStdString(shortcut);
    bool moved = true;
    while (moved && !rest.isEmpty()) {
        moved = false;
        for (const QString& modifier : modifierOrder()) {
            if (rest.startsWith(modifier + '+')) {
                out.modifiers.insert(modifier);
                rest = rest.mid(modifier.size() + 1);
                moved = true;
                break;
            }
        }
    }
    out.key = rest;
    return out;
}

QLabel* captionLabel(QWidget* parent, const QString& words) {
    auto* label = new QLabel(words, parent);
    label->setObjectName("hotkey-caption");
    return label;
}

}  // namespace

Hotkeys::Hotkeys(ui::Keymap& keymap, QWidget* parent) : QDialog{parent}, keymap_{keymap} {
    setWindowTitle("Keyboard Shortcuts");
    resize(1100, 780);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // --- what is being looked at ------------------------------------------
    //
    // Search and the modifier row do different jobs and sit together anyway:
    // one narrows the list, the other swaps the map to another layer, and both
    // are answers to "where is the thing I am after".
    auto* bar = new QWidget(this);
    bar->setObjectName("hotkey-bar");
    auto* barRow = new QHBoxLayout(bar);
    barRow->setContentsMargins(14, 10, 14, 10);
    barRow->setSpacing(10);

    search_ = new QLineEdit(bar);
    search_->setObjectName("hotkey-search");
    search_->setPlaceholderText("Search commands or keystrokes");
    search_->setClearButtonEnabled(true);
    search_->setFixedWidth(280);
    search_->addAction(QIcon{icons::pixmap(icons::Glyph::Magnifier, 13, theme::textAt(0.42))},
                       QLineEdit::LeadingPosition);
    barRow->addWidget(search_);

    for (const QString& modifier : modifierOrder()) {
        auto* chip = new QPushButton(KeyboardMap::faceFor(modifier), bar);
        chip->setObjectName("hotkey-mod");
        chip->setCheckable(true);
        chip->setToolTip("Show the " + modifier + " layer of the keyboard");
        connect(chip, &QPushButton::toggled, this, [this, modifier](bool on) {
            if (filling_) {
                return;
            }
            if (on) {
                held_.insert(modifier);
            } else {
                held_.remove(modifier);
            }
            fillKeyboard();
            fillStatus();
        });
        modifiers_.insert(modifier, chip);
        barRow->addWidget(chip);
    }

    layerLabel_ = new QLabel(bar);
    layerLabel_->setProperty("muted", true);
    barRow->addWidget(layerLabel_);
    barRow->addStretch(1);

    auto* importer = new QPushButton("Import…", bar);
    importer->setObjectName("hotkey-import");
    importer->setProperty("flat", true);
    auto* exporter = new QPushButton("Export…", bar);
    exporter->setObjectName("hotkey-export");
    exporter->setProperty("flat", true);
    barRow->addWidget(importer);
    barRow->addWidget(exporter);
    root->addWidget(bar);

    // --- the keyboard ------------------------------------------------------
    auto* well = new QWidget(this);
    well->setObjectName("hotkey-well");
    auto* wellColumn = new QVBoxLayout(well);
    wellColumn->setContentsMargins(16, 14, 16, 11);
    wellColumn->setSpacing(11);

    keyboard_ = new KeyboardMap(well);
    keyboard_->setOnPick([this](const QString& actionId) { selectAction(actionId.toStdString()); });
    keyboard_->setOnHold([this](const QString& modifier) {
        if (auto* chip = modifiers_.value(modifier, nullptr)) {
            chip->setChecked(!chip->isChecked());
        }
    });
    wellColumn->addWidget(keyboard_);

    // The legend: twelve colours mean nothing until something says what they
    // are, and the map is unreadable without it.
    auto* legend = new QWidget(well);
    auto* legendFlow = new chrome::FlowLayout(legend, 0, 12);
    for (const QString& category : categoryOrder()) {
        auto* entry = new QWidget(legend);
        auto* row = new QHBoxLayout(entry);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        auto* dot = new QLabel(entry);
        dot->setPixmap(swatch(tintFor(category.toStdString())));
        auto* name = new QLabel(category, entry);
        name->setObjectName("hotkey-legend");
        row->addWidget(dot);
        row->addWidget(name);
        legendFlow->addWidget(entry);
    }
    boundLabel_ = new QLabel(legend);
    boundLabel_->setObjectName("hotkey-legend");
    legendFlow->addWidget(boundLabel_);
    wellColumn->addWidget(legend);
    root->addWidget(well);

    // --- categories, commands, and the one being changed -------------------
    auto* middle = new QWidget(this);
    auto* middleRow = new QHBoxLayout(middle);
    middleRow->setContentsMargins(0, 0, 0, 0);
    middleRow->setSpacing(0);

    auto* side = new QWidget(middle);
    side->setObjectName("hotkey-side");
    side->setFixedWidth(198);
    auto* sideColumn = new QVBoxLayout(side);
    sideColumn->setContentsMargins(8, 8, 8, 10);
    sideColumn->setSpacing(8);

    categories_ = new QTreeWidget(side);
    categories_->setObjectName("hotkey-tree");
    categories_->setColumnCount(2);
    categories_->setHeaderHidden(true);
    categories_->setRootIsDecorated(false);
    categories_->setUniformRowHeights(true);
    categories_->setSelectionMode(QAbstractItemView::SingleSelection);
    categories_->setFocusPolicy(Qt::NoFocus);
    // The count column is fixed and the name column takes the rest: left to
    // resize itself, the pair asks for more width than the sidebar has and puts
    // a scrollbar under a list of twelve short words.
    categories_->header()->setStretchLastSection(false);
    categories_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    categories_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    categories_->setColumnWidth(1, 34);
    categories_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideColumn->addWidget(categories_, 1);

    auto* resetCategory = new QPushButton("Reset Category", side);
    resetCategory->setObjectName("hotkey-reset-category");
    auto* resetAll = new QPushButton("Restore Defaults", side);
    resetAll->setObjectName("hotkey-reset-all");
    sideColumn->addWidget(resetCategory);
    sideColumn->addWidget(resetAll);
    middleRow->addWidget(side);

    table_ = new QTableWidget(middle);
    table_->setObjectName("hotkey-table");
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({"Command", "Shortcut", "Category"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table_->setColumnWidth(1, 150);
    table_->setColumnWidth(2, 128);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setFrameShape(QFrame::NoFrame);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(26);
    middleRow->addWidget(table_, 1);

    auto* detail = new QWidget(middle);
    detail->setObjectName("hotkey-detail");
    detail->setFixedWidth(296);
    auto* detailColumn = new QVBoxLayout(detail);
    detailColumn->setContentsMargins(14, 12, 14, 12);
    detailColumn->setSpacing(9);

    detailColumn->addWidget(captionLabel(detail, "Selected"));
    selName_ = new QLabel(detail);
    selName_->setObjectName("hotkey-name");
    selName_->setWordWrap(true);
    detailColumn->addWidget(selName_);
    selWhere_ = new QLabel(detail);
    selWhere_->setProperty("muted", true);
    selWhere_->setWordWrap(true);
    detailColumn->addWidget(selWhere_);

    detailColumn->addSpacing(4);
    detailColumn->addWidget(captionLabel(detail, "Binding"));
    binding_ = new QPushButton(detail);
    binding_->setObjectName("hotkey-binding");
    binding_->setCheckable(true);
    binding_->setToolTip("Click, then press the keys you want");
    detailColumn->addWidget(binding_);

    record_ = new QPushButton("Record Binding", detail);
    record_->setObjectName("hotkey-record");
    record_->setCheckable(true);
    auto* unbind = new QPushButton("Unbind", detail);
    unbind->setObjectName("hotkey-clear");
    auto* bindingRow = new QHBoxLayout;
    bindingRow->setSpacing(6);
    bindingRow->addWidget(record_, 1);
    bindingRow->addWidget(unbind);
    detailColumn->addLayout(bindingRow);

    // The clash note. Refusing a keystroke somebody else holds is right, and
    // refusing it without offering the one thing they might want next is not:
    // this says whose it is and takes it if asked.
    clash_ = new QWidget(detail);
    clash_->setObjectName("hotkey-clash");
    clash_->setStyleSheet(
        QString("#hotkey-clash{background:%1;border:1px solid %2;border-radius:6px}")
            .arg(theme::mix(theme::surface(), theme::tint(0), 0.13).name(QColor::HexRgb),
                 theme::mix(theme::surface(), theme::tint(0), 0.34).name(QColor::HexRgb)));
    auto* clashColumn = new QVBoxLayout(clash_);
    clashColumn->setContentsMargins(9, 8, 9, 9);
    clashColumn->setSpacing(7);
    clashText_ = new QLabel(clash_);
    clashText_->setWordWrap(true);
    clashText_->setStyleSheet(
        QString("color:%1;font-size:11px").arg(theme::textAt(0.78).name(QColor::HexRgb)));
    auto* reassign = new QPushButton("Reassign", clash_);
    reassign->setObjectName("hotkey-reassign");
    clashColumn->addWidget(clashText_);
    clashColumn->addWidget(reassign, 0, Qt::AlignLeft);
    clash_->hide();
    detailColumn->addWidget(clash_);

    detailColumn->addStretch(1);
    auto* resetOne = new QPushButton("Reset to Default", detail);
    resetOne->setObjectName("hotkey-reset");
    auto* close = new QPushButton("Close", detail);
    auto* footRow = new QHBoxLayout;
    footRow->setSpacing(6);
    footRow->addWidget(resetOne, 1);
    footRow->addWidget(close);
    detailColumn->addLayout(footRow);
    middleRow->addWidget(detail);
    root->addWidget(middle, 1);

    // --- what the whole set adds up to -------------------------------------
    auto* foot = new QWidget(this);
    foot->setObjectName("hotkey-status");
    auto* footStrip = new QHBoxLayout(foot);
    footStrip->setContentsMargins(14, 5, 14, 5);
    status_ = new QLabel(foot);
    auto* note = new QLabel("Changes are saved as you make them", foot);
    footStrip->addWidget(status_);
    footStrip->addStretch(1);
    footStrip->addWidget(note);
    root->addWidget(foot);

    // --- wiring ------------------------------------------------------------
    connect(search_, &QLineEdit::textChanged, this, [this] {
        fillList();
        fillStatus();
    });
    connect(categories_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (filling_ || categories_->currentItem() == nullptr) {
            return;
        }
        category_ = categories_->currentItem()->data(0, Qt::UserRole).toString();
        fillList();
        fillStatus();
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (filling_ || table_->currentRow() < 0) {
            return;
        }
        QTableWidgetItem* cell = table_->item(table_->currentRow(), 0);
        if (cell != nullptr) {
            selectAction(cell->data(Qt::UserRole).toString().toStdString());
        }
    });
    connect(record_, &QPushButton::toggled, this, [this](bool on) { setRecording(on); });
    connect(binding_, &QPushButton::toggled, this, [this](bool on) { setRecording(on); });
    connect(unbind, &QPushButton::clicked, this, [this] {
        if (selected_.empty()) {
            return;
        }
        static_cast<void>(keymap_.clearShortcut(selected_));
        changed();
    });
    connect(reassign, &QPushButton::clicked, this, [this] {
        if (wanted_.empty() || wantedHolder_.empty()) {
            return;
        }
        // The other command loses it first, because `setShortcut` refuses a
        // keystroke that is still held -- deliberately, and this is the one
        // place that is allowed to answer the question it asks.
        static_cast<void>(keymap_.clearShortcut(wantedHolder_));
        const std::string keystroke = wanted_;
        wanted_.clear();
        wantedHolder_.clear();
        static_cast<void>(assign(selected_, keystroke));
    });
    connect(resetOne, &QPushButton::clicked, this, [this] {
        if (!selected_.empty()) {
            static_cast<void>(this->resetOne(selected_));
        }
    });
    connect(resetCategory, &QPushButton::clicked, this, [this] {
        for (const ui::ActionInfo& action : ui::allActions()) {
            if (category_ == "All" || text(action.category) == category_) {
                static_cast<void>(keymap_.resetToDefault(action.id));
            }
        }
        changed();
    });
    connect(resetAll, &QPushButton::clicked, this, [this] {
        keymap_.resetAll();
        changed();
    });
    connect(importer, &QPushButton::clicked, this, &Hotkeys::importKeymap);
    connect(exporter, &QPushButton::clicked, this, &Hotkeys::exportKeymap);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    fillCategories();
    refresh();
    if (selected_.empty() && !ui::allActions().empty()) {
        selectAction(std::string{ui::allActions().front().id});
    }
}

QColor Hotkeys::tintFor(std::string_view category) const {
    const int at = static_cast<int>(categoryOrder().indexOf(text(category)));
    return theme::tint(at < 0 ? theme::tintCount() - 1 : at);
}

std::string Hotkeys::layer() const {
    std::string prefix;
    for (const QString& modifier : modifierOrder()) {
        if (held_.contains(modifier)) {
            prefix += modifier.toStdString() + "+";
        }
    }
    return prefix;
}

void Hotkeys::fillKeyboard() {
    const Split chosen = split(keymap_.shortcutFor(selected_));
    QHash<QString, KeyboardMap::Binding> bindings;
    int bound = 0;
    for (const ui::ActionInfo& action : ui::allActions()) {
        const Split where = split(keymap_.shortcutFor(action.id));
        if (where.key.isEmpty() || where.modifiers != held_) {
            continue;
        }
        ++bound;
        bindings.insert(where.key, KeyboardMap::Binding{text(action.label),
                                                        tintFor(action.category), text(action.id)});
    }
    keyboard_->setBindings(bindings);
    keyboard_->setHeld(held_);
    keyboard_->setSelected(chosen.modifiers == held_ ? text(selected_) : QString{});

    const std::string prefix = layer();
    layerLabel_->setText(prefix.empty()
                             ? QStringLiteral("unmodified layer")
                             : QString::fromStdString(prefix.substr(0, prefix.size() - 1)) +
                                   QStringLiteral(" layer"));
    boundLabel_->setText(QString::number(bound) + " bound on this layer");
}

void Hotkeys::fillCategories() {
    filling_ = true;
    categories_->clear();
    QHash<QString, int> counts;
    for (const ui::ActionInfo& action : ui::allActions()) {
        counts[text(action.category)] += 1;
    }
    const auto add = [this](const QString& name, const QColor& colour, int count) {
        auto* item = new QTreeWidgetItem(categories_);
        item->setText(0, name);
        item->setIcon(0, QIcon{swatch(colour, 7)});
        item->setData(0, Qt::UserRole, name);
        item->setText(1, QString::number(count));
        item->setForeground(1, theme::textAt(0.38));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    };
    add("All", theme::textAt(0.40), static_cast<int>(ui::allActions().size()));
    for (const QString& category : categoryOrder()) {
        add(category, tintFor(category.toStdString()), counts.value(category));
    }
    categories_->setCurrentItem(categories_->topLevelItem(0));
    filling_ = false;
}

void Hotkeys::fillList() {
    filling_ = true;
    const QString query = search_->text().trimmed();
    QFont keyFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    keyFont.setPointSizeF(font().pointSizeF());

    std::vector<const ui::ActionInfo*> shown;
    for (const ui::ActionInfo& action : ui::allActions()) {
        if (category_ != "All" && text(action.category) != category_) {
            continue;
        }
        const QString bound = QString::fromStdString(keymap_.shortcutFor(action.id));
        if (!query.isEmpty() && !text(action.label).contains(query, Qt::CaseInsensitive) &&
            !bound.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        shown.push_back(&action);
    }

    table_->clearContents();
    table_->setRowCount(static_cast<int>(shown.size()));
    int row = 0;
    int chosenRow = -1;
    for (const ui::ActionInfo* action : shown) {
        auto* label = new QTableWidgetItem(text(action->label));
        label->setData(Qt::UserRole, text(action->id));
        table_->setItem(row, 0, label);

        const std::string shortcut = keymap_.shortcutFor(action->id);
        auto* bound = new QTableWidgetItem(QString::fromStdString(shortcut));
        bound->setFont(keyFont);
        if (!keymap_.isDefault(action->id)) {
            // Marked, because "what have I changed" is the first question
            // somebody asks of a keymap they have been editing for a month.
            bound->setForeground(theme::accent(200));
        } else if (shortcut.empty()) {
            bound->setText("—");
            bound->setForeground(theme::textAt(0.30));
        }
        table_->setItem(row, 1, bound);

        auto* where = new QTableWidgetItem(text(action->category));
        where->setIcon(QIcon{swatch(tintFor(action->category), 7)});
        where->setForeground(theme::textAt(0.50));
        table_->setItem(row, 2, where);

        if (text(action->id).toStdString() == selected_) {
            chosenRow = row;
        }
        ++row;
    }
    if (chosenRow >= 0) {
        table_->selectRow(chosenRow);
    } else {
        table_->clearSelection();
    }
    filling_ = false;
}

void Hotkeys::fillSelection() {
    const ui::ActionInfo* action = ui::findAction(selected_);
    if (action == nullptr) {
        selName_->setText("Nothing selected");
        selWhere_->setText("Pick a key on the map, or a row in the list.");
        binding_->setText("—");
        return;
    }
    selName_->setText(text(action->label));
    const std::string shortcut = keymap_.shortcutFor(action->id);
    QString where = text(action->category);
    if (!keymap_.isDefault(action->id)) {
        where += action->defaultShortcut.empty()
                     ? QStringLiteral(" · changed, had no default")
                     : QStringLiteral(" · default is ") + text(action->defaultShortcut);
    }
    selWhere_->setText(where);
    binding_->setText(recording_ ? QStringLiteral("Press keys…")
                                 : (shortcut.empty() ? QStringLiteral("Unassigned")
                                                     : QString::fromStdString(shortcut)));
}

void Hotkeys::fillStatus() {
    int bound = 0;
    for (const ui::ActionInfo& action : ui::allActions()) {
        if (!keymap_.shortcutFor(action.id).empty()) {
            ++bound;
        }
    }
    status_->setText(QString::number(table_->rowCount()) + " of " +
                     QString::number(ui::allActions().size()) + " commands shown · " + category_ +
                     " · " + QString::number(bound) + " bound in all");
}

void Hotkeys::refresh() {
    fillList();
    fillKeyboard();
    fillSelection();
    fillStatus();
}

std::string Hotkeys::selectedAction() const {
    return selected_;
}

void Hotkeys::selectAction(const std::string& actionId) {
    if (ui::findAction(actionId) == nullptr) {
        return;
    }
    selected_ = actionId;
    // The map follows the selection: choosing "Save As" from the list brings up
    // the Ctrl+Shift layer, rather than leaving somebody looking at a keyboard
    // that has nothing to do with what they just clicked.
    const Split where = split(keymap_.shortcutFor(actionId));
    if (!where.key.isEmpty()) {
        held_ = where.modifiers;
    }
    if (recording_) {
        setRecording(false);
    }
    clash_->hide();

    filling_ = true;
    for (auto chip = modifiers_.cbegin(); chip != modifiers_.cend(); ++chip) {
        chip.value()->setChecked(held_.contains(chip.key()));
    }
    for (int row = 0; row < table_->rowCount(); ++row) {
        QTableWidgetItem* cell = table_->item(row, 0);
        if (cell != nullptr && cell->data(Qt::UserRole).toString().toStdString() == actionId) {
            table_->selectRow(row);
            break;
        }
    }
    filling_ = false;

    fillKeyboard();
    fillSelection();
}

void Hotkeys::setRecording(bool on) {
    if (recording_ == on) {
        return;
    }
    recording_ = on;
    filling_ = true;
    record_->setChecked(on);
    binding_->setChecked(on);
    filling_ = false;
    record_->setText(on ? "Listening…" : "Record Binding");
    if (on) {
        clash_->hide();
        // The keys have to reach this window rather than the table, which would
        // take the arrows for itself, or the search box, which would type them.
        setFocus(Qt::OtherFocusReason);
    }
    fillSelection();
}

Status Hotkeys::assign(const std::string& actionId, const std::string& keystroke) {
    if (Status bound = keymap_.setShortcut(actionId, keystroke); !bound) {
        // Said, not swallowed: the message names the command already holding
        // it, which is what somebody needs to decide what to do next.
        wanted_ = keystroke;
        wantedHolder_ = keymap_.actionFor(keystroke);
        clashText_->setText(QString::fromStdString(bound.error().message()) +
                            ". Reassigning clears the other binding.");
        clash_->setVisible(!wantedHolder_.empty());
        return bound;
    }
    clash_->hide();
    changed();
    return {};
}

Status Hotkeys::resetOne(const std::string& actionId) {
    if (Status back = keymap_.resetToDefault(actionId); !back) {
        return back;
    }
    changed();
    return {};
}

void Hotkeys::changed() {
    refresh();
    if (onChanged_) {
        onChanged_();
    }
}

void Hotkeys::keyPressEvent(QKeyEvent* event) {
    if (!recording_) {
        QDialog::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        setRecording(false);
        return;
    }
    switch (event->key()) {
        case Qt::Key_Control:
        case Qt::Key_Shift:
        case Qt::Key_Alt:
        case Qt::Key_Meta:
            // Still waiting: somebody holding Ctrl on the way to Ctrl+S has not
            // chosen anything yet.
            return;
        default:
            break;
    }
    if (selected_.empty()) {
        return;
    }
    const QKeySequence sequence{event->keyCombination()};
    auto normalised =
        ui::normaliseShortcut(sequence.toString(QKeySequence::PortableText).toStdString());
    if (!normalised) {
        clashText_->setText(QString::fromStdString(normalised.error().message()));
        clash_->show();
        return;
    }
    setRecording(false);
    static_cast<void>(assign(selected_, *normalised));
}

void Hotkeys::importKeymap() {
    const QString path = QFileDialog::getOpenFileName(this, "Import Keyboard Shortcuts", QString{},
                                                      "Keymap (*.conf);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        clashText_->setText("That file could not be opened.");
        clash_->show();
        return;
    }
    auto loaded = ui::Keymap::decode(QString::fromUtf8(file.readAll()).toStdString());
    if (!loaded) {
        // Said in the same place a refused keystroke is said, rather than in a
        // message box: the reason is one line and belongs beside the thing it
        // is about.
        clashText_->setText(QString::fromStdString(loaded.error().message()));
        clash_->show();
        return;
    }
    keymap_ = std::move(*loaded);
    changed();
}

void Hotkeys::exportKeymap() {
    const QString path = QFileDialog::getSaveFileName(this, "Export Keyboard Shortcuts",
                                                      "keymap.conf", "Keymap (*.conf)");
    if (path.isEmpty()) {
        return;
    }
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        clashText_->setText("That file could not be written.");
        clash_->show();
        return;
    }
    file.write(QByteArray::fromStdString(keymap_.encode()));
}

}  // namespace zaro::app
