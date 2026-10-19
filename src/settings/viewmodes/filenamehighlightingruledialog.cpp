/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "filenamehighlightingruledialog.h"

#include <KColorButton>
#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <KMimeTypeChooser>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeDatabase>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSignalBlocker>

#include <algorithm>

namespace
{
enum TriState {
    Unchanged,
    On,
    Off,
};

struct ColorRoleLabel {
    KColorScheme::ForegroundRole role;
    KLazyLocalizedString label;
};

const ColorRoleLabel colorRoleLabels[] = {
    {KColorScheme::NormalText, kli18nc("@item:inlistbox color scheme text color", "Normal text")},
    {KColorScheme::InactiveText, kli18nc("@item:inlistbox color scheme text color", "Inactive text")},
    {KColorScheme::ActiveText, kli18nc("@item:inlistbox color scheme text color", "Active text")},
    {KColorScheme::LinkText, kli18nc("@item:inlistbox color scheme text color", "Link text")},
    {KColorScheme::VisitedText, kli18nc("@item:inlistbox color scheme text color", "Visited text")},
    {KColorScheme::NegativeText, kli18nc("@item:inlistbox color scheme text color", "Negative text")},
    {KColorScheme::NeutralText, kli18nc("@item:inlistbox color scheme text color", "Neutral text")},
    {KColorScheme::PositiveText, kli18nc("@item:inlistbox color scheme text color", "Positive text")},
};

QComboBox *createTriStateComboBox(const QString &on, const QString &off, QWidget *parent)
{
    auto comboBox = new QComboBox(parent);
    comboBox->addItem(i18nc("@item:inlistbox text style attribute is not changed", "Unchanged"));
    comboBox->addItem(on);
    comboBox->addItem(off);
    return comboBox;
}

int triStateIndex(const std::optional<bool> &value)
{
    if (!value.has_value()) {
        return Unchanged;
    }
    return *value ? On : Off;
}

std::optional<bool> triStateValue(int index)
{
    switch (index) {
    case On:
        return true;
    case Off:
        return false;
    default:
        return std::nullopt;
    }
}

QIcon colorSwatch(const QColor &color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(1, 1, 14, 14), 3, 3);
    return QIcon(pixmap);
}

bool isGroupPattern(const QString &pattern)
{
    return pattern.endsWith(QLatin1String("/*")) && pattern.count(QLatin1Char('/')) == 1;
}
}

