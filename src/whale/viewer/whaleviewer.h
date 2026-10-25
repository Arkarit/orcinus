/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWER_H
#define WHALEVIEWER_H

#include "whaleviewerlist.h"

#include <QImage>
#include <QPixmap>
#include <QPointer>
#include <QWidget>

class DolphinView;
class QTimer;
class QVariantAnimation;

/**
 * @brief The built-in viewer: a borderless full screen window that pages through images (Whale).
 *
 * It opens on normal activation of a file it can show (V-E1) and pages through a
 * WhaleViewerList. Every file it shows becomes the current item of the view it was opened from,
 * and the selection (V-E8): there is no switch. Paging through a multiple selection keeps the
 * selection or moves it, as WhaleViewerSettings::multiSelection() says.
 *
 * No visible controls: the wheel (accumulating, so fast turns skip files without loading them),
 * arrow keys, Page Up/Down, Home/End, and clicks into the left or right fifth page; Escape
 * closes. A click elsewhere does nothing (V-E10). The file name shows briefly at the bottom, as
 * does a note at the first or last file. A file that fails to load shows its name and a message,
 * and the viewer stays there (V-E11). The pointer hides after a short rest.
 *
 * There is at most one viewer; opening another closes the one before.
 *
 * See .privatedoc/whale-viewer-plan.md.
 */
class WhaleViewer : public QWidget
{
    Q_OBJECT

public:
    ~WhaleViewer() override;

    /**
     * Installs what the views need to know of the viewer (DolphinView::setShownInternally()).
     * Idempotent.
     */
    static void install();

    /**
     * Opens @p item in the viewer if it can show it: the list is the folder of @p item in
     * @p view. The viewer goes to the screen of @p window.
     * @return Whether the viewer opened it; otherwise the caller opens it as usual.
     */
    static bool openItem(const KFileItem &item, DolphinView *view, QWidget *window);

    /**
     * Opens the items the viewer can show among @p items (several were opened at once): with
     * two or more of them, it pages through only those, from the current item of @p view;
     * a single one opens as with openItem().
     * @return The items the viewer did not take, for the caller to open as usual.
     */
    static KFileItemList openItems(const KFileItemList &items, DolphinView *view, QWidget *window);

    /** @return The open viewer, or nullptr. */
    static WhaleViewer *instance();

    const WhaleViewerList &list() const;
    /** @return The URL of the file shown, or of the one paged to and still to be shown. */
    QUrl currentUrl() const;
    /** @return Whether the image of currentUrl() is loaded and shown. */
    bool isLoaded() const;
    /** @return Whether currentUrl() failed to load (V-E11). */
    bool hasLoadError() const;
    /** @return The text shown at the bottom right now (name or note), or empty. */
    QString overlayText() const;

    /** Moves @p steps files on (backwards if negative), as the wheel and the keys do. */
    void step(int steps);
    /** Goes to the file @p index of the list, as Home and End do. */
    void showIndex(int index);

Q_SIGNALS:
    /** The file paged to changed. */
    void currentChanged(const QUrl &url);
    /** The image of @p url was loaded, or failed to load (hasLoadError()). */
    void loadFinished(const QUrl &url);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    WhaleViewer(DolphinView *source, const WhaleViewerList &list, QWidget *window);
    static void show(DolphinView *source, const WhaleViewerList &list, QWidget *window);

    void scheduleLoad();
    void load();
    void followInView();
    void showOverlay(const QString &text);
    void updateScaled();

    QPointer<DolphinView> m_source;
    QPointer<QWidget> m_window;
    WhaleViewerList m_list;
    bool m_moveSelection = true;

    QImage m_image;
    QPixmap m_scaled;
    QUrl m_loadedUrl;
    bool m_loadError = false;
    int m_generation = 0;
    int m_wheelDelta = 0;

    QTimer *m_loadTimer = nullptr;
    QTimer *m_overlayTimer = nullptr;
    QTimer *m_cursorTimer = nullptr;
    QVariantAnimation *m_overlayFade = nullptr;
    QString m_overlayText;
    qreal m_overlayOpacity = 0;
};

#endif
