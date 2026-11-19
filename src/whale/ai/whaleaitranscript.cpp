/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaitranscript.h"

#include <KLocalizedString>

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QMetaEnum>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
const char KindProperty[] = "entryKind";
const char TimeProperty[] = "entryTime";
const char RowProperty[] = "transcriptRow";
const char EntryProperty[] = "transcriptEntry";
const char RecordProperty[] = "entryRecord";

QString statusName(WhaleAcpToolCall::Status status)
{
    switch (status) {
    case WhaleAcpToolCall::Status::Pending:
        return QStringLiteral("pending");
    case WhaleAcpToolCall::Status::InProgress:
        return QStringLiteral("in_progress");
    case WhaleAcpToolCall::Status::Completed:
        return QStringLiteral("completed");
    case WhaleAcpToolCall::Status::Failed:
        return QStringLiteral("failed");
    case WhaleAcpToolCall::Status::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QString();
}

/** The locale's time with seconds, without the time zone. */
QString timeFormat()
{
    static const QString format = QLocale().timeFormat(QLocale::LongFormat).remove(QRegularExpression(QStringLiteral("\\s*t+"))).trimmed();
    return format;
}

/**
 * A push button that never widens its parent: a text that does not fit ends in "…", and the full
 * text is its tooltip.
 */
class ElidedButton : public QPushButton
{
public:
    using QPushButton::QPushButton;

    QSize minimumSizeHint() const override
    {
        QSize size = QPushButton::minimumSizeHint();
        size.setWidth(std::min(size.width(), fontMetrics().horizontalAdvance(QStringLiteral("MMM…")) + iconSize().width()));
        return size;
    }

    bool isElided() const
    {
        return width() < sizeHint().width();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QPushButton::resizeEvent(event);
        setToolTip(isElided() ? text() : QString());
    }

    void paintEvent(QPaintEvent *) override
    {
        QStyleOptionButton option;
        initStyleOption(&option);
        if (isElided()) {
            const QRect contents = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
            int room = contents.width() - fontMetrics().horizontalAdvance(QLatin1Char(' '));
            if (!option.icon.isNull()) {
                room -= option.iconSize.width() + 4;
            }
            option.text = fontMetrics().elidedText(option.text, Qt::ElideRight, std::max(room, 0));
        }
        QPainter painter(this);
        style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
    }
};

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

QString kindIconName(const QString &kind)
{
    static const QHash<QString, QString> icons{
        {QStringLiteral("read"), QStringLiteral("document-open")},
        {QStringLiteral("edit"), QStringLiteral("document-edit")},
        {QStringLiteral("delete"), QStringLiteral("edit-delete")},
        {QStringLiteral("move"), QStringLiteral("edit-move")},
        {QStringLiteral("search"), QStringLiteral("edit-find")},
        {QStringLiteral("execute"), QStringLiteral("utilities-terminal")},
        {QStringLiteral("fetch"), QStringLiteral("internet-services")},
        {QStringLiteral("switch_mode"), QStringLiteral("security-medium")},
    };
    return icons.value(kind, QStringLiteral("dialog-question"));
}

QString kindText(const QString &kind)
{
    if (kind == QLatin1String("read")) {
        return i18nc("@info what an AI agent wants to do", "Read files");
    }
    if (kind == QLatin1String("edit")) {
        return i18nc("@info what an AI agent wants to do", "Change files");
    }
    if (kind == QLatin1String("delete")) {
        return i18nc("@info what an AI agent wants to do", "Delete files");
    }
    if (kind == QLatin1String("move")) {
        return i18nc("@info what an AI agent wants to do", "Move or rename files");
    }
    if (kind == QLatin1String("search")) {
        return i18nc("@info what an AI agent wants to do", "Search");
    }
    if (kind == QLatin1String("execute")) {
        return i18nc("@info what an AI agent wants to do", "Run a command");
    }
    if (kind == QLatin1String("fetch")) {
        return i18nc("@info what an AI agent wants to do", "Fetch from the internet");
    }
    if (kind == QLatin1String("switch_mode")) {
        return i18nc("@info what an AI agent wants to do", "Change its mode");
    }
    return QString();
}

/** @return The command line of an `execute` call, as the agents put it into rawInput. */
QString commandOf(const WhaleAcpToolCall &toolCall)
{
    const QJsonValue command = toolCall.rawInput.toObject().value(QLatin1String("command"));
    if (command.isString()) {
        return command.toString();
    }
    if (command.isArray()) {
        QStringList words;
        const QJsonArray array = command.toArray();
        for (const QJsonValue &word : array) {
            words.append(word.toString());
        }
        return words.join(QLatin1Char(' '));
    }
    return QString();
}

/** @return The `diff` items of the tool call's content as text: the path, removed and added lines. */
QString diffText(const WhaleAcpToolCall &toolCall)
{
    constexpr int MaxLines = 200;
    QStringList lines;
    for (const QJsonValue &item : toolCall.content) {
        const QJsonObject diff = item.toObject();
        if (diff.value(QLatin1String("type")).toString() != QLatin1String("diff")) {
            continue;
        }
        if (!lines.isEmpty()) {
            lines.append(QString());
        }
        const QJsonValue oldText = diff.value(QLatin1String("oldText"));
        lines.append(oldText.isString() && !oldText.toString().isEmpty()
                         ? diff.value(QLatin1String("path")).toString()
                         : i18nc("@info %1 is a path", "%1 (new file)", diff.value(QLatin1String("path")).toString()));
        if (oldText.isString() && !oldText.toString().isEmpty()) {
            const QStringList removed = oldText.toString().split(QLatin1Char('\n'));
            for (const QString &line : removed) {
                lines.append(QStringLiteral("- ") + line);
            }
        }
        const QStringList added = diff.value(QLatin1String("newText")).toString().split(QLatin1Char('\n'));
        for (const QString &line : added) {
            lines.append(QStringLiteral("+ ") + line);
        }
    }
    if (lines.size() > MaxLines) {
        const int more = lines.size() - MaxLines;
        lines = lines.mid(0, MaxLines);
        lines.append(i18ncp("@info", "… and one more line", "… and %1 more lines", more));
    }
    return lines.join(QLatin1Char('\n'));
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

/** What a tool call row shows (see toolCallDetails()); with @p content also the changes. */
QJsonObject toolCallRecord(const WhaleAcpToolCall &toolCall, bool content)
{
    QJsonObject object{
        {QStringLiteral("id"), toolCall.id},
        {QStringLiteral("title"), toolCall.title},
        {QStringLiteral("kind"), toolCall.kind},
        {QStringLiteral("status"), statusName(toolCall.status)},
        {QStringLiteral("locations"), QJsonArray::fromStringList(toolCall.locations)},
    };
    if (!toolCall.rawInput.isUndefined() && !toolCall.rawInput.isNull()) {
        object.insert(QStringLiteral("rawInput"), toolCall.rawInput);
    }
    // Only as much as is shown: an output can be a whole file.
    const QString output = jsonText(toolCall.rawOutput).left(2000);
    if (!output.isEmpty()) {
        object.insert(QStringLiteral("rawOutput"), output);
    }
    if (content && !toolCall.content.isEmpty()) {
        object.insert(QStringLiteral("content"), toolCall.content);
    }
    return object;
}

WhaleAcpToolCall toolCallFromRecord(const QJsonObject &object)
{
    WhaleAcpToolCall toolCall;
    toolCall.id = object.value(QLatin1String("id")).toString();
    toolCall.title = object.value(QLatin1String("title")).toString();
    toolCall.kind = object.value(QLatin1String("kind")).toString();
    const QString status = object.value(QLatin1String("status")).toString();
    toolCall.status = status == QLatin1String("cancelled") ? WhaleAcpToolCall::Status::Cancelled : WhaleAcpToolCall::statusFromString(status);
    for (const QJsonValue &location : object.value(QLatin1String("locations")).toArray()) {
        toolCall.locations.append(location.toString());
    }
    toolCall.rawInput = object.value(QLatin1String("rawInput"));
    toolCall.rawOutput = object.value(QLatin1String("rawOutput"));
    toolCall.content = object.value(QLatin1String("content")).toArray();
    return toolCall;
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
    const QDateTime now = m_restoreTime.isValid() ? m_restoreTime : QDateTime::currentDateTime();
    const QList<QWidget *> before = entries();
    const QDateTime previous = before.isEmpty() ? QDateTime() : timeOf(before.constLast());
    entry->setProperty(KindProperty, QVariant::fromValue(int(kind)));
    entry->setProperty(TimeProperty, now);
    entry->setProperty(RecordProperty,
                       QJsonObject{{QStringLiteral("kind"), QString::fromLatin1(QMetaEnum::fromType<Kind>().valueToKey(int(kind)))},
                                   {QStringLiteral("time"), now.toString(Qt::ISODateWithMs)}});

    // Each entry in a row of its own, its time at the start (shown by the "Times" filter); the
    // date too when the day changed since the entry before.
    auto *row = new QWidget(m_content);
    row->setObjectName(QStringLiteral("transcriptRow"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *stamp = new QLabel(row);
    stamp->setObjectName(QStringLiteral("timestamp"));
    stamp->setForegroundRole(QPalette::PlaceholderText);
    QFont small = stamp->font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    small.setFeature(QFont::Tag("tnum"), 1);
    stamp->setFont(small);
    const QString time = QLocale().toString(now.time(), timeFormat());
    stamp->setText(previous.isValid() && previous.date() == now.date() ? time : QLocale().toString(now.date(), QLocale::ShortFormat) + QLatin1Char(' ') + time);
    stamp->setToolTip(QLocale().toString(now, QLocale::LongFormat));
    stamp->setMinimumWidth(QFontMetrics(small).horizontalAdvance(time));
    stamp->setAlignment(Qt::AlignRight | Qt::AlignTop);
    stamp->setContentsMargins(0, entry->contentsMargins().top(), 0, 0);
    stamp->setVisible(m_showTimestamps);
    layout->addWidget(stamp, 0, Qt::AlignTop);
    layout->addWidget(entry, 1);
    row->setProperty(EntryProperty, QVariant::fromValue(entry));
    entry->setProperty(RowProperty, QVariant::fromValue(row));

    // Before the stretch at the end.
    m_layout->insertWidget(m_layout->count() - 1, row);
    setEntryVisible(entry, m_showActions || !isAction(kind));
    Q_EMIT changed();
    return entry;
}

void WhaleAiTranscript::updateRecord(QWidget *widget, const QJsonObject &fields)
{
    QWidget *entry = widget;
    while (entry && !entry->property(RowProperty).isValid()) {
        entry = entry->parentWidget();
    }
    if (!entry) {
        return;
    }
    QJsonObject record = entry->property(RecordProperty).toJsonObject();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        record.insert(it.key(), it.value());
    }
    entry->setProperty(RecordProperty, record);
    Q_EMIT changed();
}

void WhaleAiTranscript::setEntryVisible(QWidget *entry, bool visible)
{
    entry->setVisible(visible);
    if (auto *row = entry->property(RowProperty).value<QWidget *>()) {
        row->setVisible(visible);
    }
}

bool WhaleAiTranscript::isAction(Kind kind)
{
    // The agent's thinking goes with what it did (user, 2026-11-18): off, only the conversation is left.
    return kind == Kind::ToolCall || kind == Kind::Action || kind == Kind::Thought;
}

void WhaleAiTranscript::setShowActions(bool show)
{
    if (m_showActions == show) {
        return;
    }
    m_showActions = show;
    for (QWidget *entry : entries()) {
        if (isAction(kindOf(entry))) {
            setEntryVisible(entry, show);
        }
    }
}

void WhaleAiTranscript::setShowTimestamps(bool show)
{
    if (m_showTimestamps == show) {
        return;
    }
    m_showTimestamps = show;
    for (QWidget *label : m_content->findChildren<QLabel *>(QStringLiteral("timestamp"))) {
        label->setVisible(show);
    }
}

bool WhaleAiTranscript::showTimestamps() const
{
    return m_showTimestamps;
}

QDateTime WhaleAiTranscript::timeOf(const QWidget *entry)
{
    return entry->property(TimeProperty).toDateTime();
}

bool WhaleAiTranscript::showActions() const
{
    return m_showActions;
}

bool WhaleAiTranscript::hasOpenPermissionRequest() const
{
    return !m_permissions.isEmpty();
}

void WhaleAiTranscript::addUserMessage(const QString &text, const QString &context)
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
    if (!context.isEmpty()) {
        QLabel *contextLabel = createTextLabel(Qt::PlainText);
        contextLabel->setParent(frame);
        contextLabel->setObjectName(QStringLiteral("context"));
        contextLabel->setText(context);
        contextLabel->setForegroundRole(QPalette::PlaceholderText);
        QFont small = contextLabel->font();
        small.setPointSizeF(small.pointSizeF() * 0.9);
        contextLabel->setFont(small);
        layout->addWidget(contextLabel);
    }
    addEntry(Kind::UserMessage, frame);
    updateRecord(frame, {{QStringLiteral("text"), text}, {QStringLiteral("context"), context}});
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
    updateRecord(m_currentAnswer, {{QStringLiteral("text"), m_currentAnswerText}});
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
    updateRecord(m_currentThought, {{QStringLiteral("text"), m_currentThoughtText}});
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
    updateRecord(entry.row, {{QStringLiteral("toolCall"), toolCallRecord(toolCall, false)}});
}

void WhaleAiTranscript::addPermissionRequest(const WhaleAcpPermissionRequest &request)
{
    endTurn();
    auto *frame = new QFrame(m_content);
    frame->setObjectName(QStringLiteral("permission-") + request.key);
    frame->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QVBoxLayout(frame);

    const WhaleAcpToolCall &toolCall = request.toolCall;
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    // What kind of thing it wants to do, with an icon, then the agent's own title.
    auto *head = new QHBoxLayout;
    auto *icon = new QLabel(frame);
    icon->setObjectName(QStringLiteral("kindIcon"));
    const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize);
    icon->setPixmap(QIcon::fromTheme(kindIconName(toolCall.kind)).pixmap(iconSize));
    icon->setAlignment(Qt::AlignTop);
    head->addWidget(icon);
    QLabel *question = createTextLabel(Qt::PlainText);
    question->setParent(frame);
    const QString title = toolCall.title.isEmpty() ? toolCall.kind : toolCall.title;
    question->setText(i18nc("@info %1 describes what an AI agent wants to do", "The agent asks for permission: %1", title));
    QFont bold = question->font();
    bold.setBold(true);
    question->setFont(bold);
    head->addWidget(question, 1);
    layout->addLayout(head);

    const QString kind = kindText(toolCall.kind);
    if (!kind.isEmpty()) {
        QLabel *kindLabel = createTextLabel(Qt::PlainText);
        kindLabel->setParent(frame);
        kindLabel->setObjectName(QStringLiteral("kind"));
        kindLabel->setText(kind);
        kindLabel->setForegroundRole(QPalette::PlaceholderText);
        layout->addWidget(kindLabel);
    }

    // The command decides it: always in sight.
    const QString command = toolCall.kind == QLatin1String("execute") ? commandOf(toolCall) : QString();
    if (!command.isEmpty()) {
        QLabel *commandLabel = createTextLabel(Qt::PlainText);
        commandLabel->setParent(frame);
        commandLabel->setObjectName(QStringLiteral("command"));
        commandLabel->setFont(mono);
        commandLabel->setText(command);
        layout->addWidget(commandLabel);
    }

    if (!toolCall.locations.isEmpty()) {
        QLabel *paths = createTextLabel(Qt::PlainText);
        paths->setParent(frame);
        paths->setObjectName(QStringLiteral("paths"));
        paths->setText(toolCall.locations.join(QLatin1Char('\n')));
        layout->addWidget(paths);
    }

    // The changes, else what the agent passed to its tool.
    const QString diff = diffText(toolCall);
    const QString input = diff.isEmpty() ? jsonText(toolCall.rawInput) : QString();
    if (!diff.isEmpty() || !input.isEmpty()) {
        QLabel *body = createTextLabel(Qt::PlainText);
        body->setFont(mono);
        body->setText(diff.isEmpty() ? input : diff);
        QWidget *foldable = createFoldable(diff.isEmpty() ? i18nc("@action:button unfolds technical details", "Details")
                                                          : i18nc("@action:button unfolds the changes an AI agent wants to make", "Changes"),
                                           body,
                                           false);
        foldable->setObjectName(diff.isEmpty() ? QStringLiteral("details") : QStringLiteral("changes"));
        foldable->setParent(frame);
        layout->addWidget(foldable);
    }

    auto *buttons = new QWidget(frame);
    buttons->setObjectName(QStringLiteral("buttons"));
    // One below the other: options can be long ("Yes, and don't ask again for commands that start
    // with …"), and the panel is narrow.
    auto *buttonLayout = new QVBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    QLabel *answer = createTextLabel(Qt::PlainText);
    answer->setParent(frame);
    answer->setObjectName(QStringLiteral("answer"));
    answer->hide();

    for (const WhaleAcpPermissionRequest::Option &option : request.options) {
        auto *button = new ElidedButton(option.name, buttons);
        button->setObjectName(QStringLiteral("option-") + option.id);
        button->setProperty("optionKind", option.kind);
        if (option.kind.startsWith(QLatin1String("allow"))) {
            button->setIcon(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")));
        } else if (option.kind.startsWith(QLatin1String("reject"))) {
            button->setIcon(QIcon::fromTheme(QStringLiteral("dialog-cancel")));
        }
        // As wide as its text, at most the panel (an alignment would make the text a minimum).
        button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        buttonLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, key = request.key, option, buttons, answer] {
            // Hiding the focused button would pass the focus on to the next widget, often the file view.
            const bool hadFocus = buttons->isAncestorOf(QApplication::focusWidget());
            buttons->hide();
            if (hadFocus && m_focusReturn) {
                m_focusReturn->setFocus(Qt::OtherFocusReason);
            }
            answer->setText(i18nc("@info the option the user chose for a permission request", "Answered: %1", option.name));
            answer->show();
            updateRecord(answer, {{QStringLiteral("answer"), option.name}});
            m_permissions.remove(key);
            Q_EMIT permissionAnswered(key, option.id);
            Q_EMIT openPermissionsChanged();
        });
    }
    layout->addWidget(buttons);
    layout->addWidget(answer);

    m_permissions.insert(request.key, frame);
    addEntry(Kind::Permission, frame);
    QJsonArray options;
    for (const WhaleAcpPermissionRequest::Option &option : request.options) {
        options.append(QJsonObject{{QStringLiteral("id"), option.id}, {QStringLiteral("name"), option.name}, {QStringLiteral("kind"), option.kind}});
    }
    updateRecord(frame, {{QStringLiteral("toolCall"), toolCallRecord(toolCall, true)}, {QStringLiteral("options"), options}});
    Q_EMIT openPermissionsChanged();
    // A question must be seen.
    m_followBottom = true;
    scrollToBottomLater();
}