FileNameHighlightingRuleDialog::FileNameHighlightingRuleDialog(const FileNameHighlightRule &rule, QWidget *parent)
    : QDialog(parent)
    , m_rule(rule)
{
    setWindowTitle(i18nc("@title:window", "File Name Highlighting Rule"));

    auto topLayout = new QVBoxLayout(this);
    auto formLayout = new QFormLayout();
    topLayout->addLayout(formLayout);

    m_name = new QLineEdit(this);
    m_name->setText(rule.name);
    formLayout->addRow(i18nc("@label:textbox", "Name:"), m_name);

    m_condition = new QComboBox(this);
    m_condition->addItem(i18nc("@item:inlistbox file name highlighting condition", "Executable files"),
                         static_cast<int>(FileNameHighlightRule::Condition::Executable));
    m_condition->addItem(i18nc("@item:inlistbox file name highlighting condition", "Symbolic links"),
                         static_cast<int>(FileNameHighlightRule::Condition::Symlink));
    m_condition->addItem(i18nc("@item:inlistbox file name highlighting condition", "MIME type"), static_cast<int>(FileNameHighlightRule::Condition::MimeType));
    m_condition->addItem(i18nc("@item:inlistbox file name highlighting condition", "File name pattern"),
                         static_cast<int>(FileNameHighlightRule::Condition::NamePattern));
    m_condition->setCurrentIndex(m_condition->findData(static_cast<int>(rule.condition)));
    formLayout->addRow(i18nc("@label:listbox", "Condition:"), m_condition);

    m_appliesTo = new QComboBox(this);
    m_appliesTo->addItem(i18nc("@item:inlistbox file name highlighting rule applies to", "Files and folders"),
                         static_cast<int>(FileNameHighlightRule::ItemKind::FilesAndFolders));
    m_appliesTo->addItem(i18nc("@item:inlistbox file name highlighting rule applies to", "Folders"),
                         static_cast<int>(FileNameHighlightRule::ItemKind::Folders));
    m_appliesTo->addItem(i18nc("@item:inlistbox file name highlighting rule applies to", "Files"), static_cast<int>(FileNameHighlightRule::ItemKind::Files));
    m_appliesTo->setCurrentIndex(m_appliesTo->findData(static_cast<int>(rule.appliesTo)));
    formLayout->addRow(i18nc("@label:listbox", "Applies to:"), m_appliesTo);

    m_patternsWidget = new QWidget(this);
    auto mimeTypesLayout = new QVBoxLayout(m_patternsWidget);
    mimeTypesLayout->setContentsMargins(0, 0, 0, 0);
    auto mimeTypesRow = new QHBoxLayout();
    m_patterns = new QLineEdit(m_patternsWidget);
    m_patterns->setText(rule.patterns.join(QLatin1String("; ")));
    m_chooseMimeTypes = new QPushButton(i18nc("@action:button", "Choose…"), m_patternsWidget);
    mimeTypesRow->addWidget(m_patterns);
    mimeTypesRow->addWidget(m_chooseMimeTypes);
    mimeTypesLayout->addLayout(mimeTypesRow);
    m_patternsWarning = new QLabel(m_patternsWidget);
    m_patternsWarning->setWordWrap(true);
    mimeTypesLayout->addWidget(m_patternsWarning);
    m_caseSensitive = new QCheckBox(i18nc("@option:check file name patterns", "Case sensitive"), m_patternsWidget);
    m_caseSensitive->setChecked(rule.caseSensitive);
    mimeTypesLayout->addWidget(m_caseSensitive);
    m_patternsLabel = new QLabel(this);
    formLayout->addRow(m_patternsLabel, m_patternsWidget);

    // Text color: unchanged, a role of the color scheme or a custom color.
    m_colorUnchanged = new QRadioButton(i18nc("@option:radio text color", "Unchanged"), this);
    m_colorFromScheme = new QRadioButton(i18nc("@option:radio text color", "From color scheme:"), this);
    m_colorCustom = new QRadioButton(i18nc("@option:radio text color", "Custom:"), this);
    auto colorGroup = new QButtonGroup(this);
    colorGroup->addButton(m_colorUnchanged);
    colorGroup->addButton(m_colorFromScheme);
    colorGroup->addButton(m_colorCustom);

    m_colorRole = new QComboBox(this);
    for (const ColorRoleLabel &entry : colorRoleLabels) {
        const QColor color = FileNameHighlighter::resolveNameColor(FileNameColor::schemeRole(entry.role), palette());
        m_colorRole->addItem(colorSwatch(color), entry.label.toString(), static_cast<int>(entry.role));
    }
    m_customColor = new KColorButton(this);

    auto schemeRow = new QHBoxLayout();
    schemeRow->addWidget(m_colorFromScheme);
    schemeRow->addWidget(m_colorRole);
    schemeRow->addStretch();
    auto customRow = new QHBoxLayout();
    customRow->addWidget(m_colorCustom);
    customRow->addWidget(m_customColor);
    customRow->addStretch();

    formLayout->addRow(i18nc("@label", "Text color:"), m_colorUnchanged);
    formLayout->addRow(QString(), schemeRow);
    formLayout->addRow(QString(), customRow);
    auto schemeHint = new QLabel(i18nc("@info", "Colors from the color scheme adapt to light and dark color schemes."), this);
    schemeHint->setWordWrap(true);
    QFont hintFont = schemeHint->font();
    hintFont.setPointSizeF(hintFont.pointSizeF() * 0.9);
    schemeHint->setFont(hintFont);
    formLayout->addRow(QString(), schemeHint);

    switch (rule.style.color.source()) {
    case FileNameColor::Source::Unset:
        m_colorUnchanged->setChecked(true);
        m_customColor->setColor(palette().text().color());
        break;
    case FileNameColor::Source::SchemeRole:
        m_colorFromScheme->setChecked(true);
        m_colorRole->setCurrentIndex(m_colorRole->findData(static_cast<int>(rule.style.color.role())));
        m_customColor->setColor(FileNameHighlighter::resolveNameColor(rule.style.color, palette()));
        break;
    case FileNameColor::Source::Custom:
        m_colorCustom->setChecked(true);
        m_customColor->setColor(rule.style.color.customColor());
        break;
    }

    m_bold = createTriStateComboBox(i18nc("@item:inlistbox", "Bold"), i18nc("@item:inlistbox", "Not bold"), this);
    m_bold->setCurrentIndex(triStateIndex(rule.style.bold));
    formLayout->addRow(i18nc("@label:listbox", "Bold:"), m_bold);

    m_italic = createTriStateComboBox(i18nc("@item:inlistbox", "Italic"), i18nc("@item:inlistbox", "Not italic"), this);
    m_italic->setCurrentIndex(triStateIndex(rule.style.italic));
    formLayout->addRow(i18nc("@label:listbox", "Italic:"), m_italic);

    m_preview = new QLabel(this);
    m_preview->setAutoFillBackground(true);
    m_preview->setFrameShape(QFrame::StyledPanel);
    m_preview->setContentsMargins(6, 4, 6, 4);
    formLayout->addRow(i18nc("@label", "Preview:"), m_preview);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    topLayout->addStretch();
    topLayout->addWidget(m_buttonBox);

    connect(m_condition, &QComboBox::currentIndexChanged, this, &FileNameHighlightingRuleDialog::updateWidgets);
    connect(m_appliesTo, &QComboBox::currentIndexChanged, this, &FileNameHighlightingRuleDialog::updateWidgets);
    connect(m_patterns, &QLineEdit::textChanged, this, &FileNameHighlightingRuleDialog::updateWidgets);
    connect(m_chooseMimeTypes, &QPushButton::clicked, this, &FileNameHighlightingRuleDialog::chooseMimeTypes);
    connect(m_caseSensitive, &QCheckBox::toggled, this, &FileNameHighlightingRuleDialog::updateWidgets);
    connect(colorGroup, &QButtonGroup::buttonToggled, this, &FileNameHighlightingRuleDialog::updateWidgets);
    connect(m_colorRole, &QComboBox::currentIndexChanged, this, &FileNameHighlightingRuleDialog::updateWidgets);
    connect(m_customColor, &KColorButton::changed, this, &FileNameHighlightingRuleDialog::updateWidgets);
    connect(m_bold, &QComboBox::currentIndexChanged, this, &FileNameHighlightingRuleDialog::updateWidgets);
    connect(m_italic, &QComboBox::currentIndexChanged, this, &FileNameHighlightingRuleDialog::updateWidgets);
    connect(m_name, &QLineEdit::textChanged, this, &FileNameHighlightingRuleDialog::updateWidgets);

    // Switching from a scheme color to a custom color starts with the current scheme color.
    connect(m_colorCustom, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked && m_rule.style.color.source() != FileNameColor::Source::Custom) {
            const QColor schemeColor =
                FileNameHighlighter::resolveNameColor(FileNameColor::schemeRole(static_cast<KColorScheme::ForegroundRole>(m_colorRole->currentData().toInt())),
                                                      palette());
            m_customColor->setColor(schemeColor);
        }
    });

    updateWidgets();
}

