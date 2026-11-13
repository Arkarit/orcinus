/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicontextchips.h"

#include <KLocalizedString>

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QResizeEvent>
#include <QStyle>
#include <QToolButton>

namespace
{
/** A label that never asks for the width of its text: it elides it (the chip has a tooltip). */
class ElidedLabel : public QLabel
{
public:
    using QLabel::QLabel;

    /** The part of the text that is elided in its middle. */
    void setName(const QString &name)
    {
        m_name = name;
        update();
    }

    QSize minimumSizeHint() const override
    {
        return QSize(fontMetrics().horizontalAdvance(QStringLiteral("M…")), QLabel::minimumSizeHint().height());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QRect rect = contentsRect();
        const QString elided = WhaleAiContextChips::elidedText(fontMetrics(), text(), m_name, rect.width());
        style()->drawItemText(&painter, rect, int(alignment()) | Qt::AlignVCenter, palette(), isEnabled(), elided, foregroundRole());
    }

private:
    QString m_name;
};

/** A rounded frame drawn here: QFrame::StyledPanel is not drawn by every style (Breeze). */
class ChipFrame : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor border = palette().color(QPalette::WindowText);
        border.setAlphaF(0.25);
        painter.setPen(QPen(border, 1));
        painter.setBrush(palette().color(QPalette::AlternateBase));
        const qreal radius = 4;
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }
};

/** Lays its widgets out left to right and wraps them into further rows. */
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent)
        : QLayout(parent)
    {
        setContentsMargins(0, 0, 0, 0);
    }

    ~FlowLayout() override
    {
        while (QLayoutItem *item = takeAt(0)) {
            delete item;
        }
    }

    void addItem(QLayoutItem *item) override
    {
        m_items.append(item);
    }

    int count() const override
    {
        return m_items.size();
    }

    QLayoutItem *itemAt(int index) const override
    {
        return m_items.value(index);
    }

    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }

    Qt::Orientations expandingDirections() const override
    {
        return {};
    }

    bool hasHeightForWidth() const override
    {
        return true;
    }

    int heightForWidth(int width) const override
    {
        return arrange(QRect(0, 0, width, 0), false);
    }

    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }

    QSize sizeHint() const override
    {
        return minimumSize();
    }

    QSize minimumSize() const override
    {
        QSize size;
        for (const QLayoutItem *item : m_items) {
            size = size.expandedTo(item->minimumSize());
        }
        return size;
    }

private:
    int arrange(const QRect &rect, bool apply) const
    {
        const int gap = spacing() >= 0 ? spacing() : 4;
        int x = rect.x();
        int y = rect.y();
        int rowHeight = 0;
        for (QLayoutItem *item : m_items) {
            QSize size = item->sizeHint();
            size.setWidth(qMin(size.width(), rect.width()));
            if (x > rect.x() && x + size.width() > rect.right() + 1) {
                x = rect.x();
                y += rowHeight + gap;
                rowHeight = 0;
            }
            if (apply) {
                item->setGeometry(QStyle::visualRect(parentWidget() ? parentWidget()->layoutDirection() : Qt::LeftToRight, rect, QRect(QPoint(x, y), size)));
            }
            x += size.width() + gap;
            rowHeight = qMax(rowHeight, size.height());
        }
        return y + rowHeight - rect.y();
    }

    QList<QLayoutItem *> m_items;
};

QString fileName(const QString &path)
{
    // A folder may come with a trailing slash.
    const QString clean = QDir::cleanPath(path);
    const QString name = QFileInfo(clean).fileName();
    return name.isEmpty() ? clean : name;
}

QString pathList(const QStringList &paths)
{
    constexpr int MaxShown = 20;
    QStringList shown = paths.mid(0, MaxShown);
    if (paths.size() > MaxShown) {
        shown.append(i18ncp("@info:tooltip", "… and one more", "… and %1 more", paths.size() - MaxShown));
    }
    return shown.join(QLatin1Char('\n'));
}
}

WhaleAiContextChips::WhaleAiContextChips(QWidget *parent)
    : QWidget(parent)
{
    new FlowLayout(this);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    hide();
}

