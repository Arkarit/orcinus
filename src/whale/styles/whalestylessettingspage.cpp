/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalestylessettingspage.h"

#include "dolphinmainwindow.h"
#include "whalestyleelement.h"
#include "whalestyleelements.h"
#include "whalestylemanager.h"
#include "whalestyles.h"
#include "whalestylestore.h"

#include <KActionCollection>
#include <KKeySequenceWidget>
#include <KLocalizedString>
#include <KMessageBox>

#include <QAbstractItemModel>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
constexpr int IdRole = Qt::UserRole;
constexpr int KeyRole = Qt::UserRole + 1;
}

WhaleStylesSettingsPage::WhaleStylesSettingsPage(DolphinMainWindow *window, QWidget *parent)
    : SettingsPageBase(parent)
    , m_window(window)
{
    auto topLayout = new QHBoxLayout(this);

    // The styles in their order
    auto listLayout = new QVBoxLayout();
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("styleList"));
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    listLayout->addWidget(m_list);

    auto listButtons = new QHBoxLayout();
    m_addButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:button", "New from Window"), this);
    m_addButton->setObjectName(QStringLiteral("addStyle"));
    m_addButton->setToolTip(i18nc("@info:tooltip", "Add a style with the current state of the window"));
    m_removeButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), i18nc("@action:button", "Delete"), this);
    m_removeButton->setObjectName(QStringLiteral("removeStyle"));
    m_upButton = new QPushButton(QIcon::fromTheme(QStringLiteral("go-up")), QString(), this);
    m_upButton->setObjectName(QStringLiteral("moveStyleUp"));
    m_upButton->setToolTip(i18nc("@info:tooltip", "Move up"));
    m_downButton = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), QString(), this);
    m_downButton->setObjectName(QStringLiteral("moveStyleDown"));
    m_downButton->setToolTip(i18nc("@info:tooltip", "Move down"));
    listButtons->addWidget(m_addButton);
    listButtons->addWidget(m_removeButton);
    listButtons->addStretch();
    listButtons->addWidget(m_upButton);
    listButtons->addWidget(m_downButton);
    listLayout->addLayout(listButtons);
    topLayout->addLayout(listLayout, 2);

    // The chosen style
    m_details = new QWidget(this);
    auto detailsLayout = new QVBoxLayout(m_details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    auto form = new QFormLayout();
    m_name = new QLineEdit(m_details);
    m_name->setObjectName(QStringLiteral("styleName"));
    form->addRow(i18nc("@label:textbox", "Name:"), m_name);
    m_shortcut = new KKeySequenceWidget(m_details);
    m_shortcut->setObjectName(QStringLiteral("styleShortcut"));
    m_shortcut->setModifierlessAllowed(false);
    form->addRow(i18nc("@label", "Shortcut:"), m_shortcut);
    detailsLayout->addLayout(form);

    auto elementsLabel = new QLabel(i18nc("@label", "The style sets:"), m_details);
    detailsLayout->addWidget(elementsLabel);
    m_elements = new QTreeWidget(m_details);
    m_elements->setObjectName(QStringLiteral("styleElements"));
    m_elements->setColumnCount(2);
    m_elements->setHeaderLabels({i18nc("@title:column", "Element"), i18nc("@title:column", "Value")});
    m_elements->setRootIsDecorated(false);
    m_elements->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_elements->setToolTip(i18nc("@info:tooltip", "An element without a check mark is left as it is when the style is applied."));
    detailsLayout->addWidget(m_elements);

    auto detailButtons = new QHBoxLayout();
    m_captureButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action:button", "Capture Again"), m_details);
    m_captureButton->setObjectName(QStringLiteral("captureStyle"));
    m_captureButton->setToolTip(i18nc("@info:tooltip", "Take the values from the current state of the window"));
    m_applyButton = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")), i18nc("@action:button", "Apply to Window"), m_details);
    m_applyButton->setObjectName(QStringLiteral("applyStyle"));
    m_applyButton->setToolTip(i18nc("@info:tooltip", "Apply the style to the window now, as shown here"));
    detailButtons->addWidget(m_captureButton);
    detailButtons->addStretch();
    detailButtons->addWidget(m_applyButton);
    detailsLayout->addLayout(detailButtons);

    m_hint = new QLabel(m_details);
    m_hint->setWordWrap(true);
    m_hint->setText(i18nc("@info", "Apply a style from View → Styles or by its shortcut."));
    detailsLayout->addWidget(m_hint);
    topLayout->addWidget(m_details, 3);

    connect(m_list, &QListWidget::currentRowChanged, this, &WhaleStylesSettingsPage::showCurrentStyle);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, &WhaleStylesSettingsPage::takeListOrder);
    connect(m_addButton, &QPushButton::clicked, this, &WhaleStylesSettingsPage::addFromWindow);
    connect(m_removeButton, &QPushButton::clicked, this, &WhaleStylesSettingsPage::removeCurrent);
    connect(m_upButton, &QPushButton::clicked, this, [this]() {
        moveCurrent(-1);
    });
    connect(m_downButton, &QPushButton::clicked, this, [this]() {
        moveCurrent(1);
    });
    connect(m_name, &QLineEdit::editingFinished, this, &WhaleStylesSettingsPage::renameCurrent);
    connect(m_shortcut, &KKeySequenceWidget::keySequenceChanged, this, &WhaleStylesSettingsPage::changeShortcut);
    connect(m_elements, &QTreeWidget::itemChanged, this, &WhaleStylesSettingsPage::elementToggled);
    connect(m_captureButton, &QPushButton::clicked, this, &WhaleStylesSettingsPage::captureAgain);
    connect(m_applyButton, &QPushButton::clicked, this, &WhaleStylesSettingsPage::applyToWindow);

    loadStyles();
}