FileNameHighlightRule FileNameHighlightingRuleDialog::rule() const
{
    FileNameHighlightRule rule = m_rule;
    rule.name = m_name->text().trimmed();
    rule.condition = condition();
    // Executable files are always files; the rule stores no item kind of its own.
    rule.appliesTo = rule.condition == FileNameHighlightRule::Condition::Executable
        ? FileNameHighlightRule::ItemKind::FilesAndFolders
        : static_cast<FileNameHighlightRule::ItemKind>(m_appliesTo->currentData().toInt());
    rule.patterns = hasPatterns() ? parsePatterns(m_patterns->text()) : QStringList();
    rule.caseSensitive = rule.condition == FileNameHighlightRule::Condition::NamePattern && m_caseSensitive->isChecked();
    rule.style = style();
    return rule;
}

QString FileNameHighlightingRuleDialog::displayName(const FileNameHighlightRule &rule)
{
    if (!rule.name.isEmpty()) {
        return rule.name;
    }

    switch (rule.condition) {
    case FileNameHighlightRule::Condition::Executable:
        return i18nc("@item:inlistbox file name highlighting condition", "Executable files");
    case FileNameHighlightRule::Condition::Symlink:
        return i18nc("@item:inlistbox file name highlighting condition", "Symbolic links");
    case FileNameHighlightRule::Condition::MimeType:
        return i18nc("@item:inlistbox %1 is a list of MIME types", "MIME type: %1", rule.patterns.join(QLatin1String(", ")));
    case FileNameHighlightRule::Condition::NamePattern:
        return i18nc("@item:inlistbox %1 is a list of file name patterns", "File name: %1", rule.patterns.join(QLatin1String(", ")));
    }
    return QString();
}

