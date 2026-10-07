/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "treemapview.h"

#include "whaleviewcontext.h"

#include <KFormat>
#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIODevice>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMetaEnum>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
constexpr quint8 StateVersion = 1;
// How long a scan may take before the view says it is scanning; a cache hit is faster.
constexpr int LoadingDelayMs = 150;

QString formatSize(qint64 size)
{
    return KFormat().formatByteSize(double(size));
}

QString statusName(TreemapView::Status status)
{
    switch (status) {
    case TreemapView::Status::Unsupported:
        return QStringLiteral("unsupported");
    case TreemapView::Status::Scanning:
        return QStringLiteral("scanning");
    case TreemapView::Status::Ready:
        return QStringLiteral("ready");
    case TreemapView::Status::Empty:
        return QStringLiteral("empty");
    case TreemapView::Status::Failed:
        return QStringLiteral("failed");
    }
    return {};
}
}

/**
 * A label that shortens its text in the middle when it is too narrow (QLabel clips it).
 */
class ElidingLabel : public QLabel
{
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(contentsRect(), alignment(), fontMetrics().elidedText(text(), Qt::ElideMiddle, contentsRect().width()));
    }
};

// TreemapCanvas ------------------------------------------------------------------

TreemapCanvas::TreemapCanvas(TreemapView *view)
    : m_view(view)
{
    setObjectName(QStringLiteral("whaleTreemapCanvas"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(40, 40);
}

void TreemapCanvas::invalidateLayout()
{
    m_layoutValid = false;
    m_imageValid = false;
    m_hoveredCell = -1;
    update();
}

void TreemapCanvas::invalidateImage()
{
    m_imageValid = false;
    update();
}

void TreemapCanvas::ensureLayout()
{
    if (m_layoutValid) {
        return;
    }
    QElapsedTimer clock;
    clock.start();
    m_layout.compute(m_view->status() == TreemapView::Status::Ready ? m_view->layoutRoot() : WhaleTreeNode(), QRectF(rect()));
    m_layoutMicroseconds = clock.nsecsElapsed() / 1000;
    m_layoutValid = true;
    m_imageValid = false;
    ++m_layoutCount;
}

int TreemapCanvas::cellAt(const QPointF &position)
{
    ensureLayout();
    return m_layout.cellAt(position);
}

int TreemapCanvas::cellFor(const QUrl &url)
{
    ensureLayout();
    if (!url.isLocalFile()) {
        return -1;
    }
    return m_layout.cellFor(QDir::cleanPath(url.toLocalFile()));
}

int TreemapCanvas::selectedCell()
{
    ensureLayout();
    if (m_selectedCellLayout != m_layoutCount || m_selectedCellUrl != m_view->selectedUrl()) {
        m_selectedCellUrl = m_view->selectedUrl();
        m_selectedCellLayout = m_layoutCount;
        m_selectedCell = m_selectedCellUrl.isEmpty() ? -1 : cellFor(m_selectedCellUrl);
    }
    return m_selectedCell;
}

void TreemapCanvas::setHoveredCell(int index)
{
    if (index != m_hoveredCell) {
        m_hoveredCell = index;
        update();
    }
}

void TreemapCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QElapsedTimer clock;
    clock.start();
    QPainter painter(this);

    switch (m_view->status()) {
    case TreemapView::Status::Ready:
        break;
    case TreemapView::Status::Scanning:
    case TreemapView::Status::Unsupported:
    case TreemapView::Status::Empty:
    case TreemapView::Status::Failed:
        painter.fillRect(rect(), palette().color(QPalette::Base));
        paintMessage(painter, m_view->statusText());
        return;
    }

    ensureLayout();
    if (!m_imageValid || m_image.devicePixelRatio() != devicePixelRatioF()) {
        QElapsedTimer renderClock;
        renderClock.start();
        m_image = m_renderer.render(m_layout, m_view->renderOptions(), devicePixelRatioF());
        m_renderMicroseconds = renderClock.nsecsElapsed() / 1000;
        m_imageValid = true;
    }
    painter.drawImage(QPointF(0, 0), m_image);

    const QList<TreemapLayout::Cell> &cells = m_layout.cells();
    const int selected = selectedCell();
    if (selected > 0 && selected < cells.count()) {
        // The selected entry, or dashed the folder that holds it when it is too small.
        const bool exact = cells.at(selected).node.url() == m_view->selectedUrl();
        QPen pen(palette().color(QPalette::Highlight), 3);
        pen.setStyle(exact ? Qt::SolidLine : Qt::DashLine);
        pen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(cells.at(selected).rect.adjusted(1.5, 1.5, -1.5, -1.5));
    }
    if (m_hoveredCell > 0 && m_hoveredCell < cells.count()) {
        const QRectF rect = cells.at(m_hoveredCell).rect.adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setBrush(QColor(255, 255, 255, 40));
        painter.setPen(QPen(QColor(0, 0, 0, 160), 1));
        painter.drawRect(rect);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 1));
        painter.drawRect(rect.adjusted(1, 1, -1, -1));
    }
    m_paintMicroseconds = clock.nsecsElapsed() / 1000;
}

void TreemapCanvas::paintMessage(QPainter &painter, const QString &text)
{
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap, text);
}

void TreemapCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // The hovered cell is gone with the old layout; the selection is kept by URL.
    const bool hovered = m_hoveredCell >= 0;
    invalidateLayout();
    if (hovered) {
        m_view->hoverCell(-1);
    }
}

void TreemapCanvas::mouseMoveEvent(QMouseEvent *event)
{
    m_view->hoverCell(cellAt(event->position()));
}

void TreemapCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_view->selectCell(cellAt(event->position()));
    }
    QWidget::mousePressEvent(event);
}

void TreemapCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_view->openCell(cellAt(event->position()));
    }
}

void TreemapCanvas::wheelEvent(QWheelEvent *event)
{
    if (m_view->status() != TreemapView::Status::Ready) {
        return;
    }
    m_wheelDelta += event->angleDelta().y();
    while (m_wheelDelta >= 120) {
        m_wheelDelta -= 120;
        m_view->zoomTowards(cellAt(event->position()));
    }
    while (m_wheelDelta <= -120) {
        m_wheelDelta += 120;
        m_view->zoomOut();
    }
    m_view->hoverCell(cellAt(event->position()));
    event->accept();
}

void TreemapCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        QWidget::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        m_view->moveSelection(event->key());
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        m_view->openSelection();
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        m_view->zoomIn();
        break;
    case Qt::Key_Minus:
        m_view->zoomOut();
        break;
    case Qt::Key_Escape:
        m_view->clearSelection();
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void TreemapCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    event->accept();
    if (m_view->status() != TreemapView::Status::Ready) {
        return;
    }
    if (event->reason() != QContextMenuEvent::Mouse) {
        // The Menu key: Qt sends this event before the key press itself.
        showMenuForSelection();
        return;
    }
    // As in the file list, a right click selects the entry first.
    const int index = cellAt(event->pos());
    if (index <= 0) {
        return;
    }
    m_view->selectCell(index);
    m_view->showContextMenu(m_view->selectedUrl(), event->globalPos());
}

void TreemapCanvas::showMenuForSelection()
{
    if (m_view->status() != TreemapView::Status::Ready || m_view->selectedUrl().isEmpty()) {
        return;
    }
    // At the selected entry, or the folder that shows it when it is too small.
    const int selected = selectedCell();
    const QPoint position = selected > 0 ? m_layout.cells().at(selected).rect.center().toPoint() : rect().center();
    m_view->showContextMenu(m_view->selectedUrl(), mapToGlobal(position));
}

void TreemapCanvas::leaveEvent(QEvent *event)
{
    m_view->hoverCell(-1);
    QWidget::leaveEvent(event);
}

void TreemapCanvas::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange) {
        invalidateImage();
    }
    QWidget::changeEvent(event);
}

// TreemapWidget ------------------------------------------------------------------

TreemapWidget::TreemapWidget(TreemapView *view)
    : m_view(view)
{
    setObjectName(QStringLiteral("whaleTreemapView"));
}

QVariantMap TreemapWidget::treemapState() const
{
    QVariantMap state = m_view->state();
    // The context menu the selected entry would get (names of its entries).
    QStringList menu;
    const QVariantList entries = contextMenuEntries(m_view->selectedUrl());
    for (const QVariant &entry : entries) {
        menu.append(entry.toMap().value(QStringLiteral("name")).toString());
    }
    state.insert(QStringLiteral("selectedMenu"), menu);
    return state;
}

QRect TreemapWidget::cellRect(const QUrl &url) const
{
    TreemapCanvas *canvas = m_view->canvas();
    const int index = canvas->cellFor(url);
    if (index <= 0 || canvas->layout().cells().at(index).node.url() != url) {
        return {};
    }
    return canvas->layout().cells().at(index).rect.toRect();
}

QUrl TreemapWidget::urlAt(const QPoint &position) const
{
    TreemapCanvas *canvas = m_view->canvas();
    const int index = canvas->cellAt(position);
    return index > 0 ? canvas->layout().cells().at(index).node.url() : QUrl();
}

QVariantList TreemapWidget::contextMenuEntries(const QUrl &url) const
{
    QVariantList entries;
    std::unique_ptr<QMenu> menu(m_view->createContextMenu(url, nullptr));
    if (!menu) {
        return entries;
    }
    const auto actions = menu->actions();
    for (const QAction *action : actions) {
        if (!action->isSeparator()) {
            entries.append(QVariantMap{{QStringLiteral("name"), action->objectName()},
                                       {QStringLiteral("text"), action->text()},
                                       {QStringLiteral("enabled"), action->isEnabled()}});
        }
    }
    return entries;
}

// TreemapView --------------------------------------------------------------------

