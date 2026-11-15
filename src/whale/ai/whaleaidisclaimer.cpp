/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaidisclaimer.h"
#include "whaleaiagents.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

WhaleAiDisclaimerNotifier *WhaleAiDisclaimerNotifier::instance()
{
    static WhaleAiDisclaimerNotifier notifier;
    return &notifier;
}

WhaleAiDisclaimer::WhaleAiDisclaimer(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("aiDisclaimer"));

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("content"));

    // Header: icon and title.
    const int iconSize = style()->pixelMetric(QStyle::PM_LargeIconSize);
    auto *icon = new QLabel(content);
    icon->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-warning")).pixmap(iconSize));
    icon->setAlignment(Qt::AlignTop);
    auto *title = new QLabel(i18nc("@title", "Before you start using AI agents"), content);
    title->setObjectName(QStringLiteral("title"));
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.2);
    title->setFont(titleFont);
    title->setWordWrap(true);
    auto *header = new QHBoxLayout;
    header->addWidget(icon, 0, Qt::AlignTop);
    header->addWidget(title, 1, Qt::AlignVCenter);

    auto *text = new QLabel(content);
    text->setObjectName(QStringLiteral("text"));
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    auto item = [](const QString &lead, const QString &rest) {
        return QStringLiteral("<li><b>%1</b> %2</li>").arg(lead.toHtmlEscaped(), rest.toHtmlEscaped());
    };
    auto paragraph = [](const QString &text) {
        return QStringLiteral("<p>%1</p>").arg(text.toHtmlEscaped());
    };
    auto heading = [](const QString &text) {
        return QStringLiteral("<p><b>%1</b></p>").arg(text.toHtmlEscaped());
    };
    text->setText(
        paragraph(i18nc("@info",
                        "AI agents are powerful tools. They can modify files, execute commands, install software, and change system "
                        "settings. Mistakes can cause data loss or damage your operating system."))
        + heading(i18nc("@info", "Please keep the following in mind:")) + QStringLiteral("<ul>")
        + item(i18nc("@info lead of a list item", "Start safely."),
               i18nc("@info", "If you are new to AI agents, experiment in a virtual machine first. You can run Orcinus inside one."))
        + item(i18nc("@info lead of a list item", "Know the basics."),
               i18nc("@info",
                     "Familiarity with Linux users, permissions, and file systems is strongly recommended. Advanced system "
                     "administration knowledge is helpful, but not required."))
        + item(i18nc("@info lead of a list item", "Ask when unsure."),
               i18nc("@info", "If you do not understand an action or its consequences, ask the agent to explain before proceeding."))
        + item(i18nc("@info lead of a list item", "Keep backups."), i18nc("@info", "Protect important files before giving agents access to your system."))
        + QStringLiteral("</ul>") + heading(i18nc("@title the user decides about AI agents and answers for it", "Your system, your responsibility."))
        + paragraph(i18nc("@info",
                          "You decide which agents to trust and what access to grant them. Orcinus is free software under the GNU "
                          "General Public License, version 2 or later. As its sections 11 and 12 state, it comes with no warranty, "
                          "to the extent permitted by applicable law, and no copyright holder or contributor is liable for any "
                          "damage arising from its use, including what an AI agent does through it.")));

    auto *license = new QToolButton(content);
    license->setObjectName(QStringLiteral("viewLicense"));
    license->setIcon(QIcon::fromTheme(QStringLiteral("license")));
    license->setText(i18nc("@action:button", "View License"));
    license->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    license->setAutoRaise(true);

    auto *line = new QFrame(content);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    m_understood = new QCheckBox(i18nc("@option:check", "I understand the risks of using AI agents."), content);
    m_understood->setObjectName(QStringLiteral("understood"));
    m_continue = new QPushButton(i18nc("@action:button", "Continue"), content);
    m_continue->setObjectName(QStringLiteral("continue"));
    m_continue->setEnabled(false);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_continue);

    auto *layout = new QVBoxLayout(content);
    layout->addLayout(header);
    layout->addWidget(text);
    layout->addWidget(license, 0, Qt::AlignLeft);
    layout->addWidget(line);
    layout->addWidget(m_understood);
    layout->addLayout(buttons);
    layout->addStretch();

    // The AI panel can be narrow: the text scrolls, it does not widen the panel.
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(m_understood, &QCheckBox::toggled, m_continue, &QPushButton::setEnabled);
    connect(license, &QToolButton::clicked, this, &WhaleAiDisclaimer::showLicense);
    connect(m_continue, &QPushButton::clicked, this, [] {
        WhaleAiAgents::setDisclaimerAccepted(true);
        Q_EMIT WhaleAiDisclaimerNotifier::instance()->accepted();
    });
}

void WhaleAiDisclaimer::showLicense()
{
    // The application's license (GPL 2 or later); the plain GPL 2 where there is none (tests).
    const QList<KAboutLicense> licenses = KAboutData::applicationData().licenses();
    KAboutLicense license = KAboutLicense::byKeyword(QStringLiteral("GPLV2"));
    if (!licenses.isEmpty() && licenses.first().key() != KAboutLicense::Unknown) {
        license = licenses.first();
    }

    auto *dialog = new QDialog(window());
    dialog->setObjectName(QStringLiteral("licenseDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(license.name(KAboutLicense::FullName));
    auto *view = new QPlainTextEdit(license.text(), dialog);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont font = view->font();
    font.setFamily(QStringLiteral("monospace"));
    font.setStyleHint(QFont::Monospace);
    view->setFont(font);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(view);
    layout->addWidget(buttons);
    dialog->resize(view->fontMetrics().horizontalAdvance(QLatin1Char('m')) * 84, 600);
    dialog->show();
}

#include "moc_whaleaidisclaimer.cpp"
