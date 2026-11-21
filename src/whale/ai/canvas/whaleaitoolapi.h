/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAITOOLAPI_H
#define WHALEAITOOLAPI_H

#include "whaleaitoolbackend.h"
#include "whaleaitoolstore.h"

#include <KFileItem>

#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <functional>

class KCoreDirLister;
class KJob;
class QTimer;
class QQmlEngine;
class WhaleAiCanvasPlayer;

/** What a tool may know and do in the window (CV2); set by the window, like the AI panel's view actions. */
struct WhaleAiToolHost {
    /** The folder of the active view. */
    std::function<QUrl()> currentFolder;
    /** The selected items of the active view. */
    std::function<QList<QUrl>()> selection;
    /** The folder of the other view of the split view, empty without one. */
    std::function<QUrl()> otherFolder;
    /** Shows @p url in the active view: a folder is opened, a file selected in its folder. */
    std::function<void(const QUrl &url)> reveal;
};

/**
 * @brief The "orcinus" object of a QML tool (CV2): what a tool may know and do.
 *
 * Reading, listing and file operations go through KIO, so changes can be undone with Orcinus'
 * Undo, deleting goes to the trash, and remote folders work. Asynchronous calls take a JavaScript
 * callback as their last argument. Paths may be given as absolute paths or URLs; URLs come back.
 * This is the intended way, not a sandbox: a tool can also write files in other ways (see
 * .privatedoc/ai-canvas-plan.md, CV0). Messages for the tool's log (load errors, failed callbacks)
 * are told with logged().
 */
class WhaleAiToolApi : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString toolName READ toolName CONSTANT)
    Q_PROPERTY(QString toolFolder READ toolFolder CONSTANT)
    Q_PROPERTY(WhaleAiCanvasPlayer *player READ player CONSTANT)
    /** The tool's backend (CV4): send(value), signal message(value), state, exitCode, restart(). */
    Q_PROPERTY(WhaleAiToolBackend *backend READ backend CONSTANT)

public:
    /** The most items list() gives. */
    static constexpr int MaxListed = 100000;
    /** The most bytes readText() reads. */
    static constexpr qint64 MaxText = 16 * 1024 * 1024;

    WhaleAiToolApi(const WhaleAiTool &tool, const WhaleAiToolStore &store, const WhaleAiToolHost &host, QWidget *dialogParent, QObject *parent = nullptr);
    ~WhaleAiToolApi() override;

    /** The engine that runs the tool's QML; needed to give JavaScript values to callbacks. */
    void setEngine(QQmlEngine *engine);

    QString toolName() const;
    QString toolFolder() const;
    WhaleAiCanvasPlayer *player() const;
    WhaleAiToolBackend *backend() const;

    // ---- context ----
    Q_INVOKABLE QString currentFolder() const;
    Q_INVOKABLE QStringList selection() const;
    /** The folder of the other split view, or "". */
    Q_INVOKABLE QString otherFolder() const;

    // ---- reading ----
    /**
     * Lists @p url: callback(items, error). Options: recursive (false), mime (a type such as audio/mpeg,
     * a group written as audio/ and an asterisk, or a list of these),
     * hidden (false), limit (10000), progress (function(count)). @return An id for cancel().
     */
    Q_INVOKABLE int list(const QString &url, const QJSValue &options, const QJSValue &callback);
    Q_INVOKABLE void cancel(int id);
    /** callback(item, error) */
    Q_INVOKABLE void item(const QString &url, const QJSValue &callback);
    /** callback(text, error); at most @p maxBytes (1 MiB if 0). */
    Q_INVOKABLE void readText(const QString &url, const QJSValue &callback, qint64 maxBytes = 0);

    // ---- changing, with undo ----
    /** Copies @p urls into the folder @p destination: callback(ok, error). */
    Q_INVOKABLE void copy(const QStringList &urls, const QString &destination, const QJSValue &callback = QJSValue());
    Q_INVOKABLE void move(const QStringList &urls, const QString &destination, const QJSValue &callback = QJSValue());
    /** Renames @p url to @p newName in its folder. */
    Q_INVOKABLE void rename(const QString &url, const QString &newName, const QJSValue &callback = QJSValue());
    Q_INVOKABLE void trash(const QStringList &urls, const QJSValue &callback = QJSValue());
    Q_INVOKABLE void mkdir(const QString &url, const QJSValue &callback = QJSValue());
    /** Writes a new text file; an existing one only with overwrite, and then the old one goes to the trash first. */
    Q_INVOKABLE void writeText(const QString &url, const QString &text, bool overwrite = false, const QJSValue &callback = QJSValue());

    // ---- the view ----
    /** Shows @p url in the active view (a file selected in its folder). */
    Q_INVOKABLE void reveal(const QString &url);

    // ---- user interface ----
    /** Asks for a folder: callback(url), "" if cancelled. */
    Q_INVOKABLE void pickFolder(const QJSValue &callback, const QString &start = QString());
    /** Asks for files: callback(urls). Options: multiple (true), mime (a filter, e.g. image/png). */
    Q_INVOKABLE void pickFiles(const QJSValue &callback, const QJSValue &options = QJSValue());
    /** A short message above the tool. */
    Q_INVOKABLE void notify(const QString &text);

    // ---- settings of the tool, kept across starts ----
    /** The value of @p key, else the manifest's default, else @p fallback. */
    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);

    /** @return The URL of @p text, an absolute path or a URL; invalid for anything else. */
    static QUrl urlOf(const QString &text);

Q_SIGNALS:
    /** The folder, the selection or the other folder of the window changed. */
    void contextChanged();
    /**
     * Files in the current folder were added, removed or changed (not on entering another folder:
     * contextChanged): {folder, added: [items], removed: [items], changed: [items]}, items as list()
     * gives them, hidden files left out. Changes within a moment come together; the folder is only
     * watched while a tool listens.
     */
    void folderContentsChanged(const QJSValue &changes);
    void notified(const QString &text);
    void logged(const QString &message);

protected:
    void connectNotify(const QMetaMethod &signal) override;
    void disconnectNotify(const QMetaMethod &signal) override;

private:
    /** Watches the current folder while folderContentsChanged() has listeners. */
    void updateWatch();
    void addChange(const KFileItem &item, int kind);
    void reportChanges();
    QJSValue itemValue(const KFileItem &item) const;
    void call(const QJSValue &callback, const QJSValueList &arguments);
    /** Runs @p job, records it for undo, and tells @p callback (ok, error). */
    void finish(KJob *job, const QJSValue &callback);
    QJSValue itemValue(const QUrl &url, const QString &name, bool isDir, qint64 size, qint64 modified, const QString &mime) const;
    /** Calls @p callback later with (null, error), or (false, error) for a change. */
    void failLater(const QJSValue &callback, const QString &error, bool change = false);

    WhaleAiTool m_tool;
    WhaleAiToolStore m_store;
    WhaleAiToolHost m_host;
    QPointer<QWidget> m_dialogParent;
    QPointer<QQmlEngine> m_engine;
    WhaleAiCanvasPlayer *m_player;
    WhaleAiToolBackend *m_backend;
    QHash<int, QPointer<KJob>> m_lists;
    KCoreDirLister *m_watcher = nullptr;
    QUrl m_watched;
    bool m_watchReady = false; ///< the first listing of m_watched is through; changes count from then
    struct Change {
        int kind;
        KFileItem item;
    };
    QHash<QUrl, Change> m_changes;
    QTimer *m_changeTimer = nullptr;
    int m_nextId = 1;
};

#endif
