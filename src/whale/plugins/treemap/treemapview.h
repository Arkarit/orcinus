/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TREEMAPVIEW_H
#define TREEMAPVIEW_H

#include "treemaplayout.h"
#include "treemaprenderer.h"
#include "whaleselection.h"
#include "whaletreescanner.h"
#include "whaleview.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QTimer;
class QToolButton;
class TreemapView;

/**
 * The area that shows the treemap, and its mouse and keyboard input.
 *
 * The layout is made again only when the shown tree or the size changes, and the
 * image only when the layout or a display option changes (both lazily, before the next
 * paint or hit test). Hovering and selecting only paint outlines on top of the image.
 */
class TreemapCanvas : public QWidget
{
public:
    explicit TreemapCanvas(TreemapView *view);

    /// The tree or the root of the layout has changed.
    void invalidateLayout();
    /// A display option (colors, labels) has changed.
    void invalidateImage();

    /// Lays out and renders now if needed (also for hit tests).
    void ensureLayout();
    const TreemapLayout &layout() const
    {
        return m_layout;
    }

    /// The index of the cell under @p position, -1 for none.
    int cellAt(const QPointF &position);
    /// The cell that shows @p url, or the cell of the deepest folder above it that is shown.
    int cellFor(const QUrl &url);
    /// Sets the hovered cell (-1 for none) and repaints its outline.
    void setHoveredCell(int index);
    int hoveredCell() const
    {
        return m_hoveredCell;
    }
    /// The cell of the view's selection (cellFor()), -1 for none.
    int selectedCell();

    qint64 lastLayoutMicroseconds() const
    {
        return m_layoutMicroseconds;
    }
    qint64 lastRenderMicroseconds() const
    {
        return m_renderMicroseconds;
    }
    qint64 lastPaintMicroseconds() const
    {
        return m_paintMicroseconds;
    }
    int layoutCount() const
    {
        return m_layoutCount;
    }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void paintMessage(QPainter &painter, const QString &text);

    TreemapView *const m_view;
    TreemapLayout m_layout;
    TreemapRenderer m_renderer;
    QImage m_image;
    bool m_layoutValid = false;
    bool m_imageValid = false;
    int m_hoveredCell = -1;
    int m_wheelDelta = 0;
    qint64 m_layoutMicroseconds = 0;
    qint64 m_renderMicroseconds = 0;
    qint64 m_paintMicroseconds = 0;
    int m_layoutCount = 0;
    // The cell of the view's selection, cached per layout.
    int m_selectedCell = -1;
    int m_selectedCellLayout = -1;
    QUrl m_selectedCellUrl;
};

/**
 * The widget of the treemap view: a small bar (zoom, the zoomed folder, the entry under
 * the pointer, display options) above the TreemapCanvas.
 *
 * The invokable methods describe the view for Whale's development tools, which only
 * see the widget through Qt's meta-object system; they change nothing.
 */
class TreemapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TreemapWidget(TreemapView *view);

    /// The state of the view as a map of plain values (status, roots, cells, hover, selection, ...).
    Q_INVOKABLE QVariantMap treemapState() const;
    /// The rectangle of the cell that shows @p url in canvas coordinates; empty if none does.
    Q_INVOKABLE QRect cellRect(const QUrl &url) const;
    /// The entry at @p position in canvas coordinates; empty for none or a rest cell.
    Q_INVOKABLE QUrl urlAt(const QPoint &position) const;

private:
    TreemapView *const m_view;
};