WhaleStylesSettingsPage::~WhaleStylesSettingsPage() = default;

void WhaleStylesSettingsPage::applySettings()
{
    renameCurrent();
    WhaleStyles *styles = WhaleStyles::self();
    if (styles->editableStore().replaceStyles(m_styles)) {
        styles->save();
    }
}

void WhaleStylesSettingsPage::restoreDefaults()
{
}

void WhaleStylesSettingsPage::loadStyles()
{
    // Another process may have changed them.
    WhaleStyles::self()->reload();
    m_styles = WhaleStyles::self()->store().styles();
    fillList(m_styles.isEmpty() ? QString() : m_styles.first().id);
}

void WhaleStylesSettingsPage::fillList(const QString &currentId)
{
    m_filling = true;
    m_list->clear();
    int current = -1;
    for (const WhaleStyle &style : std::as_const(m_styles)) {
        auto item = new QListWidgetItem(style.name, m_list);
        item->setData(IdRole, style.id);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
        if (style.id == currentId) {
            current = m_list->count() - 1;
        }
    }
    m_list->setCurrentRow(current);
    m_filling = false;
    showCurrentStyle();
}

void WhaleStylesSettingsPage::showCurrentStyle()
{
    if (m_filling) {
        return;
    }
    m_filling = true;
    const WhaleStyle *style = currentStyle();
    m_details->setEnabled(style);
    m_name->setText(style ? style->name : QString());
    m_shortcut->setKeySequence(style ? style->shortcut : QKeySequence());

    m_elements->clear();
    if (style) {
        // In the order of applying; elements of a newer Whale at the end.
        const QList<const WhaleStyleElement *> elements = WhaleStyleElements::self().elements();
        for (const WhaleStyleElement *element : elements) {
            auto item = new QTreeWidgetItem(m_elements);
            item->setData(0, KeyRole, element->key());
            item->setText(0, element->title());
            const auto it = style->elements.constFind(element->key());
            if (it == style->elements.cend()) {
                const QString missing = element->valueIfMissing();
                item->setText(1, missing.isEmpty() ? i18nc("@item:intext style element", "Not captured") : element->describe(missing));
                item->setFlags(Qt::ItemIsSelectable);
                item->setToolTip(0,
                                 missing.isEmpty() ? i18nc("@info:tooltip", "Not part of this style. Capture it again to add it.")
                                                   : i18nc("@info:tooltip", "Not part of this style, so applied like this. Capture it again to change it."));
                continue;
            }
            item->setText(1, element->describe(it->value));
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(0, it->enabled ? Qt::Checked : Qt::Unchecked);
            if (m_window && !element->isAvailable(m_window)) {
                // F1: e.g. the style of a view with a style for each folder.
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                item->setToolTip(0, i18nc("@info:tooltip", "Not applied in the current settings (only with one view style for all folders)."));
            }
        }
        for (auto it = style->elements.cbegin(); it != style->elements.cend(); ++it) {
            if (WhaleStyleElements::self().element(it.key())) {
                continue;
            }
            auto item = new QTreeWidgetItem(m_elements);
            item->setData(0, KeyRole, it.key());
            item->setText(0, it.key());
            item->setText(1, i18nc("@item:intext style element", "From a newer version of Orcinus"));
            item->setFlags(Qt::ItemIsSelectable);
            item->setCheckState(0, it->enabled ? Qt::Checked : Qt::Unchecked);
        }
    }
    m_filling = false;
    updateButtons();
}

