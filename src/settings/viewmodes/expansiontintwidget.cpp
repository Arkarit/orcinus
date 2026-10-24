/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "expansiontintwidget.h"

#include <KColorButton>
#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyleOption>
#include <QToolButton>
#include <QVBoxLayout>

/**
 * Shows a small expanded folder tree in the compact view style with the tints and names.
 */
class ExpansionTintPreview : public QWidget
{
public:
    explicit ExpansionTintPreview(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setColors(const QList<QColor> &colors)
    {
        m_colors = colors;
        update();
    }

    /**
     * Sets the opacity of the folder names; 0 hides them.
     */
    void setNameOpacity(qreal opacity)
    {
        m_nameOpacity = opacity;
        update();
    }

    QSize sizeHint() const override
    {
        return QSize(rowHeight() * 12, rowHeight() * int(rows().count()) + 2);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        const QRect frame = rect();
        painter.fillRect(frame, palette().color(QPalette::Base));

        const int height = rowHeight();
        const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
        const QIcon folderIcon = QIcon::fromTheme(QStringLiteral("folder"));
        const QIcon fileIcon = QIcon::fromTheme(QStringLiteral("text-plain"));
        const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

        const QList<Row> allRows = rows();
        int y = 1;
        for (int i = 0; i < allRows.count(); ++i) {
            const Row &row = allRows.at(i);
            const bool expanded = i + 1 < allRows.count() && allRows.at(i + 1).depth > row.depth;
            const QRect slot(frame.left() + 1, y, frame.width() - 2, height);
            // As the view does: one area per level, from its indentation to the end of the column,
            // and for an expanded folder the color of its children from the end of its arrow on.
            if (!m_colors.isEmpty()) {
                const auto fill = [&](int inset, int level) {
                    const QRect area = rightToLeft ? slot.adjusted(0, 0, -inset, 0) : slot.adjusted(inset, 0, 0, 0);
                    painter.fillRect(area, m_colors.at((level - 1) % m_colors.count()));
                };
                for (int level = 1; level <= row.depth; ++level) {
                    fill(level * height, level);
                }
                if (expanded) {
                    fill((row.depth + 1) * height - (height - iconSize) / 2, row.depth + 1);
                }
            }

            if (row.folder) {
                QStyleOption arrow;
                arrow.initFrom(this);
                arrow.rect =
                    QStyle::visualRect(layoutDirection(),
                                       slot,
                                       QRect(slot.left() + row.depth * height + (height - iconSize) / 2, y + (height - iconSize) / 2, iconSize, iconSize));
                const QStyle::PrimitiveElement collapsed = rightToLeft ? QStyle::PE_IndicatorArrowLeft : QStyle::PE_IndicatorArrowRight;
                style()->drawPrimitive(expanded ? QStyle::PE_IndicatorArrowDown : collapsed, &arrow, &painter, this);
            }

            // Like the compact view: the expansion arrow, then the icon and the name.
            const int iconOffset = (row.depth + 1) * height + (height - iconSize) / 2;
            const QRect iconRect =
                QStyle::visualRect(layoutDirection(), slot, QRect(slot.left() + iconOffset, y + (height - iconSize) / 2, iconSize, iconSize));
            (row.folder ? folderIcon : fileIcon).paint(&painter, iconRect);

            const int textOffset = iconOffset + iconSize + height / 4;
            const QRect textRect = QStyle::visualRect(layoutDirection(), slot, QRect(slot.left() + textOffset, y, slot.width() - textOffset, height));
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeading, row.name);

            y += height;
        }

        if (m_nameOpacity > 0) {
            paintNames(&painter, frame, height);
        }

        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(frame.adjusted(0, 0, -1, -1));
    }

private:
    struct Row {
        int depth;
        bool folder;
        QString name;
    };

