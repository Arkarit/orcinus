/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef EXPANSIONTINTWIDGET_H
#define EXPANSIONTINTWIDGET_H

#include "views/expansiontintsettings.h"

#include <QWidget>

class ExpansionTintPreview;
class KColorButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QToolButton;

/**
 * @brief Configures the tinting of expanded folders: a preset or custom colors, and the
 *        names of the folders in the indentation, with a preview.
 *
 * The color buttons show the colors of the selected preset. Changing a color, adding
 * or removing one switches to the custom colors.
 */
class ExpansionTintWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ExpansionTintWidget(QWidget *parent = nullptr);

    ExpansionTintSettings settings() const;
    void setSettings(const ExpansionTintSettings &settings);

    QComboBox *presetComboBox() const;

    /**
     * @return The color buttons currently shown, one per color of the cycle.
     */
    QList<KColorButton *> colorButtons() const;
    QToolButton *addColorButton() const;
    QToolButton *removeColorButton() const;
    QCheckBox *showNamesCheckBox() const;
    QSlider *nameOpacitySlider() const;

Q_SIGNALS:
    void changed();

protected:
    void changeEvent(QEvent *event) override;

private:
    /**
     * @return The colors of the cycle as shown, for the palette of this widget.
     */
    QList<QColor> shownColors() const;

    /**
     * Switches to the custom colors @p colors after an edit by the user.
     */
    void setCustomColors(const QList<QColor> &colors);

    void updateWidgets();

    ExpansionTintSettings m_settings;
    QComboBox *m_presetComboBox;
    QWidget *m_colorRow;
    QList<KColorButton *> m_colorButtons;
    QToolButton *m_addColorButton;
    QToolButton *m_removeColorButton;
    QCheckBox *m_showNamesCheckBox;
    QSlider *m_nameOpacitySlider;
    QLabel *m_nameOpacityLabel;
    ExpansionTintPreview *m_preview;
};

#endif