TreemapView::TreemapView(WhaleViewContext *context, QObject *parent)
    : WhaleView(context, parent)
    , m_widget(new TreemapWidget(this))
    , m_canvas(new TreemapCanvas(this))
    , m_zoomOutButton(new QToolButton)
    , m_pathLabel(new ElidingLabel)
    , m_infoLabel(new ElidingLabel)
    , m_colorModeBox(new QComboBox)
    , m_labelsBox(new QCheckBox(i18nc("@option:check Treemap: show the names of entries", "Names")))
    , m_loadingTimer(new QTimer(this))
    , m_scanner(context->treeScanner())
    , m_operations(context->fileOperations())
{
    auto bar = new QWidget;
    auto barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(4, 2, 4, 2);
    barLayout->setSpacing(4);
    m_zoomOutButton->setIcon(QIcon::fromTheme(QStringLiteral("zoom-out")));
    m_zoomOutButton->setToolTip(i18nc("@info:tooltip Treemap", "Zoom out to the folder above (wheel down, −)"));
    m_zoomOutButton->setAutoRaise(true);
    m_zoomOutButton->setFocusPolicy(Qt::NoFocus);
    m_zoomOutButton->setObjectName(QStringLiteral("zoomOut"));
    connect(m_zoomOutButton, &QToolButton::clicked, this, &TreemapView::zoomOut);
    // Labels shrink with the view instead of widening it.
    m_pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_pathLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_pathLabel->setTextFormat(Qt::PlainText);
    m_pathLabel->setObjectName(QStringLiteral("zoomPath"));
    m_infoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_infoLabel->setTextFormat(Qt::PlainText);
    m_infoLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_infoLabel->setObjectName(QStringLiteral("entryInfo"));
    m_colorModeBox->addItem(i18nc("@item:inlistbox Treemap colors", "File Types"));
    m_colorModeBox->addItem(i18nc("@item:inlistbox Treemap colors", "Top Folders"));
    m_colorModeBox->setToolTip(i18nc("@info:tooltip Treemap", "Colors of the entries: by file type, or by the folder of the location they are in"));
    // Narrow, so the treemap fits into half of a split view.
    m_colorModeBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_colorModeBox->setMinimumContentsLength(8);
    m_colorModeBox->setFocusPolicy(Qt::NoFocus);
    m_colorModeBox->setObjectName(QStringLiteral("colorMode"));
    connect(m_colorModeBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_colorMode = index == 1 ? TreemapRenderer::ColorMode::TopFolder : TreemapRenderer::ColorMode::FileType;
        m_canvas->invalidateImage();
    });
    m_labelsBox->setChecked(m_showLabels);
    m_labelsBox->setFocusPolicy(Qt::NoFocus);
    m_labelsBox->setObjectName(QStringLiteral("showLabels"));
    connect(m_labelsBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_showLabels = checked;
        m_canvas->invalidateImage();
    });
    barLayout->addWidget(m_zoomOutButton);
    barLayout->addWidget(m_pathLabel, 1);
    barLayout->addWidget(m_infoLabel, 1);
    barLayout->addWidget(m_colorModeBox);
    barLayout->addWidget(m_labelsBox);

    auto layout = new QVBoxLayout(m_widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar);
    layout->addWidget(m_canvas, 1);
    m_widget->setFocusProxy(m_canvas);
    m_widget->setFocusPolicy(Qt::StrongFocus);

    m_loadingTimer->setSingleShot(true);
    m_loadingTimer->setInterval(LoadingDelayMs);
    connect(m_loadingTimer, &QTimer::timeout, this, [this]() {
        m_showLoading = true;
        m_canvas->update();
    });

    connect(m_scanner, &WhaleTreeScanner::progress, this, [this](int visited) {
        m_progress = visited;
        if (m_showLoading) {
            m_canvas->update();
        }
    });
    connect(m_scanner, &WhaleTreeScanner::finished, this, &TreemapView::slotFinished);
    connect(m_scanner, &WhaleTreeScanner::failed, this, &TreemapView::slotFailed);

    connect(context, &WhaleViewContext::urlChanged, this, [this]() {
        // A new location: a new tree, nothing zoomed or selected (a stored state of the
        // location follows through restoreState()).
        startScan(false);
    });
    connect(context, &WhaleViewContext::loadingStarted, this, [this]() {
        // Reloading the location (F5) scans again; the cache answers unless something changed.
        if (m_status != Status::Scanning && m_status != Status::Unsupported && this->context()->url() == m_scanUrl) {
            startScan(true);
        }
    });
    connect(context, &WhaleViewContext::selectionChanged, this, &TreemapView::slotSelectionChanged);
    connect(m_operations, &WhaleFileOperations::operationFinished, this, &TreemapView::slotOperationFinished);

    startScan(false);
    // Whale's selection, e.g. made in the file list before switching to the treemap.
    const QList<QUrl> selected = context->selection().selected();
    setSelected(selected.isEmpty() ? QUrl() : selected.first());
}

TreemapView::~TreemapView()
{
    // The canvas calls back into the view while it handles events; not any more.
    m_scanner->disconnect(this);
    m_operations->disconnect(this);
    delete m_widget;
}

QWidget *TreemapView::widget()
{
    return m_widget;
}

WhaleViewCapabilities TreemapView::capabilities() const
{
    using C = WhaleViewCapability;
    // No Zoom: the host's zoom actions are about the file list; the treemap zooms itself.
    // No HiddenFiles: the scan counts hidden entries, whatever "Show Hidden Files" says.
    return C::Selection | C::RecursiveContent | C::KeyboardNavigation;
}

