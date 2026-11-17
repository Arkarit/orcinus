/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAITOOLTAB_H
#define WHALEAITOOLTAB_H

#include "whaleaitoolapi.h"
#include "whaleaitoolstore.h"

#include <QImage>
#include <QJsonObject>
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
    /** The tool's backend (CV4), or nullptr while the tool is not loaded. */
    WhaleAiToolBackend *backend() const;

    /** Reads the manifest again and loads the tool's QML anew. */
    void load();
    /** Shows @p message instead of the tool, which is not loaded: its files changed since it was kept. */
    void block(const QString &message);
    bool isBlocked() const;

    /**
     * The user confirms the backend (the "Start" of the question bar): it is allowed as it is now
     * and starts. For the dev MCP, which answers for the agent tests.
     * @return Whether there was a backend waiting for it.
     */
    bool confirmBackend();
    /** The backend for the agent: {"state", "exitCode", "command", "stderr"}; empty without one. */
    QJsonObject backendStatus() const;

    // ---- for the agent's development loop (CV3) ----
    /** The root's own properties and the items with an objectName (type, text, enabled, ...). */
    QJsonObject inspect() const;
    /**
     * Acts in the tool as the user would: {"click": objectName}, {"keys": "Right"} (a key
     * sequence) or {"function": name, "arguments": [...]} (a function of the root).
     * @return What happened, or nothing with @p error set (English).
     */
    QJsonObject invoke(const QJsonObject &arguments, QString *error);
    /** What the tool shows now, or a null image when it is not loaded. */
    QImage screenshot() const;

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

private Q_SLOTS:
    void addLog(const QString &line);

private:
    void showMessage(const QString &text, int type, const QList<QAction *> &actions = {});
    /** @p wait: let a running backend end before returning (Orcinus quits), else stop it in the background. */
    void unload(bool wait = false);
    /** Starts the backend if the user allowed it, else asks. */
    void startBackend();
    /** The bar that tells about the backend: the question, or that it ended. */
    void showBackendState();
    void showBackendMessage(const QString &text, int type, const QList<QAction *> &actions);

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
    KMessageWidget *m_backendMessage;
    QList<QAction *> m_backendActions;
};

#endif
