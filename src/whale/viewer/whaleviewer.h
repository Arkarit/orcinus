/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWER_H
#define WHALEVIEWER_H

#include "whaleviewercache.h"
#include "whaleviewerlist.h"

#include <QImage>
#include <QPointer>
#include <QWidget>

class DolphinView;
class QMovie;
class WhaleVideoPlayer;
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
 * Zoom (V-E5, V-E12): Ctrl+wheel towards the pointer, +/- towards the middle, 1 for 100 %, 0 to
 * fit again; the middle button drags a zoomed image. Paging fits again. Beyond 100 % the pixels
 * show sharp unless WhaleViewerSettings::smoothZoom() (V-E13).
 *
 * Animated images (GIF, WebP …) play over and over once loaded, and stop when paging on.
 *
 * Videos (V3, built with libmpv) play in a WhaleVideoWidget over the window, as the settings
 * say (autoplay, sound, loop); paging on stops them. Left and Right seek 5 seconds in a video,
 * Space or a click between the edges plays and pauses, M turns the sound off and on (V-E17,
 * V-E19). What is shown over the image or
 * video (the file name, notes) is painted by a transparent child on top.
 *
 * Controls of a video (V-E18): a bar at the bottom while the pointer moves or the video is
 * paused, with a button to play and pause, the progress (click or drag to seek) and the time.
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
    /** @return Whether the shown file becomes the selection, not only the current item. */
    bool movesSelection() const;
    /** @return The text shown at the bottom right now (name or note), or empty. */
    QString overlayText() const;
    /** @return The images decoded for the current file and its neighbours (V2a). */
    const WhaleViewerCache &cache() const;

    /** @return Whether the image is zoomed; otherwise it is fitted into the window. */
    bool isZoomed() const;
    /** @return Device pixels per image pixel, zoomed or fitted; 0 without an image. */
    qreal zoom() const;
    /** @return The part of the image (in its pixels) that is visible in the window. */
    QRectF visibleImageRect() const;
    /** @return The point of the image (in its pixels) at @p position in the window. */
    QPointF imagePosition(const QPointF &position) const;
    /**
     * Zooms to @p zoom device pixels per image pixel; the point of the image at @p anchor stays
     * where it is. Down to the fitted size it fits again; it goes no further down than 100 %
     * (or the fitted size, if that is smaller) and no further up than 3200 %.
     */
    void setZoom(qreal zoom, const QPointF &anchor);
    void zoomBy(qreal factor, const QPointF &anchor);
    /** Fits the image into the window again. */
    void fitToWindow();
    /** Moves a zoomed image by @p delta (window pixels), as dragging with the middle button. */
    void pan(const QPointF &delta);

    /** @return Whether the current file is a video (V3). */
    bool isVideo() const;
    /** @return The player of videos, once the viewer showed one; else nullptr. */
    WhaleVideoPlayer *videoPlayer() const;

    /** @return Whether the controls of a video show (V-E18). */
    bool controlsVisible() const;
    /** @return Where the controls are in the window: the whole bar, its play button, the area that seeks. */
    QRect controlBarRect() const;
    QRect playButtonRect() const;
    QRect seekAreaRect() const;

    /** @return Whether the file shown is an animation that plays. */
    bool isAnimating() const;
    /** @return The number of the frame of the animation shown, or -1. */
    int animationFrame() const;

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
    void mouseReleaseEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    WhaleViewer(DolphinView *source, const WhaleViewerList &list, QWidget *window);
    static void show(DolphinView *source, const WhaleViewerList &list, QWidget *window);

    void pagedTo(int direction);
    void scheduleLoad();
    void load();
    void preload();
    void slotImageReady(const QUrl &url);
    bool showFromCache();
    /** @return The current file and the neighbours to keep decoded: one back, two ahead. */
    QList<QUrl> wantedUrls() const;
    QSize availableSize() const;
    /** @return The image shown, if it is loaded; else nullptr. */
    const WhaleViewerCache::Entry *shownEntry() const;
    qreal fittedZoom() const;
    /** @return Where the whole image lies in the window, fitted or zoomed. */
    QRectF imageRect() const;
    void clampOrigin();
    void startAnimation();
    void stopAnimation();
    void startVideo();
    void stopVideo();
    /** Paints what lies over the image or video: the file name or a note. */
    void paintOverlay(QPainter &painter);
    void paintControls(QPainter &painter);
    void seekToPosition(qreal x);
    friend class WhaleViewerOverlay;
    void followInView();
    void showOverlay(const QString &text);

    QPointer<DolphinView> m_source;
    QPointer<QWidget> m_window;
    WhaleViewerList m_list;
    bool m_moveSelection = true;

    WhaleViewerCache *m_cache;
    /// What is drawn: the image of m_shownUrl fitted into the window, or nothing on an error.
    QImage m_shown;
    QUrl m_shownUrl;
    bool m_shownError = false;
    /// The direction paged in last, +1 or -1: the neighbours ahead are preloaded.
    int m_direction = 1;
    int m_wheelDelta = 0;

    bool m_upscale = false;
    bool m_smoothZoom = false;
    /// Device pixels per image pixel while zoomed; 0 when fitted.
    qreal m_zoom = 0;
    /// The top left corner of the zoomed image in the window.
    QPointF m_origin;
    bool m_panning = false;
    QPointF m_panPosition;

    QWidget *m_overlayWidget;
    /// Whether the pointer moved lately (it hides after a rest).
    bool m_pointerShown = true;
    /// Whether the left button drags in the progress of a video.
    bool m_seeking = false;
    WhaleVideoPlayer *m_videoPlayer = nullptr;
    /// Draws the video; only where there is a screen to draw on.
    QWidget *m_videoWidget = nullptr;

    QMovie *m_movie = nullptr;
    /// The frame of m_movie shown, at full size; null without an animation.
    QImage m_frame;

    QTimer *m_loadTimer = nullptr;
    QTimer *m_overlayTimer = nullptr;
    QTimer *m_cursorTimer = nullptr;
    QVariantAnimation *m_overlayFade = nullptr;
    QString m_overlayText;
    qreal m_overlayOpacity = 0;
};

#endif