bool WhaleAiTranscript::focusPermissionRequest(const QString &key)
{
    QWidget *frame = m_permissions.value(key);
    if (!frame) {
        return false;
    }
    const auto buttons = frame->findChild<QWidget *>(QStringLiteral("buttons"))->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->property("optionKind").toString().startsWith(QLatin1String("allow"))) {
            button->setFocus(Qt::OtherFocusReason);
            return true;
        }
    }
    if (!buttons.isEmpty()) {
        buttons.first()->setFocus(Qt::OtherFocusReason);
        return true;
    }
    return false;
}

void WhaleAiTranscript::setFocusReturn(QWidget *widget)
{
    m_focusReturn = widget;
}

void WhaleAiTranscript::withdrawPermissionRequest(const QString &key)
{
    QWidget *frame = m_permissions.take(key);
    if (!frame) {
        return;
    }
    Q_EMIT openPermissionsChanged();
    QWidget *buttons = frame->findChild<QWidget *>(QStringLiteral("buttons"));
    const bool hadFocus = buttons->isAncestorOf(QApplication::focusWidget());
    buttons->hide();
    if (hadFocus && m_focusReturn) {
        m_focusReturn->setFocus(Qt::OtherFocusReason);
    }
    auto *answer = frame->findChild<QLabel *>(QStringLiteral("answer"));
    answer->setText(i18nc("@info a permission request of an AI agent", "No longer needed."));
    answer->show();
    updateRecord(frame, {{QStringLiteral("withdrawn"), true}});
}