TreemapRenderer::Options TreemapView::renderOptions() const
{
    TreemapRenderer::Options options;
    options.colorMode = m_colorMode;
    options.showLabels = m_showLabels;
    if (!m_zoomPath.isEmpty()) {
        // The colors stay those of the location's entries when zooming in.
        const QList<WhaleTreeNode> top = TreemapLayout::orderedChildren(m_tree);
        for (int position = 0; position < top.count(); ++position) {
            if (top.at(position).name() == m_zoomPath.first()) {
                options.topFolderPosition = position;
            }
        }
    }
    options.font = m_canvas->font();
    options.palette = m_canvas->palette();
    return options;
}

QString TreemapView::statusText() const
{
    switch (m_status) {
    case Status::Unsupported:
        return i18nc("@info Treemap", "The treemap can only show local folders.");
    case Status::Scanning:
        if (!m_showLoading) {
            return QString();
        }
        return m_progress > 0 ? i18nc("@info Treemap", "Scanning %1…\n%2 entries so far", m_scanUrl.toLocalFile(), m_progress)
                              : i18nc("@info Treemap", "Scanning %1…", m_scanUrl.toLocalFile());
    case Status::Empty:
        return i18nc("@info Treemap", "Nothing in this folder takes up space.");
    case Status::Failed:
        return i18nc("@info Treemap: %1 is the reason", "The folder cannot be shown as a treemap:\n%1\n\nPress F5 to try again.", m_error);
    case Status::Ready:
        break;
    }
    return QString();
}

void TreemapView::startScan(bool keepView)
{
    m_scanner->cancel();
    if (keepView && m_hasPendingState) {
        // A state is still waiting for a tree (e.g. this scan restarts one): it stays.
    } else if (keepView) {
        // Keep what the user zoomed into and selected across the new scan.
        QStringList selection;
        const bool selected = !m_selected.isEmpty() && relativePath(m_selected, &selection);
        m_pendingZoomPath = m_zoomPath;
        m_pendingSelection = selection;
        m_pendingSelectionSet = selected;
        m_hasPendingState = true;
    } else {
        m_hasPendingState = false;
        setSelected(QUrl());
    }
    m_tree = WhaleTreeNode();
    m_zoomNode = WhaleTreeNode();
    m_zoomPath.clear();
    m_error.clear();
    m_progress = 0;
    m_showLoading = false;
    m_scanUrl = context()->url();

    if (!m_scanUrl.isLocalFile()) {
        m_loadingTimer->stop();
        setStatus(Status::Unsupported);
        return;
    }
    m_scanClock.start();
    m_loadingTimer->start();
    setStatus(Status::Scanning);
    // The defaults: hidden entries count (it is about space), other file systems do not.
    m_scanner->scan(m_scanUrl);
}

void TreemapView::slotFinished(const WhaleTreeNode &root)
{
    m_loadingTimer->stop();
    m_showLoading = false;
    m_lastScanMs = m_scanClock.elapsed();
    ++m_finishedScans;
    m_tree = root;
    m_zoomNode = root;
    m_zoomPath.clear();
    if (!m_selected.isEmpty()) {
        QStringList parts;
        if (!relativePath(m_selected, &parts) || nodeAt(parts).isNull()) {
            setSelected(QUrl());
        }
    }
    setStatus(root.totalSize() > 0 ? Status::Ready : Status::Empty);
    applyPendingState();
}

void TreemapView::slotFailed(WhaleTreeScanner::Error error, const QString &message)
{
    Q_UNUSED(error)
    m_loadingTimer->stop();
    m_showLoading = false;
    m_error = message;
    setStatus(Status::Failed);
}

void TreemapView::setStatus(Status status)
{
    m_status = status;
    m_canvas->invalidateLayout();
    updateBar();
    reportInfo();
}

void TreemapView::applyPendingState()
{
    if (!m_hasPendingState || m_status != Status::Ready) {
        return;
    }
    m_hasPendingState = false;
    setZoomPath(m_pendingZoomPath);
    if (m_pendingSelectionSet) {
        const WhaleTreeNode node = nodeAt(m_pendingSelection);
        setSelected(node.isNull() ? QUrl() : node.url());
        m_canvas->update();
    }
    updateBar();
}

void TreemapView::setZoomPath(const QStringList &path)
{
    // As deep as the path still exists in the tree.
    WhaleTreeNode node = m_tree;
    QStringList resolved;
    for (const QString &name : path) {
        const QList<WhaleTreeNode> children = node.children();
        const auto child = std::find_if(children.cbegin(), children.cend(), [&name](const WhaleTreeNode &candidate) {
            return candidate.isDirectory() && candidate.name() == name;
        });
        if (child == children.cend() || child->totalSize() <= 0) {
            break;
        }
        node = *child;
        resolved.append(name);
    }
    if (resolved == m_zoomPath && node.url() == m_zoomNode.url()) {
        return;
    }
    m_zoomPath = resolved;
    m_zoomNode = node;
    m_canvas->invalidateLayout();
    updateBar();
}

QByteArray TreemapView::saveState() const
{
    QStringList zoomPath = m_zoomPath;
    QStringList selection;
    bool selected = !m_selected.isEmpty() && relativePath(m_selected, &selection);
    if (m_hasPendingState) {
        // The tree of the location is not there yet: what is waiting for it.
        zoomPath = m_pendingZoomPath;
        selection = m_pendingSelection;
        selected = m_pendingSelectionSet;
    }
    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream << StateVersion << zoomPath << selected << selection << quint8(m_colorMode == TreemapRenderer::ColorMode::TopFolder ? 1 : 0) << m_showLabels;
    return state;
}

