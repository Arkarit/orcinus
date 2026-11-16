/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAITOOLTAB_H
#define WHALEAITOOLTAB_H

#include "whaleaitoolapi.h"
#include "whaleaitoolstore.h"

#include <QWidget>

class KMessageWidget;
class QAction;
class QTimer;
class QVBoxLayout;
class WhaleAiQuickWidget;

/**
 * @brief The tab of one QML tool on the AI canvas (CV2).
 *
 * Loads the tool's QML when the tab is first shown (and again with load()), each time in a
 * fresh engine with the "orcinus" object and the import guard. Load errors, QML warnings and
 * failed callbacks go into the tool's log; the user sees errors and the tool's notify() above it.
 * A kept tool whose files changed is blocked until the user trusts it again.
 */
class WhaleAiToolTab : public QWidget
{
    Q_OBJECT

public:
    /** The most lines the log keeps. */
    static constexpr int MaxLog = 200;

    WhaleAiToolTab(const WhaleAiTool &tool, const WhaleAiToolStore &store, const WhaleAiToolHost &host, QWidget *parent = nullptr);
    ~WhaleAiToolTab() override;

    QString id() const;
    const WhaleAiTool &tool() const;
    /** @return Whether the QML is loaded without errors. */
    bool isLoaded() const;
    /** Load errors, QML warnings and failed callbacks, oldest first. */
    QStringList log() const;
    WhaleAiQuickWidget *quickWidget() const;
    WhaleAiToolApi *api() const;

    /** Reads the manifest again and loads the tool's QML anew. */
    void load();
    /** Shows @p message instead of the tool, which is not loaded: its files changed since it was kept. */
    void block(const QString &message);
    bool isBlocked() const;

Q_SIGNALS:
    void loaded(bool ok);
    void logChanged();
    /** The user wants to trust the changed tool again. */
    void trustRequested();
    /** The user does not want the changed tool any more. */
    void removeRequested();

public Q_SLOTS:
    /** The window's folder or selection changed. */
    void notifyContextChanged();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void addLog(const QString &line);
    void showMessage(const QString &text, int type, const QList<QAction *> &actions = {});
    void unload();

    WhaleAiTool m_tool;
    WhaleAiToolStore m_store;
    WhaleAiToolHost m_host;
    QVBoxLayout *m_layout;
    KMessageWidget *m_message;
    QTimer *m_messageTimer;
    WhaleAiQuickWidget *m_quick = nullptr;
    WhaleAiToolApi *m_api = nullptr;
    QStringList m_log;
    bool m_loaded = false;
    bool m_triedLoading = false;
    bool m_blocked = false;
    QList<QAction *> m_actions;
};

#endif