void WhaleAiTranscript::addNotice(const QString &text, Kind kind, const QString &details, const QString &toolTip)
{
    endTurn();
    QLabel *label = createTextLabel(Qt::PlainText);
    label->setText(text);
    label->setToolTip(toolTip);
    QFont italic = label->font();
    italic.setItalic(kind == Kind::Notice || kind == Kind::Action);
    label->setFont(italic);
    if (kind == Kind::Error) {
        QPalette palette = label->palette();
        palette.setColor(QPalette::WindowText, QColor(0xda, 0x44, 0x53));
        label->setPalette(palette);
    } else {
        label->setForegroundRole(QPalette::PlaceholderText);
    }

    const QJsonObject fields{{QStringLiteral("text"), text}, {QStringLiteral("details"), details}, {QStringLiteral("toolTip"), toolTip}};
    if (details.isEmpty()) {
        addEntry(kind, label);
        updateRecord(label, fields);
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
    updateRecord(box, fields);
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
    Q_EMIT openPermissionsChanged();
    QList<QWidget *> rows;
    for (int i = 0; i < m_layout->count(); ++i) {
        if (QWidget *row = m_layout->itemAt(i)->widget()) {
            rows.append(row);
        }
    }
    qDeleteAll(rows);
    Q_EMIT changed();
    m_followBottom = true;
}

QList<QWidget *> WhaleAiTranscript::entries() const
{
    QList<QWidget *> list;
    for (int i = 0; i < m_layout->count(); ++i) {
        if (QWidget *row = m_layout->itemAt(i)->widget()) {
            list.append(row->property(EntryProperty).value<QWidget *>());
        }
    }
    return list;
}

int WhaleAiTranscript::entryCount() const
{
    // The stretch at the end is no entry.
    return m_layout->count() - 1;
}

QJsonArray WhaleAiTranscript::record(int from, int count) const
{
    QJsonArray array;
    const QList<QWidget *> list = entries();
    const qsizetype end = count < 0 ? list.size() : std::min<qsizetype>(list.size(), qsizetype(std::max(0, from)) + count);
    for (qsizetype i = std::max(0, from); i < end; ++i) {
        array.append(list.at(i)->property(RecordProperty).toJsonObject());
    }
    return array;
}

void WhaleAiTranscript::restore(const QJsonArray &record)
{
    endTurn();
    const QMetaEnum kinds = QMetaEnum::fromType<Kind>();
    int index = 0;
    for (const QJsonValue &value : record) {
        const QJsonObject entry = value.toObject();
        bool known = false;
        const Kind kind = Kind(kinds.keyToValue(entry.value(QLatin1String("kind")).toString().toLatin1().constData(), &known));
        if (!known) {
            continue;
        }
        m_restoreTime = QDateTime::fromString(entry.value(QLatin1String("time")).toString(), Qt::ISODateWithMs);
        if (!m_restoreTime.isValid()) {
            m_restoreTime = QDateTime::currentDateTime();
        }
        const QString text = entry.value(QLatin1String("text")).toString();
        switch (kind) {
        case Kind::UserMessage:
            addUserMessage(text, entry.value(QLatin1String("context")).toString());
            break;
        case Kind::AgentMessage:
            endTurn();
            appendAgentText(text);
            endTurn();
            break;
        case Kind::Thought:
            endTurn();
            appendThought(text);
            endTurn();
            break;
        case Kind::ToolCall:
            updateToolCall(toolCallFromRecord(entry.value(QLatin1String("toolCall")).toObject()));
            // A second call with the same id would update this one.
            m_toolCalls.remove(entry.value(QLatin1String("toolCall")).toObject().value(QLatin1String("id")).toString());
            break;
        case Kind::Permission: {
            WhaleAcpPermissionRequest request;
            request.key = QStringLiteral("restored-%1").arg(index);
            request.toolCall = toolCallFromRecord(entry.value(QLatin1String("toolCall")).toObject());
            for (const QJsonValue &option : entry.value(QLatin1String("options")).toArray()) {
                const QJsonObject object = option.toObject();
                request.options.append({object.value(QLatin1String("id")).toString(),
                                        object.value(QLatin1String("name")).toString(),
                                        object.value(QLatin1String("kind")).toString()});
            }
            const QString answer = entry.value(QLatin1String("answer")).toString();
            addPermissionRequest(request);
            QWidget *frame = m_permissions.take(request.key);
            frame->findChild<QWidget *>(QStringLiteral("buttons"))->hide();
            auto *label = frame->findChild<QLabel *>(QStringLiteral("answer"));
            if (answer.isEmpty()) {
                label->setText(i18nc("@info a permission request of an AI agent", "No longer needed."));
                updateRecord(frame, {{QStringLiteral("withdrawn"), true}});
            } else {
                label->setText(i18nc("@info the option the user chose for a permission request", "Answered: %1", answer));
                updateRecord(frame, {{QStringLiteral("answer"), answer}});
            }
            label->show();
            Q_EMIT openPermissionsChanged();
            break;
        }
        case Kind::Notice:
        case Kind::Action:
        case Kind::Error:
            addNotice(text, kind, entry.value(QLatin1String("details")).toString(), entry.value(QLatin1String("toolTip")).toString());
            break;
        }
        ++index;
    }
    m_restoreTime = QDateTime();
    endTurn();
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
