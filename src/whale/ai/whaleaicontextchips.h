/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICONTEXTCHIPS_H
#define WHALEAICONTEXTCHIPS_H

#include "whaleaicontext.h"

#include <QFontMetrics>
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
 *
 * With setAccess() (an agent in its sandbox, AI5d3), the folder, other-view and added chips
 * whose path the agent does not reach say so (property "noAccess") and get a "giveAccess"
 * button, which emits accessRequested().
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

    /** @p reaches tells whether the agent reaches a path; empty: no sandbox, nothing to mark. */
    void setAccess(const std::function<bool(const QString &)> &reaches);

    /**
     * @return @p text in at most @p width: if it does not fit, the file or folder @p name in it
     *         keeps its first characters and as much of its end as fits, with "…" between (the
     *         end of a long name says more than its middle); the rest of @p text stays. Without
     *         @p name in @p text, the end of the text goes.
     */
    static QString elidedText(const QFontMetrics &metrics, const QString &text, const QString &name, int width);

    /** All rows of chips at the current width: the transcript above gives way, not the chips. */
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

Q_SIGNALS:
    void contextChanged();
    /** The user wants the agent to reach @p path. */
    void accessRequested(const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    QWidget *createChip(const QString &objectName,
                        const QString &iconName,
                        const QString &text,
                        const QString &name,
                        const QString &toolTip,
                        const QString &accessPath,
                        const std::function<void()> &remove);

    WhaleAiContext m_window;
    QStringList m_added;
    bool m_folderRemoved = false;
    bool m_selectionRemoved = false;
    bool m_otherRemoved = false;
    std::function<bool(const QString &)> m_reaches;
};

#endif
