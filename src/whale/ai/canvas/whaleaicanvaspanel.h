/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICANVASPANEL_H
#define WHALEAICANVASPANEL_H

#include "panels/panel.h"

class QTabWidget;
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

    /** A file or folder on the canvas was clicked: shown in the view. */
    Q_INVOKABLE void activate(const QString &url);
    /** Opens @p url in the program the user chose for it (sounds and videos without libmpv). */
    Q_INVOKABLE void openExternally(const QString &url);
    /** A link in a text: local files are shown in the view, others open in the browser. */
    Q_INVOKABLE void openLink(const QString &link);

Q_SIGNALS:
    /** A local file or folder on the canvas was clicked. */
    void localUrlActivated(const QUrl &url);

protected:
    bool urlChanged() override;
    void hideEvent(QHideEvent *event) override;

private:
    WhaleAiCanvasModel *m_model;
    WhaleAiCanvasPlayer *m_player;
    QTabWidget *m_tabs;
    WhaleAiQuickWidget *m_canvas;
};

#endif