void TreemapView::restoreState(const QByteArray &state)
{
    QDataStream stream(state);
    quint8 version = 0;
    QStringList zoomPath;
    bool selected = false;
    QStringList selection;
    quint8 colorMode = 0;
    bool showLabels = true;
    stream >> version >> zoomPath >> selected >> selection >> colorMode >> showLabels;
    if (stream.status() != QDataStream::Ok || version != StateVersion || colorMode > 1) {
        return; // unknown or damaged: keep the view as it is
    }

    m_colorModeBox->setCurrentIndex(colorMode); // updates m_colorMode
    m_labelsBox->setChecked(showLabels);
    m_pendingZoomPath = zoomPath;
    // Whale's selection (e.g. made in the file list meanwhile) is newer than a stored one.
    m_pendingSelectionSet = selected && context()->selection().isEmpty();
    m_pendingSelection = selection;
    m_hasPendingState = true;
    applyPendingState();
}

bool TreemapView::relativePath(const QUrl &url, QStringList *parts) const
{
    const QUrl location = context()->url();
    if (!url.isLocalFile() || !location.isLocalFile()) {
        return false;
    }
    const QString path = QDir::cleanPath(url.toLocalFile());
    const QString root = QDir::cleanPath(location.toLocalFile());
    if (path == root) {
        parts->clear();
        return true;
    }
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    if (!path.startsWith(prefix)) {
        return false;
    }
    *parts = path.mid(prefix.length()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return true;
}

QUrl TreemapView::urlOf(const QStringList &relativePath) const
{
    QString path = QDir::cleanPath(context()->url().toLocalFile());
    for (const QString &part : relativePath) {
        path += (path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + part;
    }
    return QUrl::fromLocalFile(path);
}

bool TreemapView::isListed(const QUrl &url) const
{
    QStringList parts;
    return relativePath(url, &parts) && parts.count() == 1;
}

WhaleTreeNode TreemapView::nodeAt(const QStringList &relativePath) const
{
    WhaleTreeNode node = m_tree;
    for (const QString &name : relativePath) {
        if (node.isNull()) {
            break;
        }
        const QList<WhaleTreeNode> children = node.children();
        const auto child = std::find_if(children.cbegin(), children.cend(), [&name](const WhaleTreeNode &candidate) {
            return candidate.name() == name;
        });
        node = child == children.cend() ? WhaleTreeNode() : *child;
    }
    return node;
}

void TreemapView::hoverCell(int index)
{
    if (index == m_canvas->hoveredCell()) {
        return;
    }
    m_canvas->setHoveredCell(index);
    updateBar();
    reportInfo();
}

void TreemapView::selectCell(int index)
{
    const QList<TreemapLayout::Cell> &cells = m_canvas->layout().cells();
    if (index <= 0 || index >= cells.count()) {
        return;
    }
    const TreemapLayout::Cell &cell = cells.at(index);
    // A rest cell stands for many small entries; its folder is selected instead.
    selectUrl(cell.isRest() ? cells.at(cell.parent).node.url() : cell.node.url());
}

void TreemapView::selectUrl(const QUrl &url)
{
    if (url == m_selected) {
        return;
    }
    setSelected(url);
    // An entry of the listing is Whale's selection too (status bar, information panel,
    // the file list after switching views). A deep entry clears Whale's selection, so no
    // invisible selection of the listing is left: the host ignores URLs it does not list.
    context()->setSelection(url.isEmpty() ? WhaleSelection() : WhaleSelection({url}, url));
    m_canvas->update();
    updateBar();
    reportInfo();
}

void TreemapView::setSelected(const QUrl &url)
{
    if (url == m_selected) {
        return;
    }
    m_selected = url;
    // Whale's actions on selected items act on it (T2).
    Q_EMIT selectedItemsChanged();
}

QList<QUrl> TreemapView::selectedItems() const
{
    // Entries below the location only; the location itself is not an item of the view.
    QStringList parts;
    if (m_selected.isEmpty() || !relativePath(m_selected, &parts) || parts.isEmpty()) {
        return {};
    }
    return {m_selected};
}

void TreemapView::clearSelection()
{
    selectUrl(QUrl());
}

QMenu *TreemapView::createContextMenu(const QUrl &url, QWidget *parent)
{
    // Entries below the location only: the location itself is Whale's (Up, the file list).
    QStringList parts;
    if (url.isEmpty() || !relativePath(url, &parts) || parts.isEmpty()) {
        return nullptr;
    }
    using Operation = WhaleFileOperations::Operation;
    const QList<QUrl> urls{url};
    // Only what can be done with the entry now (e.g. nothing that removes it from a
    // read-only folder, only "Show in Folder" if it is gone).
    const WhaleFileOperations::Operations supported = m_operations->supportedOperations(urls);
    const WhaleTreeNode node = nodeAt(parts);
    const bool folder = node.isNull() ? QFileInfo(url.toLocalFile()).isDir() : node.isDirectory();

    auto menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("whaleTreemapContextMenu"));
    auto add = [this, menu, supported](Operation operation, const QString &name, const QString &icon, const QString &text, auto function) {
        if (!(supported & operation)) {
            return;
        }
        QAction *action = menu->addAction(QIcon::fromTheme(icon), text);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, function);
    };
    WhaleFileOperations *operations = m_operations;
    add(Operation::Open,
        QStringLiteral("open"),
        QStringLiteral("document-open"),
        i18nc("@action:inmenu Treemap: open the file or enter the folder", "Open"),
        [operations, urls]() {
            operations->openItems(urls);
        });
    if (!folder) {
        add(Operation::OpenWith,
            QStringLiteral("open_with"),
            QStringLiteral("system-run"),
            i18nc("@action:inmenu Treemap", "Open With…"),
            [operations, urls]() {
                operations->openItemsWith(urls);
            });
    }
    add(Operation::ShowInFolder,
        QStringLiteral("show_in_folder"),
        QStringLiteral("document-open-folder"),
        folder ? i18nc("@action:inmenu Treemap: go to the folder that contains this folder and select it", "Show in Parent Folder")
               : i18nc("@action:inmenu Treemap: go to the folder of this file and select it", "Show in Folder"),
        [operations, url]() {
            operations->showItemInFolder(url);
        });
    menu->addSeparator();
    add(Operation::Copy, QStringLiteral("copy"), QStringLiteral("edit-copy"), i18nc("@action:inmenu Treemap", "Copy"), [operations, urls]() {
        operations->copyItems(urls);
    });
    add(Operation::Cut, QStringLiteral("cut"), QStringLiteral("edit-cut"), i18nc("@action:inmenu Treemap", "Cut"), [operations, urls]() {
        operations->cutItems(urls);
    });
    add(Operation::CopyLocation,
        QStringLiteral("copy_location"),
        QStringLiteral("edit-copy-path"),
        i18nc("@action:inmenu Treemap", "Copy Location"),
        [operations, urls]() {
            operations->copyLocations(urls);
        });
    menu->addSeparator();
    add(Operation::Rename, QStringLiteral("rename"), QStringLiteral("edit-rename"), i18nc("@action:inmenu Treemap", "Rename…"), [operations, urls]() {
        operations->renameItems(urls);
    });
    add(Operation::Trash, QStringLiteral("trash"), QStringLiteral("user-trash"), i18nc("@action:inmenu Treemap", "Move to Trash"), [operations, urls]() {
        operations->trashItems(urls);
    });
    add(Operation::Delete, QStringLiteral("delete"), QStringLiteral("edit-delete"), i18nc("@action:inmenu Treemap", "Delete"), [operations, urls]() {
        operations->deleteItems(urls);
    });
    menu->addSeparator();
    add(Operation::Properties,
        QStringLiteral("properties"),
        QStringLiteral("document-properties"),
        i18nc("@action:inmenu Treemap", "Properties"),
        [operations, urls]() {
            operations->showProperties(urls);
        });

    const auto actions = menu->actions();
    if (std::all_of(actions.cbegin(), actions.cend(), [](const QAction *action) {
            return action->isSeparator();
        })) {
        delete menu;
        return nullptr;
    }
    return menu;
}

