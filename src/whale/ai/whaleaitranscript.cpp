/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaitranscript.h"

#include <KLocalizedString>

#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
const char KindProperty[] = "entryKind";

QString statusIconName(WhaleAcpToolCall::Status status)
{
    switch (status) {
    case WhaleAcpToolCall::Status::Pending:
        return QStringLiteral("chronometer");
    case WhaleAcpToolCall::Status::InProgress:
        return QStringLiteral("view-refresh");
    case WhaleAcpToolCall::Status::Completed:
        return QStringLiteral("dialog-ok-apply");
    case WhaleAcpToolCall::Status::Failed:
        return QStringLiteral("dialog-error");
    case WhaleAcpToolCall::Status::Cancelled:
        return QStringLiteral("dialog-cancel");
    }
    return QString();
}

QString statusText(WhaleAcpToolCall::Status status)
{
    switch (status) {
    case WhaleAcpToolCall::Status::Pending:
        return i18nc("@info:tooltip state of a tool call of an AI agent", "Waiting");
    case WhaleAcpToolCall::Status::InProgress:
        return i18nc("@info:tooltip state of a tool call of an AI agent", "Running");
    case WhaleAcpToolCall::Status::Completed:
        return i18nc("@info:tooltip state of a tool call of an AI agent", "Done");
    case WhaleAcpToolCall::Status::Failed:
        return i18nc("@info:tooltip state of a tool call of an AI agent", "Failed");
    case WhaleAcpToolCall::Status::Cancelled:
        return i18nc("@info:tooltip state of a tool call of an AI agent", "Cancelled");
    }
    return QString();
}

QString jsonText(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull()) {
        return QString();
    }
    if (value.isString()) {
        return value.toString();
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented)).trimmed();
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented)).trimmed();
    }
    return value.toVariant().toString();
}

QString toolCallDetails(const WhaleAcpToolCall &toolCall)
{
    QStringList parts;
    if (!toolCall.locations.isEmpty()) {
        parts.append(toolCall.locations.join(QLatin1Char('\n')));
    }
    const QString input = jsonText(toolCall.rawInput);
    if (!input.isEmpty()) {
        parts.append(input);
    }
    const QString output = jsonText(toolCall.rawOutput);
    if (!output.isEmpty()) {
        parts.append(output.left(2000));
    }
    return parts.join(QStringLiteral("\n\n"));
}
}

WhaleAiTranscript::WhaleAiTranscript(QWidget *parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("transcriptContent"));
    m_layout = new QVBoxLayout(m_content);
    m_layout->addStretch();
    setWidget(m_content);

    // Follow new entries while at the bottom.
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int maximum) {
        if (m_followBottom) {
            verticalScrollBar()->setValue(maximum);
        }
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        m_followBottom = value >= verticalScrollBar()->maximum() - 4;
    });
}

QLabel *WhaleAiTranscript::createTextLabel(Qt::TextFormat format)
{
    auto *label = new QLabel(m_content);
    label->setTextFormat(format);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard | Qt::LinksAccessibleByMouse);
    label->setOpenExternalLinks(false);
    // Long words and code lines must not widen the panel.
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    connect(label, &QLabel::linkActivated, this, &WhaleAiTranscript::linkActivated);
    return label;
}

QWidget *WhaleAiTranscript::createFoldable(const QString &title, QLabel *body, bool unfolded)
{
    auto *box = new QWidget(m_content);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toggle = new QToolButton(box);
    toggle->setObjectName(QStringLiteral("toggle"));
    toggle->setText(title);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setAutoRaise(true);
    toggle->setCheckable(true);
    toggle->setChecked(unfolded);
    toggle->setArrowType(unfolded ? Qt::DownArrow : Qt::RightArrow);
    layout->addWidget(toggle);

    body->setParent(box);
    body->setVisible(unfolded);
    body->setContentsMargins(toggle->iconSize().width() + 6, 0, 0, 0);
    layout->addWidget(body);

    connect(toggle, &QToolButton::toggled, body, [toggle, body](bool on) {
        toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        body->setVisible(on);
    });
    return box;
}

QWidget *WhaleAiTranscript::addEntry(Kind kind, QWidget *entry)
{
    entry->setProperty(KindProperty, QVariant::fromValue(int(kind)));
    // Before the stretch at the end.
    m_layout->insertWidget(m_layout->count() - 1, entry);
    entry->show();
    return entry;
}