/**
 * @brief A treemap of the folder tree below the location, as cushions.
 *
 * The tree comes from the context's WhaleTreeScanner (and with it from the host's cache);
 * the view scans again when the location changes or is reloaded (F5). Each entry is a
 * rectangle with an area proportional to its total size, nested in the rectangle of its
 * folder (TreemapLayout), shaded as a cushion (TreemapRenderer).
 *
 * Input:
 * - Hover: outlines the entry and describes it in Whale's information panel and status
 *   bar (WhaleViewContext::requestItemInfo()), also below the listing.
 * - Click: selects the entry. The selection belongs to the view and may be deep (below
 *   the listing); only an entry of the listing also becomes Whale's selection. Actions on
 *   selected items stay disabled (RecursiveContent): no file operation acts on deep entries.
 * - Double click, Enter: opens the folder (or the folder of the file) as the new location.
 * - Wheel, +/-, the zoom button: zooms in and out of folders within the treemap, without
 *   changing the location.
 * - Arrow keys: Left/Right go to the neighbors, Up to the folder, Down to the largest entry.
 *
 * The state (saveState()) is the zoomed folder, the selected entry, the color mode and
 * whether names are shown.
 */
class TreemapView : public WhaleView
{
public:
    enum class Status {
        Unsupported, ///< not a local folder
        Scanning,
        Ready,
        Empty, ///< nothing with a size below the location
        Failed,
    };

    explicit TreemapView(WhaleViewContext *context, QObject *parent = nullptr);
    ~TreemapView() override;

    QWidget *widget() override;
    WhaleViewCapabilities capabilities() const override;
    QByteArray saveState() const override;
    void restoreState(const QByteArray &state) override;

    Status status() const
    {
        return m_status;
    }
    QString statusText() const;
    /// The root of the layout: the scanned tree, or the folder zoomed into.
    WhaleTreeNode layoutRoot() const
    {
        return m_zoomNode;
    }
    TreemapRenderer::Options renderOptions() const;

    // Input from the canvas
    void hoverCell(int index);
    void selectCell(int index);
    void selectUrl(const QUrl &url);
    void openCell(int index);
    void openSelection();
    void zoomTowards(int index);
    void zoomIn();
    void zoomOut();
    void moveSelection(int key);
    void clearSelection();

    QUrl selectedUrl() const
    {
        return m_selected;
    }
    QVariantMap state() const;
    TreemapCanvas *canvas() const
    {
        return m_canvas;
    }

private:
    void startScan(bool keepView);
    void slotFinished(const WhaleTreeNode &root);
    void slotFailed(WhaleTreeScanner::Error error, const QString &message);
    void slotSelectionChanged(const WhaleSelection &selection);
    void setStatus(Status status);
    void setZoomPath(const QStringList &path);
    void applyPendingState();
    void updateBar();
    void reportInfo(bool force = false);
    bool relativePath(const QUrl &url, QStringList *parts) const;
    QUrl urlOf(const QStringList &relativePath) const;
    bool isListed(const QUrl &url) const;
    WhaleTreeNode nodeAt(const QStringList &relativePath) const;
    QUrl folderOf(int cell) const;

    QPointer<TreemapWidget> m_widget;
    TreemapCanvas *m_canvas;
    QToolButton *m_zoomOutButton;
    QLabel *m_pathLabel;
    QLabel *m_infoLabel;
    QComboBox *m_colorModeBox;
    QCheckBox *m_labelsBox;
    QTimer *m_loadingTimer;

    WhaleTreeScanner *const m_scanner;
    Status m_status = Status::Scanning;
    QString m_error;
    int m_progress = 0;
    QUrl m_scanUrl;
    QElapsedTimer m_scanClock;
    qint64 m_lastScanMs = -1;
    int m_finishedScans = 0;
    // Shown only if the scan takes a moment, so a cache hit does not flicker.
    bool m_showLoading = false;

    WhaleTreeNode m_tree;
    QStringList m_zoomPath;
    WhaleTreeNode m_zoomNode;
    QUrl m_selected;
    QUrl m_infoUrl;

    // A restored state that waits for its tree.
    bool m_hasPendingState = false;
    QStringList m_pendingZoomPath;
    QStringList m_pendingSelection;
    bool m_pendingSelectionSet = false;

    TreemapRenderer::ColorMode m_colorMode = TreemapRenderer::ColorMode::FileType;
    bool m_showLabels = true;
};

#endif