void TreemapView::showContextMenu(const QUrl &url, const QPoint &globalPos)
{
    QMenu *menu = createContextMenu(url, m_canvas);
    if (!menu) {
        return;
    }
    menu->setAttribute(Qt::WA_DeleteOnClose);
    // Not exec(): the menu must not block the event loop (scans, D-Bus) while it is open.
    menu->popup(globalPos);
}

void TreemapView::slotOperationFinished(WhaleFileOperations::Operation operation,
                                        const QList<QUrl> &urls,
                                        WhaleFileOperations::Result result,
                                        const QList<QUrl> &newUrls)
{
    m_lastOperation = QString::fromLatin1(QMetaEnum::fromType<WhaleFileOperations::Operation>().valueToKey(int(operation))) + QLatin1Char(':')
        + QString::fromLatin1(QMetaEnum::fromType<WhaleFileOperations::Result>().valueToKey(int(result)));
    if (result == WhaleFileOperations::Result::Canceled) {
        return;
    }
    // A renamed selection stays selected under its new name.
    if (operation == WhaleFileOperations::Operation::Rename && !m_selected.isEmpty()) {
        for (int i = 0; i < urls.count() && i < newUrls.count(); ++i) {
            const QString from = QDir::cleanPath(urls.at(i).toLocalFile());
            const QString selected = QDir::cleanPath(m_selected.toLocalFile());
            if (selected == from || selected.startsWith(from + QLatin1Char('/'))) {
                setSelected(QUrl::fromLocalFile(QDir::cleanPath(newUrls.at(i).toLocalFile()) + selected.mid(from.length())));
                break;
            }
        }
    }
    // The tree is a snapshot: scan again (the cache has dropped the changed tree). Zoom and
    // selection stay as far as they still exist; a removed selection goes away.
    if (m_status != Status::Unsupported && context()->url() == m_scanUrl) {
        ++m_operationScans;
        startScan(true);
    }
}