void WhaleAiTranscript::addUserMessage(const QString &text)
{
    endTurn();
    auto *frame = new QFrame(m_content);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setBackgroundRole(QPalette::AlternateBase);
    frame->setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(frame);
    QLabel *label = createTextLabel(Qt::PlainText);
    label->setParent(frame);
    label->setText(text);
    layout->addWidget(label);
    addEntry(Kind::UserMessage, frame);
}

void WhaleAiTranscript::appendAgentText(const QString &text)
{
    m_currentThought = nullptr;
    if (!m_currentAnswer) {
        m_currentAnswerText.clear();
        m_currentAnswer = createTextLabel(Qt::MarkdownText);
        addEntry(Kind::AgentMessage, m_currentAnswer);
    }
    m_currentAnswerText += text;
    m_currentAnswer->setText(m_currentAnswerText);
}

void WhaleAiTranscript::appendThought(const QString &text)
{
    m_currentAnswer = nullptr;
    if (!m_currentThought) {
        m_currentThoughtText.clear();
        m_currentThought = createTextLabel(Qt::PlainText);
        m_currentThought->setForegroundRole(QPalette::PlaceholderText);
        addEntry(Kind::Thought, createFoldable(i18nc("@action:button AI agent's thinking, unfolds it", "Thinking"), m_currentThought, false));
    }
    m_currentThoughtText += text;
    m_currentThought->setText(m_currentThoughtText);
}

void WhaleAiTranscript::updateToolCall(const WhaleAcpToolCall &toolCall)
{
    auto it = m_toolCalls.find(toolCall.id);
    if (it == m_toolCalls.end()) {
        // A tool call ends the current answer; text after it is a new one.
        endTurn();
        ToolCallEntry entry;
        entry.details = createTextLabel(Qt::PlainText);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        entry.details->setFont(mono);
        entry.row = createFoldable(QString(), entry.details, false);
        entry.row->setObjectName(QStringLiteral("toolCall-") + toolCall.id);
        auto *toggle = entry.row->findChild<QToolButton *>(QStringLiteral("toggle"));
        // The status icon goes left of the title.
        auto *header = new QWidget(entry.row);
        auto *headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(4);
        entry.icon = new QLabel(header);
        entry.icon->setObjectName(QStringLiteral("status"));
        entry.title = new QLabel(header);
        entry.title->setObjectName(QStringLiteral("title"));
        entry.title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        toggle->setParent(header);
        headerLayout->addWidget(toggle);
        headerLayout->addWidget(entry.icon);
        headerLayout->addWidget(entry.title, 1);
        static_cast<QVBoxLayout *>(entry.row->layout())->insertWidget(0, header);
        // Click the title to unfold as well.
        entry.title->setTextInteractionFlags(Qt::NoTextInteraction);
        entry.title->installEventFilter(this);
        addEntry(Kind::ToolCall, entry.row);
        it = m_toolCalls.insert(toolCall.id, entry);
    }

    const ToolCallEntry &entry = it.value();
    const int size = style()->pixelMetric(QStyle::PM_SmallIconSize);
    entry.icon->setPixmap(QIcon::fromTheme(statusIconName(toolCall.status)).pixmap(size, size));
    entry.icon->setToolTip(statusText(toolCall.status));
    entry.icon->setProperty("status", int(toolCall.status));
    const QString title = toolCall.title.isEmpty() ? toolCall.kind : toolCall.title;
    entry.title->setText(title);
    entry.title->setToolTip(title);
    const QString details = toolCallDetails(toolCall);
    entry.details->setText(details);
    entry.row->findChild<QToolButton *>(QStringLiteral("toggle"))->setEnabled(!details.isEmpty());
}