QString FileNameHighlightingRuleDialog::exampleFileName(const FileNameHighlightRule &rule)
{
    switch (rule.condition) {
    case FileNameHighlightRule::Condition::Executable:
        return QStringLiteral("program");
    case FileNameHighlightRule::Condition::Symlink:
        return QStringLiteral("link");
    case FileNameHighlightRule::Condition::MimeType:
        break;
    case FileNameHighlightRule::Condition::NamePattern: {
        // "*.cpp" -> "example.cpp"; patterns with character sets get a generic name.
        QString name = rule.patterns.value(0);
        if (name.isEmpty() || name.contains(QLatin1Char('['))) {
            return QStringLiteral("example");
        }
        name.replace(QLatin1Char('*'), QLatin1String("example"));
        name.replace(QLatin1Char('?'), QLatin1Char('x'));
        return name;
    }
    }

    const QString pattern = rule.patterns.value(0);
    if (pattern == QLatin1String("image/*")) {
        return QStringLiteral("photo.jpg");
    } else if (pattern == QLatin1String("audio/*")) {
        return QStringLiteral("song.mp3");
    } else if (pattern == QLatin1String("video/*")) {
        return QStringLiteral("movie.mp4");
    }

    const QMimeType mimeType = QMimeDatabase().mimeTypeForName(pattern);
    if (mimeType.isValid() && !mimeType.preferredSuffix().isEmpty()) {
        return QStringLiteral("example.") + mimeType.preferredSuffix();
    }
    return QStringLiteral("example");
}

QFont FileNameHighlightingRuleDialog::previewFont(const FileNameTextStyle &style, const QFont &baseFont)
{
    QFont font(baseFont);
    if (style.bold.has_value()) {
        font.setBold(*style.bold);
    }
    if (style.italic.has_value()) {
        font.setItalic(*style.italic);
    }
    return font;
}

QColor FileNameHighlightingRuleDialog::previewColor(const FileNameTextStyle &style, const QPalette &palette)
{
    const QColor color = FileNameHighlighter::resolveNameColor(style.color, palette);
    return color.isValid() ? color : palette.text().color();
}

QStringList FileNameHighlightingRuleDialog::parsePatterns(const QString &text)
{
    QStringList patterns;
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[;,]")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString pattern = part.trimmed();
        if (!pattern.isEmpty() && !patterns.contains(pattern)) {
            patterns.append(pattern);
        }
    }
    return patterns;
}

QStringList FileNameHighlightingRuleDialog::unknownMimeTypePatterns(const QStringList &patterns)
{
    const QMimeDatabase database;
    QStringList unknown;
    QStringList groups;

    for (const QString &pattern : patterns) {
        if (isGroupPattern(pattern)) {
            if (groups.isEmpty()) {
                const QList<QMimeType> mimeTypes = database.allMimeTypes();
                for (const QMimeType &mimeType : mimeTypes) {
                    const QString group = mimeType.name().section(QLatin1Char('/'), 0, 0);
                    if (!groups.contains(group)) {
                        groups.append(group);
                    }
                }
            }
            if (!groups.contains(pattern.chopped(2))) {
                unknown.append(pattern);
            }
        } else if (!database.mimeTypeForName(pattern).isValid()) {
            unknown.append(pattern);
        }
    }
    return unknown;
}