void TreemapView::slotSelectionChanged(const WhaleSelection &selection)
{
    const QList<QUrl> selected = selection.selected();
    if (!selected.isEmpty()) {
        if (selected.first() != m_selected) {
            setSelected(selected.first());
            m_canvas->update();
            updateBar();
        }
    } else if (!m_selected.isEmpty() && isListed(m_selected)) {
        // Whale's selection of the listing went away; a deep selection is the view's own.
        setSelected(QUrl());
        m_canvas->update();
        updateBar();
    }
    // Whale shows its selection in the information panel now; the entry under the
    // pointer (or the deep selection) has to be described again.
    reportInfo(true);
}

QUrl TreemapView::folderOf(int index) const
{
    const QList<TreemapLayout::Cell> &cells = m_canvas->layout().cells();
    if (index <= 0 || index >= cells.count()) {
        return {};
    }
    const TreemapLayout::Cell &cell = cells.at(index);
    if (!cell.isRest() && cell.node.isDirectory()) {
        return cell.node.url();
    }
    return cells.at(cell.parent).node.url();
}

void TreemapView::openCell(int index)
{
    const QUrl folder = folderOf(index);
    // Files directly in the location have no other folder to open.
    if (!folder.isEmpty() && folder.adjusted(QUrl::StripTrailingSlash) != context()->url().adjusted(QUrl::StripTrailingSlash)) {
        context()->openFolder(folder);
    }
}

void TreemapView::openSelection()
{
    // Enter opens the selected entry as Whale's Open does (T1/T2): a folder becomes the
    // location, a file opens with its application.
    const QList<QUrl> urls = selectedItems();
    if (!urls.isEmpty() && m_status == Status::Ready && (m_operations->supportedOperations(urls) & WhaleFileOperations::Operation::Open)) {
        m_operations->openItems(urls);
    }
}

void TreemapView::zoomTowards(int index)
{
    const QList<TreemapLayout::Cell> &cells = m_canvas->layout().cells();
    if (index <= 0 || index >= cells.count()) {
        return;
    }
    // One level: the entry of the layout root on the way to the cell.
    while (cells.at(index).parent > 0) {
        index = cells.at(index).parent;
    }
    const TreemapLayout::Cell &cell = cells.at(index);
    if (cell.isRest() || !cell.node.isDirectory()) {
        return;
    }
    setZoomPath(m_zoomPath + QStringList{cell.node.name()});
}

void TreemapView::zoomIn()
{
    if (!m_selected.isEmpty()) {
        zoomTowards(m_canvas->selectedCell());
    }
}

void TreemapView::zoomOut()
{
    if (!m_zoomPath.isEmpty()) {
        setZoomPath(m_zoomPath.mid(0, m_zoomPath.count() - 1));
    }
}

void TreemapView::moveSelection(int key)
{
    if (m_status != Status::Ready) {
        return;
    }
    const QList<TreemapLayout::Cell> &cells = m_canvas->layout().cells();
    if (cells.isEmpty() || cells.first().childCount == 0) {
        return;
    }
    auto select = [this, &cells](int index) {
        if (index > 0 && !cells.at(index).isRest()) {
            selectUrl(cells.at(index).node.url());
        }
    };
    const int current = m_canvas->selectedCell();
    if (current <= 0) {
        select(cells.first().firstChild); // the largest entry
        return;
    }
    const TreemapLayout::Cell &cell = cells.at(current);
    const TreemapLayout::Cell &parent = cells.at(cell.parent);
    switch (key) {
    case Qt::Key_Left:
        select(current > parent.firstChild ? current - 1 : current);
        break;
    case Qt::Key_Right:
        select(current + 1 < parent.firstChild + parent.childCount ? current + 1 : current);
        break;
    case Qt::Key_Up:
        select(cell.parent);
        break;
    case Qt::Key_Down:
        select(cell.childCount > 0 ? cell.firstChild : current);
        break;
    default:
        break;
    }
}

void TreemapView::updateBar()
{
    m_zoomOutButton->setEnabled(!m_zoomPath.isEmpty());

    QString path = context()->url().fileName();
    if (path.isEmpty()) {
        path = context()->url().toDisplayString(QUrl::PreferLocalFile);
    }
    for (const QString &name : std::as_const(m_zoomPath)) {
        path += QStringLiteral(" › ") + name;
    }
    if (!m_zoomNode.isNull()) {
        path = i18nc("@info Treemap: %1 is the folder, %2 its size", "%1 — %2", path, formatSize(m_zoomNode.totalSize()));
    }
    m_pathLabel->setText(path);
    m_pathLabel->setToolTip(path);

    // The entry under the pointer, or else the selected one.
    QString info;
    const QList<TreemapLayout::Cell> &cells = m_canvas->layout().cells();
    int shown = m_canvas->hoveredCell();
    if (shown <= 0 && !m_selected.isEmpty()) {
        const int selected = m_canvas->selectedCell();
        shown = selected > 0 && selected < cells.count() && cells.at(selected).node.url() == m_selected ? selected : -1;
    }
    if (shown > 0 && shown < cells.count()) {
        const TreemapLayout::Cell &cell = cells.at(shown);
        if (cell.isRest()) {
            info = i18ncp("@info Treemap", "%1 small entry — %2", "%1 small entries — %2", cell.restCount, formatSize(cell.size));
        } else {
            QStringList parts;
            relativePath(cell.node.url(), &parts);
            info = cell.node.isDirectory() ? i18nc("@info Treemap: %1 folder, %2 size, %3 number of files",
                                                   "%1 — %2 in %3 files",
                                                   parts.join(QLatin1Char('/')),
                                                   formatSize(cell.size),
                                                   cell.node.fileCount())
                                           : i18nc("@info Treemap: %1 file, %2 size", "%1 — %2", parts.join(QLatin1Char('/')), formatSize(cell.size));
        }
    }
    m_infoLabel->setText(info);
}

