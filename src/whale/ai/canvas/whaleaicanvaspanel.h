/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICANVASPANEL_H
#define WHALEAICANVASPANEL_H

#include "panels/panel.h"
#include "whaleaitoolapi.h"
#include "whaleaitoolstore.h"

class QTabWidget;
class QToolButton;
class WhaleAiToolTab;
class WhaleAiCanvasModel;
class WhaleAiCanvasPlayer;
class WhaleAiQuickWidget;

/**
 * @brief The AI canvas (CV1): where the agent shows its work.
 *
 * A panel with a tab bar: the tab "Canvas" shows what the agent of the current conversation put
 * there (images, videos, sounds, texts, files, comparisons, a watched folder, streams), newest at
 * the bottom; tools get tabs of their own later (CV2). The content is QML in a QQuickWidget, typed
 * blocks only, no code of the agent. Cleared with a new conversation. See
 * .privatedoc/ai-canvas-plan.md.
 */
class WhaleAiCanvasPanel : public Panel
{
    Q_OBJECT

public:
    explicit WhaleAiCanvasPanel(QWidget *parent = nullptr);
    ~WhaleAiCanvasPanel() override;

    WhaleAiCanvasModel *model() const;
    WhaleAiCanvasPlayer *player() const;
    WhaleAiQuickWidget *canvasWidget() const;
    /** The errors of loading the canvas' QML, empty if it loaded. */
    QStringList qmlErrors() const;

    /** Empties the canvas and stops what plays. */
    void clear();

    // ---- tools (CV2) ----
    /** Where the tools are and which are kept; before restoreKeptTools() (tests). */
    void setToolStore(const WhaleAiToolStore &store);
    const WhaleAiToolStore &toolStore() const;
    /** What tools may know and do in the window. */
    void setToolHost(const WhaleAiToolHost &host);
    /** Asks whether to keep a tool (the trust step); a dialog unless set (tests). */
    void setKeepConfirmation(const std::function<bool(const WhaleAiTool &tool, const QStringList &details)> &confirm);
    /** Opens the tabs of the kept tools; a tool whose files changed since is blocked. */
    void restoreKeptTools();
    /** Opens the tool @p id in a tab (or shows its tab). @return An error (English), or empty. */
    QString openTool(const QString &id);
    /** Closes the tab of @p id, also of a kept tool (it comes back with the next start). */
    bool closeTool(const QString &id);
    /** Keeps @p id after asking the user (trusting its files as they are). @return Whether it is kept. */
    bool keepTool(const QString &id);
    void unkeepTool(const QString &id);
    WhaleAiToolTab *toolTab(const QString &id) const;
    /** The ids of the open tool tabs, in their order. */
    QStringList openTools() const;
    /** What the trust step shows for @p tool: its files, what it runs and where it writes. */
    QStringList keepDetails(const WhaleAiTool &tool) const;

    /** A file or folder on the canvas was clicked: shown in the view. */
    Q_INVOKABLE void activate(const QString &url);
    /** Opens @p url in the program the user chose for it (sounds and videos without libmpv). */
    Q_INVOKABLE void openExternally(const QString &url);
    /** A link in a text: local files are shown in the view, others open in the browser. */
    Q_INVOKABLE void openLink(const QString &link);

public Q_SLOTS:
    /** The window's folder or selection changed: tools are told. */
    void contextChanged();

Q_SIGNALS:
    /** A local file or folder on the canvas was clicked. */
    void localUrlActivated(const QUrl &url);

protected:
    bool urlChanged() override;
    void hideEvent(QHideEvent *event) override;

private:
    WhaleAiToolTab *addToolTab(const WhaleAiTool &tool);
    void updateToolTab(WhaleAiToolTab *tab);
    void fillToolsMenu();
    void showTabMenu(const QPoint &position);

    WhaleAiCanvasModel *m_model;
    WhaleAiCanvasPlayer *m_player;
    QTabWidget *m_tabs;
    WhaleAiQuickWidget *m_canvas;
    QToolButton *m_toolsButton;
    WhaleAiToolStore m_store;
    WhaleAiToolHost m_host;
    std::function<bool(const WhaleAiTool &, const QStringList &)> m_confirmKeep;
};

#endif