void WhaleStylesSettingsPage::updateButtons()
{
    const int index = currentIndex();
    m_addButton->setEnabled(m_window);
    m_removeButton->setEnabled(index >= 0);
    m_upButton->setEnabled(index > 0);
    m_downButton->setEnabled(index >= 0 && index < m_styles.size() - 1);
    m_captureButton->setEnabled(index >= 0 && m_window);
    m_applyButton->setEnabled(index >= 0 && m_window);
}

int WhaleStylesSettingsPage::currentIndex() const
{
    const QListWidgetItem *item = m_list->currentItem();
    if (!item) {
        return -1;
    }
    const QString id = item->data(IdRole).toString();
    for (int i = 0; i < m_styles.size(); ++i) {
        if (m_styles.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

WhaleStyle *WhaleStylesSettingsPage::currentStyle()
{
    const int index = currentIndex();
    return index < 0 ? nullptr : &m_styles[index];
}

void WhaleStylesSettingsPage::markChanged()
{
    Q_EMIT changed();
}

void WhaleStylesSettingsPage::addFromWindow()
{
    if (!m_window) {
        return;
    }
    QStringList ids;
    QStringList names;
    for (const WhaleStyle &style : std::as_const(m_styles)) {
        ids.append(style.id);
        names.append(style.name.toLower());
    }
    // A name of its own, to be changed right away.
    QString name = i18nc("@item name of a new style", "New Style");
    for (int i = 2; names.contains(name.toLower()); ++i) {
        name = i18nc("@item name of a new style, %1 a number", "New Style %1", i);
    }

    WhaleStyle style = WhaleStyleManager::ensure(m_window)->captureStyle();
    style.name = name;
    style.id = WhaleStyleStore::idForName(name, ids);
    m_styles.append(style);
    fillList(style.id);
    m_name->setFocus();
    m_name->selectAll();
    markChanged();
}

void WhaleStylesSettingsPage::removeCurrent()
{
    const int index = currentIndex();
    if (index < 0) {
        return;
    }
    m_styles.removeAt(index);
    const int next = std::min<int>(index, m_styles.size() - 1);
    fillList(next < 0 ? QString() : m_styles.at(next).id);
    markChanged();
}

void WhaleStylesSettingsPage::moveCurrent(int step)
{
    const int index = currentIndex();
    const int to = index + step;
    if (index < 0 || to < 0 || to >= m_styles.size()) {
        return;
    }
    const QString id = m_styles.at(index).id;
    m_styles.move(index, to);
    fillList(id);
    markChanged();
}

void WhaleStylesSettingsPage::takeListOrder()
{
    // Dragged in the list: its order becomes the order of the styles.
    QList<WhaleStyle> ordered;
    for (int row = 0; row < m_list->count(); ++row) {
        const QString id = m_list->item(row)->data(IdRole).toString();
        for (const WhaleStyle &style : std::as_const(m_styles)) {
            if (style.id == id) {
                ordered.append(style);
            }
        }
    }
    if (ordered.size() == m_styles.size()) {
        m_styles = ordered;
        updateButtons();
        markChanged();
    }
}

void WhaleStylesSettingsPage::renameCurrent()
{
    WhaleStyle *style = currentStyle();
    if (m_filling || !style) {
        return;
    }
    const QString name = m_name->text().trimmed();
    if (name.isEmpty()) {
        // A style needs a name.
        m_name->setText(style->name);
        return;
    }
    if (name == style->name) {
        return;
    }
    // The ID stays, and with it the action and its shortcut.
    style->name = name;
    m_list->currentItem()->setText(name);
    markChanged();
}

void WhaleStylesSettingsPage::changeShortcut(const QKeySequence &shortcut)
{
    WhaleStyle *style = currentStyle();
    if (m_filling || !style || shortcut == style->shortcut) {
        return;
    }
    const QString user = shortcutUser(shortcut);
    if (!user.isEmpty()) {
        KMessageBox::error(this,
                           i18nc("@info", "The shortcut %1 is already used by \"%2\".", shortcut.toString(QKeySequence::NativeText), user),
                           i18nc("@title:window", "Shortcut in Use"));
        m_filling = true;
        m_shortcut->setKeySequence(style->shortcut);
        m_filling = false;
        return;
    }
    style->shortcut = shortcut;
    markChanged();
}

QString WhaleStylesSettingsPage::shortcutUser(const QKeySequence &shortcut) const
{
    if (shortcut.isEmpty()) {
        return QString();
    }
    const int index = currentIndex();
    for (int i = 0; i < m_styles.size(); ++i) {
        if (i != index && m_styles.at(i).shortcut == shortcut) {
            return m_styles.at(i).name;
        }
    }
    if (!m_window) {
        return QString();
    }
    const QString stylePrefix = WhaleStyleManager::actionName(QString());
    const auto actions = m_window->actionCollection()->actions();
    for (const QAction *action : actions) {
        // The actions of styles have the shortcuts saved, which this page may change.
        if (action->objectName().startsWith(stylePrefix)) {
            continue;
        }
        if (action->shortcuts().contains(shortcut)) {
            return KLocalizedString::removeAcceleratorMarker(action->text());
        }
    }
    return QString();
}

void WhaleStylesSettingsPage::elementToggled(QTreeWidgetItem *item, int column)
{
    WhaleStyle *style = currentStyle();
    if (m_filling || !style || column != 0) {
        return;
    }
    const QString key = item->data(0, KeyRole).toString();
    auto it = style->elements.find(key);
    if (it == style->elements.end()) {
        return;
    }
    const bool enabled = item->checkState(0) == Qt::Checked;
    if (it->enabled != enabled) {
        it->enabled = enabled;
        markChanged();
    }
}

void WhaleStylesSettingsPage::captureAgain()
{
    WhaleStyle *style = currentStyle();
    if (!style || !m_window) {
        return;
    }
    const WhaleStyle captured = WhaleStyleManager::ensure(m_window)->recaptureStyle(*style);
    if (captured != *style) {
        *style = captured;
        markChanged();
    }
    showCurrentStyle();
}

void WhaleStylesSettingsPage::applyToWindow()
{
    renameCurrent();
    if (const WhaleStyle *style = currentStyle(); style && m_window) {
        WhaleStyleManager::ensure(m_window)->applyStyle(*style);
    }
}

#include "moc_whalestylessettingspage.cpp"
