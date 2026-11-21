/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAITOOLSTORE_H
#define WHALEAITOOLSTORE_H

#include <KSharedConfig>

#include <QJsonObject>
#include <QStringList>

#include <optional>

/**
 * A tool of the AI canvas (CV2): a folder with a manifest, tool.json.
 *
 * {"name": "Sound Triage", "icon": "audio-x-generic", "description": "...", "entry": "main.qml",
 *  "writes": ["the Target folder"], "settings": {"target": ""}}
 *
 * The id is the folder's name. "writes" only tells the user where the tool writes; Orcinus shows
 * it and does not enforce it. "backend" names a program the tool talks to, see WhaleAiToolBackend
 * (CV4): {"backend": {"command": ["python3", "backend.py"]}}.
 */
struct WhaleAiTool {
    QString id;
    QString folder;
    QString name;
    QString icon;
    QString description;
    /** The QML file to load, relative to the folder. */
    QString entry;
    QStringList writes;
    /** The tool's own settings and their defaults. */
    QJsonObject settings;
    /** The backend (CV4) as the manifest gives it (checked); empty without one. */
    QJsonObject backend;

    QString entryPath() const;
};

/**
 * @brief The tools of the AI canvas and which of them the user kept (CV2).
 *
 * Tools live in ~/.local/share/orcinus/ai/tools/<id>/, one folder each. A tool the user keeps
 * ("pinned") starts by itself when its tab comes back (the store keeps the open tabs too); keeping
 * it is the trust step, so the store remembers a fingerprint of its files then, and a kept tool
 * whose files changed since is not loaded until the user looks at it again. See .privatedoc/ai-canvas-plan.md.
 */
class WhaleAiToolStore
{
public:
    /** The user's tools in the standard place, with Orcinus' configuration. */
    WhaleAiToolStore();
    /** Tools in @p root, kept-state in @p config (tests). */
    WhaleAiToolStore(const QString &root, KSharedConfigPtr config);

    static QString defaultRoot();
    QString root() const;

    /** The ids of all tools with a valid manifest, sorted. */
    QStringList ids() const;
    /** The tool @p id, or nothing with @p error set (English). */
    std::optional<WhaleAiTool> tool(const QString &id, QString *error = nullptr) const;
    /** @return Whether @p id can be the name of a tool folder: lower case letters, digits, '-' and '_'. */
    static bool isValidId(const QString &id);

    /** The files of the tool, relative to its folder, sorted (hidden ones left out). */
    QStringList files(const QString &id) const;
    /** A fingerprint of the tool's files (names and contents). */
    QString fingerprint(const QString &id) const;
    /**
     * The fingerprint the confirmation of a backend covers (CV4): every file but the .qml files.
     * QML cannot start programs and only talks to the approved backend, so work on the UI does
     * not ask again; a change of the backend, its files or the manifest does.
     */
    QString backendFingerprint(const QString &id) const;

    /** The kept tools, in the order of their tabs. */
    QStringList pinned() const;
    /** Keeps @p id and trusts its files as they are now. */
    void pin(const QString &id);
    void unpin(const QString &id);
    /** @return Whether @p id is kept and its files are still the ones the user trusted. */
    bool isTrusted(const QString &id) const;

    /** @return Whether the open tabs were saved yet (setOpenTabs()). */
    bool hasOpenTabs() const;
    /** The tools open in tabs when last saved, in their order. */
    QStringList openTabs() const;
    /** The tool of the tab shown when last saved; empty: the canvas. */
    QString currentTab() const;
    void setOpenTabs(const QStringList &ids, const QString &current);

    /** @return Whether the user closed the hint to keep @p id. */
    bool isKeepHintDismissed(const QString &id) const;
    void dismissKeepHint(const QString &id);
    /** The hint to keep @p id shows again. */
    void resetKeepHint(const QString &id);

    /** The user confirmed the backend of @p id as it is now. */
    void allowBackend(const QString &id);
    /** @return Whether the backend of @p id may start: confirmed as it is, or the tool is trusted. */
    bool isBackendAllowed(const QString &id) const;

    /** Where Orcinus' example tools are installed (share/orcinus/ai/example-tools in the data folders). */
    static QStringList exampleRoots();
    /** The example tools in @p roots, by id (the first root wins), sorted by name. */
    QList<WhaleAiTool> examples(const QStringList &roots) const;
    /**
     * Copies @p example into the user's tools, as a tool of their own (to change, keep or delete).
     * An existing tool with its id stays as it is. @return An error (English), or empty.
     */
    QString addExample(const WhaleAiTool &example) const;

    /** The persistent settings of the tool @p id (the "settings" group of a tool). */
    KConfigGroup settingsGroup(const QString &id) const;

private:
    KConfigGroup group() const;
    QString fingerprint(const QString &id, bool withQml) const;

    QString m_root;
    KSharedConfigPtr m_config;
};

#endif