QString WhaleAiContextChips::elidedText(const QFontMetrics &metrics, const QString &text, const QString &name, int width)
{
    constexpr int KeptHead = 6;
    if (metrics.horizontalAdvance(text) <= width) {
        return text;
    }
    const int at = name.isEmpty() ? -1 : text.lastIndexOf(name);
    if (at < 0) {
        return metrics.elidedText(text, Qt::ElideRight, width);
    }
    const QString before = text.left(at);
    const QString after = text.mid(at + name.size());
    const QString ellipsis = QStringLiteral("…");
    const int room = width - metrics.horizontalAdvance(before + after);
    // The head, if there is room for it and some of the end.
    QString head = name.left(KeptHead) + ellipsis;
    if (name.size() <= KeptHead || metrics.horizontalAdvance(head) >= room) {
        head = ellipsis;
    }
    const int tailRoom = room - metrics.horizontalAdvance(head);
    int tail = name.size() - (head == ellipsis ? 0 : KeptHead);
    while (tail > 0 && metrics.horizontalAdvance(name.right(tail)) > tailRoom) {
        --tail;
    }
    if (tail == 0 && head == ellipsis) {
        // Not even the end of the name fits: the text as a whole.
        return metrics.elidedText(text, Qt::ElideRight, width);
    }
    return before + head + name.right(tail) + after;
}

QSize WhaleAiContextChips::minimumSizeHint() const
{
    const QSize minimum = layout()->minimumSize();
    const int width = qMax(this->width(), minimum.width());
    return QSize(minimum.width(), layout()->heightForWidth(width));
}

QSize WhaleAiContextChips::sizeHint() const
{
    return minimumSizeHint();
}

void WhaleAiContextChips::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Another width may need another number of rows.
    if (event->oldSize().width() != width() && layout()->heightForWidth(width()) != height()) {
        updateGeometry();
    }
}

void WhaleAiContextChips::setContext(const WhaleAiContext &context)
{
    // A removed chip comes back when it would show something else.
    if (context.folder != m_window.folder) {
        m_folderRemoved = false;
    }
    if (context.selection != m_window.selection) {
        m_selectionRemoved = false;
    }
    if (context.otherFolder != m_window.otherFolder) {
        m_otherRemoved = false;
    }
    const WhaleAiContext window{context.folder, context.selection, context.otherFolder, {}};
    if (window == m_window) {
        return;
    }
    m_window = window;
    rebuild();
}

void WhaleAiContextChips::addFiles(const QStringList &paths)
{
    bool added = false;
    for (const QString &path : paths) {
        if (!path.isEmpty() && !m_added.contains(path)) {
            m_added.append(path);
            added = true;
        }
    }
    if (added) {
        rebuild();
    }
}

WhaleAiContext WhaleAiContextChips::context() const
{
    WhaleAiContext context;
    if (!m_folderRemoved) {
        context.folder = m_window.folder;
    }
    if (!m_selectionRemoved) {
        context.selection = m_window.selection;
    }
    if (!m_otherRemoved) {
        context.otherFolder = m_window.otherFolder;
    }
    context.added = m_added;
    return context;
}

void WhaleAiContextChips::messageSent()
{
    m_folderRemoved = false;
    m_selectionRemoved = false;
    m_otherRemoved = false;
    m_added.clear();
    rebuild();
}

void WhaleAiContextChips::setAccess(const std::function<bool(const QString &)> &reaches)
{
    m_reaches = reaches;
    rebuild();
}

