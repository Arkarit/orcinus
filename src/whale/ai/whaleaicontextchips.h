/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICONTEXTCHIPS_H
#define WHALEAICONTEXTCHIPS_H

#include "whaleaicontext.h"

#include <QWidget>

#include <functional>

/**
 * @brief The context of the next message of the AI panel, as removable chips (AI4).
 *
 * The window's part (folder, selection, other split view) comes from setContext(); files the
 * user drops onto the input come from addFiles(). A removed folder, selection or other-view
 * chip stays away for the next message only: it comes back after messageSent(), or when its
 * content changes. Added files are used by the next message and then go.
 *
 * Each chip has the object name "chip-folder", "chip-selection", "chip-other" or "chip-added",
 * and a "remove" button. Without chips the widget hides itself.
 */
class WhaleAiContextChips : public QWidget
{
    Q_OBJECT

public:
    explicit WhaleAiContextChips(QWidget *parent = nullptr);

    void setContext(const WhaleAiContext &context);
    /** Adds local files and folders the user brought in for the next message. */
    void addFiles(const QStringList &paths);

    /** @return What the next message carries: the chips that are shown. */
    WhaleAiContext context() const;
    /** The next message went out with context(). */
    void messageSent();

Q_SIGNALS:
    void contextChanged();

private:
    void rebuild();
    QWidget *createChip(const QString &objectName, const QString &iconName, const QString &text, const QString &toolTip, const std::function<void()> &remove);

    WhaleAiContext m_window;
    QStringList m_added;
    bool m_folderRemoved = false;
    bool m_selectionRemoved = false;
    bool m_otherRemoved = false;
};

#endif