    /**
     * As the view does (KItemListView::paintExpansionNames()): every run of rows of the same
     * depth gets the names of all its parents, centered on the run, each below the arrow of
     * its folder, if it fits.
     */
    void paintNames(QPainter *painter, const QRect &frame, int height) const
    {
        QFont font = this->font();
        font.setPointSizeF(font.pointSizeF() * 0.92);
        const QFontMetricsF metrics(font);
        QColor color = palette().color(QPalette::Text);
        color.setAlphaF(m_nameOpacity);
        painter->save();
        painter->setFont(font);
        painter->setPen(color);

        const QList<Row> allRows = rows();
        int first = 0;
        while (first < allRows.count()) {
            const int depth = allRows.at(first).depth;
            int last = first;
            while (last + 1 < allRows.count() && allRows.at(last + 1).depth == depth) {
                ++last;
            }
            const qreal runHeight = (last - first + 1) * height;
            const qreal centerY = 1 + first * height + runHeight / 2;
            int parentDepth = depth - 1;
            for (int i = first - 1; i >= 0 && parentDepth >= 0; --i) {
                if (allRows.at(i).depth != parentDepth) {
                    continue;
                }
                const QString &name = allRows.at(i).name;
                const qreal width = metrics.horizontalAdvance(name);
                if (width + height / 2.0 <= runHeight) {
                    const qreal centerX =
                        layoutDirection() == Qt::RightToLeft ? frame.right() - (parentDepth + 0.5) * height : frame.left() + 1 + (parentDepth + 0.5) * height;
                    painter->save();
                    painter->translate(centerX, centerY);
                    painter->rotate(-90);
                    painter->drawText(QRectF(-width / 2, -height / 2.0, width, height), Qt::AlignCenter, name);
                    painter->restore();
                }
                --parentDepth;
            }
            first = last + 1;
        }
        painter->restore();
    }

    int rowHeight() const
    {
        return qMax(fontMetrics().height(), style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this)) + 4;
    }

    static QList<Row> rows()
    {
        return {
            {0, true, i18nc("@item:intext example folder in a preview", "Projects")},
            {1, true, QStringLiteral("whale")},
            {2, true, QStringLiteral("src")},
            {3, false, QStringLiteral("main.cpp")},
            {3, false, QStringLiteral("model.cpp")},
            {3, false, QStringLiteral("settings.cpp")},
            {3, false, QStringLiteral("view.cpp")},
            {2, false, QStringLiteral("README.md")},
            {1, false, i18nc("@item:intext example file in a preview", "notes.txt")},
            {0, true, i18nc("@item:intext example folder in a preview", "Music")},
        };
    }

    QList<QColor> m_colors;
    qreal m_nameOpacity = 0;
};