void WhaleAiContextChips::rebuild()
{
    QLayout *flow = layout();
    while (QLayoutItem *item = flow->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    if (!m_window.folder.isEmpty() && !m_folderRemoved) {
        flow->addWidget(createChip(QStringLiteral("chip-folder"),
                                   QStringLiteral("folder"),
                                   i18nc("@label chip in the AI panel, %1 is a folder name", "Folder: %1", fileName(m_window.folder)),
                                   fileName(m_window.folder),
                                   m_window.folder,
                                   m_window.folder,
                                   [this] {
                                       m_folderRemoved = true;
                                   }));
    }
    if (!m_window.selection.isEmpty() && !m_selectionRemoved) {
        const QString text = m_window.selection.size() == 1
            ? i18nc("@label chip in the AI panel, %1 is a file name", "Selected: %1", fileName(m_window.selection.first()))
            : i18ncp("@label chip in the AI panel", "Selected: %1 item", "Selected: %1 items", m_window.selection.size());
        const QString name = m_window.selection.size() == 1 ? fileName(m_window.selection.first()) : QString();
        flow->addWidget(
            createChip(QStringLiteral("chip-selection"), QStringLiteral("edit-select-all"), text, name, pathList(m_window.selection), QString(), [this] {
                m_selectionRemoved = true;
            }));
    }
    if (!m_window.otherFolder.isEmpty() && !m_otherRemoved) {
        flow->addWidget(createChip(QStringLiteral("chip-other"),
                                   QStringLiteral("view-split-left-right"),
                                   i18nc("@label chip in the AI panel, %1 is a folder name", "Other view: %1", fileName(m_window.otherFolder)),
                                   fileName(m_window.otherFolder),
                                   m_window.otherFolder,
                                   m_window.otherFolder,
                                   [this] {
                                       m_otherRemoved = true;
                                   }));
    }
    for (const QString &path : std::as_const(m_added)) {
        flow->addWidget(createChip(QStringLiteral("chip-added"),
                                   QFileInfo(path).isDir() ? QStringLiteral("folder") : QStringLiteral("document-new"),
                                   fileName(path),
                                   fileName(path),
                                   path,
                                   path,
                                   [this, path] {
                                       m_added.removeAll(path);
                                   }));
    }

    setVisible(flow->count() > 0);
    updateGeometry();
    Q_EMIT contextChanged();
}

QWidget *WhaleAiContextChips::createChip(const QString &objectName,
                                         const QString &iconName,
                                         const QString &text,
                                         const QString &name,
                                         const QString &toolTip,
                                         const QString &accessPath,
                                         const std::function<void()> &remove)
{
    const bool noAccess = m_reaches && !accessPath.isEmpty() && !m_reaches(accessPath);
    auto *chip = new ChipFrame(this);
    chip->setObjectName(objectName);
    chip->setToolTip(toolTip);
    auto *layout = new QHBoxLayout(chip);
    layout->setContentsMargins(5, 1, 1, 1);
    layout->setSpacing(2);

    const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize);
    auto *icon = new QLabel(chip);
    icon->setPixmap(QIcon::fromTheme(iconName).pixmap(iconSize));
    layout->addWidget(icon);

    auto *label = new ElidedLabel(noAccess ? i18nc("@label chip in the AI panel, %1 is what it shows", "%1 (no access)", text) : text, chip);
    label->setObjectName(QStringLiteral("text"));
    label->setTextFormat(Qt::PlainText);
    label->setName(name);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    layout->addWidget(label, 1);

    if (noAccess) {
        chip->setProperty("noAccess", true);
        chip->setToolTip(i18nc("@info:tooltip %1 is a path", "%1\nThe agent cannot reach this: it may only reach the folders it was given.", toolTip));
        auto *give = new QToolButton(chip);
        give->setObjectName(QStringLiteral("giveAccess"));
        give->setIcon(QIcon::fromTheme(QStringLiteral("unlock"), QIcon::fromTheme(QStringLiteral("object-unlocked"))));
        give->setIconSize(QSize(iconSize * 3 / 4, iconSize * 3 / 4));
        give->setAutoRaise(true);
        give->setToolTip(i18nc("@info:tooltip", "Give the agent access to this, reading and writing, for this conversation"));
        layout->addWidget(give);
        connect(give, &QToolButton::clicked, this, [this, accessPath] {
            Q_EMIT accessRequested(accessPath);
        });
    }

    auto *button = new QToolButton(chip);
    button->setObjectName(QStringLiteral("remove"));
    button->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete-remove"), QIcon::fromTheme(QStringLiteral("window-close"))));
    button->setIconSize(QSize(iconSize * 3 / 4, iconSize * 3 / 4));
    button->setAutoRaise(true);
    button->setToolTip(i18nc("@info:tooltip", "Do not send this with the next message"));
    layout->addWidget(button);
    connect(button, &QToolButton::clicked, this, [this, remove] {
        remove();
        // Not at once: the button is a child of the chip that goes.
        QMetaObject::invokeMethod(this, &WhaleAiContextChips::rebuild, Qt::QueuedConnection);
    });
    return chip;
}

#include "moc_whaleaicontextchips.cpp"