void TreemapView::reportInfo(bool force)
{
    // The entry under the pointer, or else a deep selection, which Whale's own selection
    // (and with it the information panel) does not know.
    QUrl url;
    const QList<TreemapLayout::Cell> &cells = m_canvas->layout().cells();
    const int hovered = m_canvas->hoveredCell();
    if (hovered > 0 && hovered < cells.count()) {
        url = cells.at(hovered).isRest() ? QUrl() : cells.at(hovered).node.url();
    } else if (!m_selected.isEmpty() && !isListed(m_selected) && m_status == Status::Ready) {
        url = m_selected;
    }
    if (url != m_infoUrl || force) {
        m_infoUrl = url;
        context()->requestItemInfo(url);
    }
}

QVariantMap TreemapView::state() const
{
    m_canvas->ensureLayout();
    const QList<TreemapLayout::Cell> &cells = m_canvas->layout().cells();
    int leaves = 0;
    int rests = 0;
    for (const TreemapLayout::Cell &cell : cells) {
        leaves += cell.isLeaf() ? 1 : 0;
        rests += cell.isRest() ? 1 : 0;
    }
    const int hovered = m_canvas->hoveredCell();
    const bool hoveredValid = hovered > 0 && hovered < cells.count();
    const int selected = m_canvas->selectedCell();
    QStringList selection;
    const bool selectionInTree = !m_selected.isEmpty() && relativePath(m_selected, &selection);
    const auto menus = m_canvas->findChildren<QMenu *>(QStringLiteral("whaleTreemapContextMenu"));
    const int openMenus = std::count_if(menus.cbegin(), menus.cend(), [](const QMenu *menu) {
        return menu->isVisible();
    });

    return {
        {QStringLiteral("status"), statusName(m_status)},
        {QStringLiteral("error"), m_error},
        {QStringLiteral("loadingShown"), m_showLoading},
        {QStringLiteral("statusText"), statusText()},
        {QStringLiteral("location"), context()->url().toString()},
        {QStringLiteral("scanRoot"), m_tree.isNull() ? QString() : m_tree.url().toString()},
        {QStringLiteral("zoomRoot"), m_zoomNode.isNull() ? QString() : m_zoomNode.url().toString()},
        {QStringLiteral("zoomPath"), m_zoomPath},
        {QStringLiteral("totalSize"), m_tree.isNull() ? 0 : m_tree.totalSize()},
        {QStringLiteral("zoomSize"), m_zoomNode.isNull() ? 0 : m_zoomNode.totalSize()},
        {QStringLiteral("cells"), int(cells.count())},
        {QStringLiteral("leafCells"), leaves},
        {QStringLiteral("restCells"), rests},
        {QStringLiteral("truncated"), m_canvas->layout().isTruncated()},
        {QStringLiteral("hovered"), hoveredValid && !cells.at(hovered).isRest() ? cells.at(hovered).node.url().toString() : QString()},
        {QStringLiteral("hoveredIsRest"), hoveredValid && cells.at(hovered).isRest()},
        {QStringLiteral("selected"), m_selected.toString()},
        {QStringLiteral("selectedRelative"), selectionInTree ? selection.join(QLatin1Char('/')) : QString()},
        {QStringLiteral("selectedDeep"), selectionInTree && selection.count() > 1},
        {QStringLiteral("selectedShown"), selected > 0 && cells.at(selected).node.url() == m_selected},
        {QStringLiteral("infoUrl"), m_infoUrl.toString()},
        {QStringLiteral("colorMode"), m_colorMode == TreemapRenderer::ColorMode::TopFolder ? QStringLiteral("topFolder") : QStringLiteral("fileType")},
        {QStringLiteral("showLabels"), m_showLabels},
        {QStringLiteral("finishedScans"), m_finishedScans},
        {QStringLiteral("operationScans"), m_operationScans},
        {QStringLiteral("lastOperation"), m_lastOperation},
        {QStringLiteral("openMenus"), openMenus},
        {QStringLiteral("lastScanMs"), m_lastScanMs},
        {QStringLiteral("scanProgress"), m_progress},
        {QStringLiteral("layoutCount"), m_canvas->layoutCount()},
        {QStringLiteral("layoutUs"), m_canvas->lastLayoutMicroseconds()},
        {QStringLiteral("renderUs"), m_canvas->lastRenderMicroseconds()},
        {QStringLiteral("paintUs"), m_canvas->lastPaintMicroseconds()},
        {QStringLiteral("canvasWidth"), m_canvas->width()},
        {QStringLiteral("canvasHeight"), m_canvas->height()},
        {QStringLiteral("zoomOutEnabled"), m_zoomOutButton->isEnabled()},
        {QStringLiteral("pathText"), m_pathLabel->text()},
        {QStringLiteral("infoText"), m_infoLabel->text()},
    };
}