ExpansionTintWidget::ExpansionTintWidget(QWidget *parent)
    : QWidget(parent)
    , m_presetComboBox(nullptr)
    , m_colorRow(nullptr)
    , m_addColorButton(nullptr)
    , m_removeColorButton(nullptr)
    , m_showNamesCheckBox(nullptr)
    , m_nameOpacitySlider(nullptr)
    , m_nameOpacityLabel(nullptr)
    , m_preview(nullptr)
{
    auto topLayout = new QHBoxLayout(this);
    topLayout->setContentsMargins(0, 0, 0, 0);

    auto formLayout = new QFormLayout();
    topLayout->addLayout(formLayout, 1);

    m_presetComboBox = new QComboBox(this);
    m_presetComboBox->addItem(i18nc("@item:inlistbox expanded folder colors", "Off"), int(ExpansionTintSettings::Preset::Off));
    m_presetComboBox->addItem(i18nc("@item:inlistbox expanded folder colors, alternating like old printer paper", "Printer paper"),
                              int(ExpansionTintSettings::Preset::PrinterPaper));
    m_presetComboBox->addItem(i18nc("@item:inlistbox expanded folder colors", "Grey"), int(ExpansionTintSettings::Preset::Grey));
    m_presetComboBox->addItem(i18nc("@item:inlistbox expanded folder colors", "Pastel (Subtle)"), int(ExpansionTintSettings::Preset::PastelSubtle));
    m_presetComboBox->addItem(i18nc("@item:inlistbox expanded folder colors", "Pastel (Vivid)"), int(ExpansionTintSettings::Preset::PastelVivid));
    m_presetComboBox->addItem(i18nc("@item:inlistbox expanded folder colors", "Custom"), int(ExpansionTintSettings::Preset::Custom));
    formLayout->addRow(i18nc("@label:listbox", "Background colors:"), m_presetComboBox);

    m_colorRow = new QWidget(this);
    auto colorLayout = new QHBoxLayout(m_colorRow);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < ExpansionTintSettings::MaximumColorCount; ++i) {
        auto button = new KColorButton(m_colorRow);
        button->setToolTip(i18nc("@info:tooltip", "Color of level %1", i + 1));
        colorLayout->addWidget(button);
        m_colorButtons.append(button);
        connect(button, &KColorButton::changed, this, [this, i](const QColor &color) {
            QList<QColor> colors = shownColors();
            if (i < colors.count() && colors.at(i) != color) {
                colors[i] = color;
                setCustomColors(colors);
            }
        });
    }
    m_addColorButton = new QToolButton(m_colorRow);
    m_addColorButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    m_addColorButton->setToolTip(i18nc("@info:tooltip", "Add a color"));
    m_removeColorButton = new QToolButton(m_colorRow);
    m_removeColorButton->setIcon(QIcon::fromTheme(QStringLiteral("list-remove")));
    m_removeColorButton->setToolTip(i18nc("@info:tooltip", "Remove the last color"));
    colorLayout->addWidget(m_addColorButton);
    colorLayout->addWidget(m_removeColorButton);
    colorLayout->addStretch();
    formLayout->addRow(QString(), m_colorRow);

    auto hint = new QLabel(i18nc("@info",
                                 "Colors the background of the contents of expanded folders in the Compact view mode by depth. "
                                 "The colors repeat for deeper levels. The presets adapt to the color scheme, "
                                 "custom colors are used as chosen."),
                           this);
    hint->setWordWrap(true);
    formLayout->addRow(QString(), hint);

    m_showNamesCheckBox = new QCheckBox(i18nc("@option:check", "Show folder names next to their contents"), this);
    m_showNamesCheckBox->setToolTip(i18nc("@info:tooltip", "Where there is room, the names of the expanded folders are shown in the indentation."));
    formLayout->addRow(QString(), m_showNamesCheckBox);

    auto opacityRow = new QWidget(this);
    auto opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    m_nameOpacitySlider = new QSlider(Qt::Horizontal, opacityRow);
    m_nameOpacitySlider->setRange(ExpansionTintSettings::MinimumNameOpacity, ExpansionTintSettings::MaximumNameOpacity);
    m_nameOpacitySlider->setPageStep(10);
    m_nameOpacityLabel = new QLabel(opacityRow);
    // Wide enough for "100 %", so that the slider does not move while it is dragged.
    m_nameOpacityLabel->setMinimumWidth(m_nameOpacityLabel->fontMetrics().horizontalAdvance(i18nc("@label opacity in percent", "%1 %", 100)));
    opacityLayout->addWidget(m_nameOpacitySlider, 1);
    opacityLayout->addWidget(m_nameOpacityLabel);
    formLayout->addRow(i18nc("@label:slider", "Name opacity:"), opacityRow);

    m_preview = new ExpansionTintPreview(this);
    topLayout->addWidget(m_preview, 0, Qt::AlignTop);

    setSettings(ExpansionTintSettings::load());

    connect(m_presetComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        m_settings.preset = ExpansionTintSettings::Preset(m_presetComboBox->currentData().toInt());
        updateWidgets();
        Q_EMIT changed();
    });
    connect(m_addColorButton, &QToolButton::clicked, this, [this]() {
        QList<QColor> colors = shownColors();
        const QList<QColor> defaults = ExpansionTintSettings::defaultCustomColors();
        colors.append(defaults.at(colors.count() % defaults.count()));
        setCustomColors(colors);
    });
    connect(m_removeColorButton, &QToolButton::clicked, this, [this]() {
        QList<QColor> colors = shownColors();
        colors.removeLast();
        setCustomColors(colors);
    });
    connect(m_showNamesCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.showNames = checked;
        updateWidgets();
        Q_EMIT changed();
    });
    connect(m_nameOpacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_settings.nameOpacity = value;
        updateWidgets();
        Q_EMIT changed();
    });
}

