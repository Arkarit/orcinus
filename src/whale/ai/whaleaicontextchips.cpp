/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicontextchips.h"

#include <KLocalizedString>

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QStyle>
#include <QToolButton>

namespace
{
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
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
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
                                   m_window.folder,
                                   [this] {
                                       m_folderRemoved = true;
                                   }));
    }
    if (!m_window.selection.isEmpty() && !m_selectionRemoved) {
        const QString text = m_window.selection.size() == 1
            ? i18nc("@label chip in the AI panel, %1 is a file name", "Selected: %1", fileName(m_window.selection.first()))
            : i18ncp("@label chip in the AI panel", "Selected: %1 item", "Selected: %1 items", m_window.selection.size());
        flow->addWidget(createChip(QStringLiteral("chip-selection"), QStringLiteral("edit-select-all"), text, pathList(m_window.selection), [this] {
            m_selectionRemoved = true;
        }));
    }
    if (!m_window.otherFolder.isEmpty() && !m_otherRemoved) {
        flow->addWidget(createChip(QStringLiteral("chip-other"),
                                   QStringLiteral("view-split-left-right"),
                                   i18nc("@label chip in the AI panel, %1 is a folder name", "Other view: %1", fileName(m_window.otherFolder)),
                                   m_window.otherFolder,
                                   [this] {
                                       m_otherRemoved = true;
                                   }));
    }
    for (const QString &path : std::as_const(m_added)) {
        flow->addWidget(createChip(QStringLiteral("chip-added"),
                                   QFileInfo(path).isDir() ? QStringLiteral("folder") : QStringLiteral("document-new"),
                                   fileName(path),
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
                                         const QString &toolTip,
                                         const std::function<void()> &remove)
{
    auto *chip = new QFrame(this);
    chip->setObjectName(objectName);
    chip->setFrameShape(QFrame::StyledPanel);
    chip->setToolTip(toolTip);
    auto *layout = new QHBoxLayout(chip);
    layout->setContentsMargins(4, 1, 1, 1);
    layout->setSpacing(2);

    const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize);
    auto *icon = new QLabel(chip);
    icon->setPixmap(QIcon::fromTheme(iconName).pixmap(iconSize));
    layout->addWidget(icon);

    auto *label = new QLabel(text, chip);
    label->setObjectName(QStringLiteral("text"));
    label->setTextFormat(Qt::PlainText);
    layout->addWidget(label);

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
