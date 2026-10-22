/*
 * SPDX-FileCopyrightText: 2006-2010 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2006 Gregor Kališnik <gregor@podnapisi.net>
 * SPDX-FileCopyrightText: 2012 Stuart Citrin <ctrn3e8@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FILTERBAR_H
#define FILTERBAR_H

#include "animatedheightwidget.h"
#include "kitemviews/private/kfileitemmodelfilter.h"

class QLineEdit;
class QToolButton;
class QComboBox;
class QAction;

/**
 * @brief Provides an input field for filtering the currently shown items.
 *
 * @author Gregor Kališnik <gregor@podnapisi.net>
 */
class FilterBar : public AnimatedHeightWidget
{
    Q_OBJECT

public:
    explicit FilterBar(QWidget *parent = nullptr);
    ~FilterBar() override;

    /** Called by view container to hide this **/
    void closeFilterBar();

    /**
     * Selects the whole text of the filter bar.
     */
    void selectAll();

    KFileItemModelFilter::FilterMode filterMode() const;
    bool isCaseSensitive() const;

    /**
     * Whale: FB2, the items the filter applies to (files by default, so folders stay shown).
     * The last choice is remembered for new filter bars.
     */
    KFileItemModelFilter::Scope filterScope() const;
    void setFilterScope(KFileItemModelFilter::Scope scope);

    /** Whale: FB3, the rest of the state, set by styles (as the user would). */
    void setFilterMode(KFileItemModelFilter::FilterMode mode);
    void setCaseSensitive(bool caseSensitive);
    bool isLocked() const;
    void setLocked(bool locked);
    QString text() const;
    void setText(const QString &text);

public Q_SLOTS:
    /** Clears the input field. */
    void clear();
    /** Clears the input field if the "lock button" is disabled. */
    void clearIfUnlocked();
    /** The input field is cleared also if the "lock button" is released. */
    void slotToggleLockButton(bool checked);

Q_SIGNALS:
    /**
     * Signal that reports the name filter has been
     * changed to \a nameFilter.
     */
    void filterChanged(const QString &nameFilter);

    /**
     * Emitted when the case sensitive mode has been changed
     */
    void caseSensitiveChanged(bool caseSensitive);

    /**
     * Emitted when the filter mode has been changed
     */
    void filterModeChanged(KFileItemModelFilter::FilterMode mode);

    /** Whale: FB2, emitted when the items the filter applies to have been changed. */
    void filterScopeChanged(KFileItemModelFilter::Scope scope);

    /**
     * Emitted as soon as the filterbar should get closed.
     */
    void closeRequest();

    /*
     * Emitted as soon as the focus should be returned back to the view.
     */
    void focusViewRequest();

protected:
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    /** @see AnimatedHeightWidget::preferredHeight() */
    int preferredHeight() const override;

private:
    QLineEdit *m_filterInput;
    QToolButton *m_lockButton;
    QToolButton *m_caseSensitiveButton;
    QComboBox *m_filterModeComboBox;
    QToolButton *m_scopeButton; // Whale: FB2
    KFileItemModelFilter::Scope m_scope = KFileItemModelFilter::Scope::Files; // Whale: FB2
    QAction *m_invalidPatternAction;

    /** Enable or disable the alterative view for when a pattern is invalid */
    void updateInvalidPatternView();

    /** Whale: FB2, shows the current scope on its button and in its menu. */
    void updateScopeButton();
};

#endif