ExpansionTintSettings ExpansionTintWidget::settings() const
{
    return m_settings;
}

void ExpansionTintWidget::setSettings(const ExpansionTintSettings &settings)
{
    m_settings = settings;
    const QSignalBlocker presetBlocker(m_presetComboBox);
    m_presetComboBox->setCurrentIndex(m_presetComboBox->findData(int(settings.preset)));
    const QSignalBlocker showNamesBlocker(m_showNamesCheckBox);
    m_showNamesCheckBox->setChecked(settings.showNames);
    const QSignalBlocker opacityBlocker(m_nameOpacitySlider);
    m_nameOpacitySlider->setValue(settings.nameOpacity);
    updateWidgets();
}

QComboBox *ExpansionTintWidget::presetComboBox() const
{
    return m_presetComboBox;
}

QList<KColorButton *> ExpansionTintWidget::colorButtons() const
{
    return m_colorButtons.mid(0, shownColors().count());
}

QToolButton *ExpansionTintWidget::addColorButton() const
{
    return m_addColorButton;
}

QToolButton *ExpansionTintWidget::removeColorButton() const
{
    return m_removeColorButton;
}

QCheckBox *ExpansionTintWidget::showNamesCheckBox() const
{
    return m_showNamesCheckBox;
}

QSlider *ExpansionTintWidget::nameOpacitySlider() const
{
    return m_nameOpacitySlider;
}

void ExpansionTintWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange) {
        // The presets are mixed into the base color of the palette.
        updateWidgets();
    }
    QWidget::changeEvent(event);
}

QList<QColor> ExpansionTintWidget::shownColors() const
{
    return m_settings.colors(palette());
}

void ExpansionTintWidget::setCustomColors(const QList<QColor> &colors)
{
    m_settings.customColors = colors;
    m_settings.preset = ExpansionTintSettings::Preset::Custom;
    const QSignalBlocker blocker(m_presetComboBox);
    m_presetComboBox->setCurrentIndex(m_presetComboBox->findData(int(ExpansionTintSettings::Preset::Custom)));
    updateWidgets();
    Q_EMIT changed();
}

void ExpansionTintWidget::updateWidgets()
{
    const QList<QColor> colors = shownColors();
    m_colorRow->setVisible(!colors.isEmpty());
    for (int i = 0; i < m_colorButtons.count(); ++i) {
        KColorButton *button = m_colorButtons.at(i);
        button->setVisible(i < colors.count());
        if (i < colors.count()) {
            const QSignalBlocker blocker(button);
            button->setColor(colors.at(i));
        }
    }
    m_addColorButton->setEnabled(colors.count() < ExpansionTintSettings::MaximumColorCount);
    m_removeColorButton->setEnabled(colors.count() > ExpansionTintSettings::MinimumColorCount);
    m_preview->setColors(colors);

    m_nameOpacitySlider->setEnabled(m_settings.showNames);
    m_nameOpacityLabel->setEnabled(m_settings.showNames);
    m_nameOpacityLabel->setText(i18nc("@label opacity in percent", "%1 %", m_settings.nameOpacity));
    m_preview->setNameOpacity(m_settings.showNames ? m_settings.nameOpacity / 100.0 : 0);
}

#include "moc_expansiontintwidget.cpp"