void FileNameHighlightingRuleDialog::updateWidgets()
{
    const bool isMimeType = condition() == FileNameHighlightRule::Condition::MimeType;
    const bool isNamePattern = condition() == FileNameHighlightRule::Condition::NamePattern;
    m_patternsLabel->setVisible(hasPatterns());
    m_patternsWidget->setVisible(hasPatterns());
    m_patternsLabel->setText(isMimeType ? i18nc("@label:textbox", "MIME types:") : i18nc("@label:textbox", "File name patterns:"));
    m_patterns->setPlaceholderText(isMimeType ? i18nc("@info:placeholder", "e.g. image/*; text/plain")
                                              : i18nc("@info:placeholder", "e.g. *.cpp; *.h; Makefile"));
    m_chooseMimeTypes->setVisible(isMimeType);
    m_caseSensitive->setVisible(isNamePattern);

    // Executable files are regular files by definition. While that condition is chosen, the
    // combo box shows this fixed choice; the user's choice returns with another condition.
    const bool isExecutable = condition() == FileNameHighlightRule::Condition::Executable;
    if (isExecutable && m_appliesTo->isEnabled()) {
        m_appliesToChoice = m_appliesTo->currentIndex();
        const QSignalBlocker blocker(m_appliesTo);
        m_appliesTo->setCurrentIndex(m_appliesTo->findData(static_cast<int>(FileNameHighlightRule::ItemKind::Files)));
    } else if (!isExecutable && !m_appliesTo->isEnabled()) {
        const QSignalBlocker blocker(m_appliesTo);
        m_appliesTo->setCurrentIndex(m_appliesToChoice);
    }
    m_appliesTo->setEnabled(!isExecutable);

    const QStringList patterns = parsePatterns(m_patterns->text());
    QString warning;
    // Explains why OK stays disabled while there are no patterns.
    if (hasPatterns() && patterns.isEmpty()) {
        warning = isMimeType ? i18nc("@info", "Enter at least one MIME type.") : i18nc("@info", "Enter at least one pattern. Use * to match every name.");
    } else if (isMimeType) {
        const QStringList unknown = unknownMimeTypePatterns(patterns);
        if (!unknown.isEmpty()) {
            warning = i18nc("@info %1 is a list of MIME types", "Unknown MIME types: %1", unknown.join(QLatin1String(", ")));
        }
    } else if (isNamePattern) {
        // File names never contain a slash, so such patterns never match.
        const bool containsSlash = std::any_of(patterns.cbegin(), patterns.cend(), [](const QString &pattern) {
            return pattern.contains(QLatin1Char('/'));
        });
        if (containsSlash) {
            warning = i18nc("@info", "File name patterns match names only and cannot contain \"/\".");
        }
    }
    m_patternsWarning->setVisible(!warning.isEmpty());
    m_patternsWarning->setText(warning);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!hasPatterns() || !patterns.isEmpty());

    m_colorRole->setEnabled(m_colorFromScheme->isChecked());
    m_customColor->setEnabled(m_colorCustom->isChecked());

    const FileNameHighlightRule current = rule();
    QPalette previewPalette = m_preview->palette();
    previewPalette.setColor(QPalette::Window, palette().base().color());
    previewPalette.setColor(QPalette::WindowText, previewColor(current.style, palette()));
    m_preview->setPalette(previewPalette);
    m_preview->setFont(previewFont(current.style, font()));
    m_preview->setText(exampleFileName(current));
}

bool FileNameHighlightingRuleDialog::hasPatterns() const
{
    return condition() == FileNameHighlightRule::Condition::MimeType || condition() == FileNameHighlightRule::Condition::NamePattern;
}

void FileNameHighlightingRuleDialog::chooseMimeTypes()
{
    // The chooser handles exact MIME types; group patterns like "image/*" are kept.
    QStringList groups;
    QStringList exactTypes;
    const QStringList patterns = parsePatterns(m_patterns->text());
    for (const QString &pattern : patterns) {
        (isGroupPattern(pattern) ? groups : exactTypes).append(pattern);
    }

    KMimeTypeChooserDialog dialog(i18nc("@title:window", "Choose MIME Types"),
                                  i18nc("@info", "Select the MIME types whose file names should be highlighted:"),
                                  exactTypes,
                                  QString(),
                                  QStringList(),
                                  KMimeTypeChooser::Comments | KMimeTypeChooser::Patterns,
                                  this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_patterns->setText((groups + dialog.chooser()->mimeTypes()).join(QLatin1String("; ")));
}

FileNameHighlightRule::Condition FileNameHighlightingRuleDialog::condition() const
{
    return static_cast<FileNameHighlightRule::Condition>(m_condition->currentData().toInt());
}

FileNameTextStyle FileNameHighlightingRuleDialog::style() const
{
    FileNameTextStyle style;
    if (m_colorFromScheme->isChecked()) {
        style.color = FileNameColor::schemeRole(static_cast<KColorScheme::ForegroundRole>(m_colorRole->currentData().toInt()));
    } else if (m_colorCustom->isChecked()) {
        style.color = FileNameColor::custom(m_customColor->color());
    }
    style.bold = triStateValue(m_bold->currentIndex());
    style.italic = triStateValue(m_italic->currentIndex());
    return style;
}

#include "moc_filenamehighlightingruledialog.cpp"
