/*
 * SPDX-FileCopyrightText: 2006-2010 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2006 Gregor Kališnik <gregor@podnapisi.net>
 * SPDX-FileCopyrightText: 2012 Stuart Citrin <ctrn3e8@gmail.com>
 * SPDX-FileCopyrightText: 2026 Alessio Bonfiglio <alessio.bonfiglio@mail.polimi.it>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "filterbar.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <KColorScheme>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPalette>
#include <QToolButton>

namespace
{
// Whale: FB2, the choices of what the filter applies to, as stored in the state config.
struct ScopeChoice {
    KFileItemModelFilter::Scope scope;
    QLatin1String name;
    QLatin1String icon;
};

constexpr ScopeChoice scopeChoices[] = {
    {KFileItemModelFilter::Scope::Files, QLatin1String("Files"), QLatin1String("text-x-generic")},
    {KFileItemModelFilter::Scope::Folders, QLatin1String("Folders"), QLatin1String("folder")},
    {KFileItemModelFilter::Scope::FilesAndFolders, QLatin1String("FilesAndFolders"), QLatin1String("folder-documents")},
};

QString scopeText(KFileItemModelFilter::Scope scope)
{
    switch (scope) {
    case KFileItemModelFilter::Scope::Files:
        return i18nc("@item:inmenu the filter applies to", "Files (Folders Stay Shown)");
    case KFileItemModelFilter::Scope::Folders:
        return i18nc("@item:inmenu the filter applies to", "Folders (Files Stay Shown)");
    case KFileItemModelFilter::Scope::FilesAndFolders:
        return i18nc("@item:inmenu the filter applies to", "Files and Folders");
    }
    return QString();
}
}

FilterBar::FilterBar(QWidget *parent)
    : AnimatedHeightWidget{parent}
{
    QWidget *contentsContainer = prepareContentsContainer();

    // Create button to lock text when changing folders
    m_lockButton = new QToolButton(contentsContainer);
    m_lockButton->setAutoRaise(true);
    m_lockButton->setCheckable(true);
    m_lockButton->setIcon(QIcon::fromTheme(QStringLiteral("object-unlocked")));
    m_lockButton->setToolTip(i18nc("@info:tooltip", "Keep Filter When Changing Folders"));
    connect(m_lockButton, &QToolButton::toggled, this, &FilterBar::slotToggleLockButton);

    // Create filter editor
    m_filterInput = new QLineEdit(contentsContainer);
    m_filterInput->setLayoutDirection(Qt::LeftToRight);
    m_filterInput->setClearButtonEnabled(true);
    m_filterInput->setPlaceholderText(i18n("Filter…"));
    connect(m_filterInput, &QLineEdit::textChanged, this, &FilterBar::filterChanged);
    connect(m_filterInput, &QLineEdit::textChanged, this, &FilterBar::updateInvalidPatternView);
    setFocusProxy(m_filterInput);

    m_invalidPatternAction = new QAction(m_filterInput);
    m_invalidPatternAction->setCheckable(false);
    m_invalidPatternAction->setIcon(QIcon::fromTheme(QStringLiteral("error-symbolic")));
    m_invalidPatternAction->setToolTip(i18n("Invalid expression"));
    m_filterInput->addAction(m_invalidPatternAction, QLineEdit::TrailingPosition);
    m_invalidPatternAction->setVisible(false);

    // Create case sensitive button
    m_caseSensitiveButton = new QToolButton(contentsContainer);
    m_caseSensitiveButton->setAutoRaise(true);
    m_caseSensitiveButton->setCheckable(true);
    m_caseSensitiveButton->setIcon(QIcon::fromTheme(QStringLiteral("format-text-superscript"), QIcon::fromTheme(QStringLiteral("format-text-bold"))));
    m_caseSensitiveButton->setToolTip(i18nc("@info:tooltip", "Match case"));
    connect(m_caseSensitiveButton, &QToolButton::toggled, this, &FilterBar::caseSensitiveChanged);
    connect(m_caseSensitiveButton, &QToolButton::toggled, this, &FilterBar::updateInvalidPatternView);

    // Create filter mode combobox
    m_filterModeComboBox = new QComboBox(contentsContainer);
    m_filterModeComboBox->addItem(i18nc("@item:inlistbox", "Plain Text"), KFileItemModelFilter::FilterMode::PlainText);
    m_filterModeComboBox->addItem(i18nc("@item:inlistbox", "Glob Pattern"), KFileItemModelFilter::FilterMode::Glob);
    m_filterModeComboBox->addItem(i18nc("@item:inlistbox", "Regular Expression"), KFileItemModelFilter::FilterMode::Regex);
    // Whale: several glob patterns
    m_filterModeComboBox->setItemData(m_filterModeComboBox->findData(KFileItemModelFilter::FilterMode::Glob),
                                      i18nc("@info:tooltip",
                                            "* stands for any text and ? for one character. Several patterns separated by spaces, commas or semicolons "
                                            "show the items that match one of them, for example: *.jpg *.png"),
                                      Qt::ToolTipRole);
    connect(m_filterModeComboBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        Q_EMIT filterModeChanged(m_filterModeComboBox->itemData(index).value<KFileItemModelFilter::FilterMode>());
    });
    connect(m_filterModeComboBox, &QComboBox::currentIndexChanged, this, &FilterBar::updateInvalidPatternView);

    // Whale: FB2, what the filter applies to
    m_scopeButton = new QToolButton(contentsContainer);
    m_scopeButton->setAutoRaise(true);
    m_scopeButton->setPopupMode(QToolButton::InstantPopup);
    auto scopeMenu = new QMenu(m_scopeButton);
    auto scopeGroup = new QActionGroup(scopeMenu);
    for (const ScopeChoice &choice : scopeChoices) {
        QAction *action = scopeMenu->addAction(QIcon::fromTheme(choice.icon), scopeText(choice.scope));
        action->setCheckable(true);
        action->setData(QVariant::fromValue(static_cast<int>(choice.scope)));
        scopeGroup->addAction(action);
    }
    connect(scopeGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        setFilterScope(static_cast<KFileItemModelFilter::Scope>(action->data().toInt()));
    });
    m_scopeButton->setMenu(scopeMenu);

    // Create close button
    QToolButton *closeButton = new QToolButton(contentsContainer);
    closeButton->setAutoRaise(true);
    closeButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-close")));
    closeButton->setToolTip(i18nc("@info:tooltip", "Hide Filter Bar"));
    connect(closeButton, &QToolButton::clicked, this, &FilterBar::closeRequest);

    // Apply layout
    QHBoxLayout *hLayout = new QHBoxLayout(contentsContainer);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->addWidget(m_lockButton);
    hLayout->addWidget(m_filterInput);
    hLayout->addWidget(m_caseSensitiveButton);
    hLayout->addWidget(m_filterModeComboBox);
    hLayout->addWidget(m_scopeButton); // Whale: FB2
    hLayout->addWidget(closeButton);

    setTabOrder({m_lockButton, m_caseSensitiveButton, m_filterModeComboBox, m_scopeButton, closeButton, m_filterInput});

    KConfigGroup filterBarConfig(KSharedConfig::openStateConfig(), QStringLiteral("FilterBar"));
    bool caseSensitiveEnabled = filterBarConfig.readEntry("caseSensitive", false);
    int filterModeComboBoxIndex = filterBarConfig.readEntry("filterMode", m_filterModeComboBox->findData(KFileItemModelFilter::FilterMode::Glob));
    m_caseSensitiveButton->setChecked(caseSensitiveEnabled);
    m_filterModeComboBox->setCurrentIndex(filterModeComboBoxIndex);

    // Whale: FB1, the filter is kept when changing folders unless the user chose otherwise. Written
    // at once, so the filter bars of views opened later start with it.
    m_lockButton->setChecked(filterBarConfig.readEntry("keepFilter", true));

    // Whale: FB2, files by default; written at once like the lock.
    const QString scopeName = filterBarConfig.readEntry("filterScope", QStringLiteral("Files"));
    for (const ScopeChoice &choice : scopeChoices) {
        if (scopeName == choice.name) {
            m_scope = choice.scope;
        }
    }
    updateScopeButton();
    connect(m_lockButton, &QToolButton::toggled, this, [](bool checked) {
        KConfigGroup config(KSharedConfig::openStateConfig(), QStringLiteral("FilterBar"));
        config.writeEntry("keepFilter", checked);
    });
}

FilterBar::~FilterBar()
{
    KConfigGroup filterBarConfig(KSharedConfig::openStateConfig(), QStringLiteral("FilterBar"));
    filterBarConfig.writeEntry("caseSensitive", this->m_caseSensitiveButton->isChecked());
    filterBarConfig.writeEntry("filterMode", this->m_filterModeComboBox->currentIndex());
}

void FilterBar::closeFilterBar()
{
    setVisible(false, WithAnimation);
    clear();
    // Whale: FB1, closing keeps the lock as it is.
}

void FilterBar::selectAll()
{
    m_filterInput->selectAll();
}

KFileItemModelFilter::FilterMode FilterBar::filterMode() const
{
    return m_filterModeComboBox->itemData(m_filterModeComboBox->currentIndex()).value<KFileItemModelFilter::FilterMode>();
}

bool FilterBar::isCaseSensitive() const
{
    return m_caseSensitiveButton->isChecked();
}

KFileItemModelFilter::Scope FilterBar::filterScope() const
{
    return m_scope;
}

void FilterBar::setFilterScope(KFileItemModelFilter::Scope scope)
{
    if (scope == m_scope) {
        return;
    }
    m_scope = scope;
    updateScopeButton();
    for (const ScopeChoice &choice : scopeChoices) {
        if (choice.scope == scope) {
            KConfigGroup config(KSharedConfig::openStateConfig(), QStringLiteral("FilterBar"));
            config.writeEntry("filterScope", QString(choice.name));
        }
    }
    Q_EMIT filterScopeChanged(scope);
}

void FilterBar::setFilterMode(KFileItemModelFilter::FilterMode mode)
{
    const int index = m_filterModeComboBox->findData(mode);
    if (index >= 0) {
        m_filterModeComboBox->setCurrentIndex(index);
    }
}

void FilterBar::setCaseSensitive(bool caseSensitive)
{
    m_caseSensitiveButton->setChecked(caseSensitive);
}

bool FilterBar::isLocked() const
{
    return m_lockButton->isChecked();
}

void FilterBar::setLocked(bool locked)
{
    m_lockButton->setChecked(locked);
}

QString FilterBar::text() const
{
    return m_filterInput->text();
}

void FilterBar::setText(const QString &text)
{
    m_filterInput->setText(text);
}

void FilterBar::updateScopeButton()
{
    for (const ScopeChoice &choice : scopeChoices) {
        if (choice.scope == m_scope) {
            m_scopeButton->setIcon(QIcon::fromTheme(choice.icon));
        }
    }
    m_scopeButton->setToolTip(i18nc("@info:tooltip", "Filter Applies To: %1", scopeText(m_scope)));
    const auto actions = m_scopeButton->menu()->actions();
    for (QAction *action : actions) {
        action->setChecked(static_cast<KFileItemModelFilter::Scope>(action->data().toInt()) == m_scope);
    }
}

void FilterBar::clear()
{
    m_filterInput->clear();
}

void FilterBar::clearIfUnlocked()
{
    if (!m_lockButton || !(m_lockButton->isChecked())) {
        clear();
    }
}

void FilterBar::slotToggleLockButton(bool checked)
{
    if (checked) {
        m_lockButton->setIcon(QIcon::fromTheme(QStringLiteral("object-locked")));
    } else {
        m_lockButton->setIcon(QIcon::fromTheme(QStringLiteral("object-unlocked")));
        clear();
    }
}

void FilterBar::updateInvalidPatternView()
{
    bool valid = true;

    KFileItemModelFilter::FilterMode current_filter_mode = filterMode();
    if (current_filter_mode != KFileItemModelFilter::FilterMode::PlainText) {
        QRegularExpression regExp = QRegularExpression();
        QString pattern = m_filterInput->text();

        QRegularExpression::PatternOptions options =
            m_caseSensitiveButton->isChecked() ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
        if (current_filter_mode == KFileItemModelFilter::FilterMode::Regex) {
            regExp.setPattern(pattern);
            regExp.setPatternOptions(options);
        } else if (current_filter_mode == KFileItemModelFilter::FilterMode::Glob) {
            regExp.setPattern(KFileItemModelFilter::globToRegularExpression(pattern)); // Whale: several patterns
            regExp.setPatternOptions(options);
        }

        valid = regExp.isValid();
    }

    if (valid) {
        m_filterInput->setPalette(QPalette());
        m_invalidPatternAction->setVisible(false);
    } else {
        auto pal = m_filterInput->palette();
        KColorScheme::adjustBackground(pal, KColorScheme::NegativeBackground);
        m_filterInput->setPalette(pal);
        m_invalidPatternAction->setVisible(true);
    }
}

void FilterBar::showEvent(QShowEvent *event)
{
    if (!event->spontaneous()) {
        m_filterInput->setFocus();
    }
}

void FilterBar::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        if (m_filterInput->text().isEmpty()) {
            Q_EMIT closeRequest();
        } else {
            m_filterInput->clear();
        }
        return;

    case Qt::Key_Enter:
    case Qt::Key_Return:
        Q_EMIT focusViewRequest();
        return;

    case Qt::Key_Down:
    case Qt::Key_PageDown:
    case Qt::Key_Up:
    case Qt::Key_PageUp: {
        Q_EMIT focusViewRequest();
        QWidget *focusWidget = QApplication::focusWidget();
        if (focusWidget && focusWidget != this) {
            QApplication::sendEvent(focusWidget, event);
        }
        return;
    }

    default:
        break;
    }

    QWidget::keyPressEvent(event);
}

int FilterBar::preferredHeight() const
{
    return std::max({m_filterInput->sizeHint().height(),
                     m_lockButton->sizeHint().height(),
                     m_caseSensitiveButton->sizeHint().height(),
                     m_filterModeComboBox->sizeHint().height()});
}

#include "moc_filterbar.cpp"