void WhaleAiTranscript::addPermissionRequest(const WhaleAcpPermissionRequest &request)
{
    endTurn();
    auto *frame = new QFrame(m_content);
    frame->setObjectName(QStringLiteral("permission-") + request.key);
    frame->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QVBoxLayout(frame);

    QLabel *question = createTextLabel(Qt::PlainText);
    question->setParent(frame);
    const QString title = request.toolCall.title.isEmpty() ? request.toolCall.kind : request.toolCall.title;
    question->setText(i18nc("@info %1 describes what an AI agent wants to do", "The agent asks for permission: %1", title));
    QFont bold = question->font();
    bold.setBold(true);
    question->setFont(bold);
    layout->addWidget(question);

    if (!request.toolCall.locations.isEmpty()) {
        QLabel *paths = createTextLabel(Qt::PlainText);
        paths->setParent(frame);
        paths->setText(request.toolCall.locations.join(QLatin1Char('\n')));
        layout->addWidget(paths);
    }

    auto *buttons = new QWidget(frame);
    buttons->setObjectName(QStringLiteral("buttons"));
    auto *buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    QLabel *answer = createTextLabel(Qt::PlainText);
    answer->setParent(frame);
    answer->setObjectName(QStringLiteral("answer"));
    answer->hide();

    for (const WhaleAcpPermissionRequest::Option &option : request.options) {
        auto *button = new QPushButton(option.name, buttons);
        button->setObjectName(QStringLiteral("option-") + option.id);
        if (option.kind.startsWith(QLatin1String("allow"))) {
            button->setIcon(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")));
        } else if (option.kind.startsWith(QLatin1String("reject"))) {
            button->setIcon(QIcon::fromTheme(QStringLiteral("dialog-cancel")));
        }
        buttonLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, key = request.key, option, buttons, answer] {
            buttons->hide();
            answer->setText(i18nc("@info the option the user chose for a permission request", "Answered: %1", option.name));
            answer->show();
            m_permissions.remove(key);
            Q_EMIT permissionAnswered(key, option.id);
        });
    }
    buttonLayout->addStretch();
    layout->addWidget(buttons);
    layout->addWidget(answer);

    m_permissions.insert(request.key, frame);
    addEntry(Kind::Permission, frame);
    // A question must be seen.
    m_followBottom = true;
    scrollToBottomLater();
}

void WhaleAiTranscript::withdrawPermissionRequest(const QString &key)
{
    QWidget *frame = m_permissions.take(key);
    if (!frame) {
        return;
    }
    frame->findChild<QWidget *>(QStringLiteral("buttons"))->hide();
    auto *answer = frame->findChild<QLabel *>(QStringLiteral("answer"));
    answer->setText(i18nc("@info a permission request of an AI agent", "No longer needed."));
    answer->show();
}

void WhaleAiTranscript::addNotice(const QString &text, Kind kind, const QString &details)
{
    endTurn();
    QLabel *label = createTextLabel(Qt::PlainText);
    label->setText(text);
    QFont italic = label->font();
    italic.setItalic(kind == Kind::Notice);
    label->setFont(italic);
    if (kind == Kind::Error) {
        QPalette palette = label->palette();
        palette.setColor(QPalette::WindowText, QColor(0xda, 0x44, 0x53));
        label->setPalette(palette);
    } else {
        label->setForegroundRole(QPalette::PlaceholderText);
    }

    if (details.isEmpty()) {
        addEntry(kind, label);
        return;
    }
    auto *box = new QWidget(m_content);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    label->setParent(box);
    layout->addWidget(label);
    QLabel *body = createTextLabel(Qt::PlainText);
    body->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    body->setText(details);
    layout->addWidget(createFoldable(i18nc("@action:button unfolds technical details", "Details"), body, false));
    addEntry(kind, box);
}

void WhaleAiTranscript::endTurn()
{
    m_currentAnswer = nullptr;
    m_currentThought = nullptr;
}

void WhaleAiTranscript::clear()
{
    endTurn();
    m_toolCalls.clear();
    m_permissions.clear();
    const QList<QWidget *> widgets = entries();
    for (QWidget *widget : widgets) {
        delete widget;
    }
    m_followBottom = true;
}

QList<QWidget *> WhaleAiTranscript::entries() const
{
    QList<QWidget *> list;
    for (int i = 0; i < m_layout->count(); ++i) {
        if (QWidget *widget = m_layout->itemAt(i)->widget()) {
            list.append(widget);
        }
    }
    return list;
}

WhaleAiTranscript::Kind WhaleAiTranscript::kindOf(const QWidget *entry)
{
    return Kind(entry->property(KindProperty).toInt());
}

bool WhaleAiTranscript::eventFilter(QObject *watched, QEvent *event)
{
    // A click on the title of a tool call unfolds its details, like the arrow.
    if (event->type() == QEvent::MouseButtonRelease) {
        if (auto *title = qobject_cast<QLabel *>(watched)) {
            if (auto *toggle = title->parentWidget()->findChild<QToolButton *>(QStringLiteral("toggle")); toggle && toggle->isEnabled()) {
                toggle->toggle();
                return true;
            }
        }
    }
    return QScrollArea::eventFilter(watched, event);
}

void WhaleAiTranscript::scrollToBottomLater()
{
    QMetaObject::invokeMethod(
        this,
        [this] {
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        },
        Qt::QueuedConnection);
}

#include "moc_whaleaitranscript.cpp"
