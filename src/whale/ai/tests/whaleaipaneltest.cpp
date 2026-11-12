/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaipanel.h"
#include "dolphindockwidget.h"
#include "dolphinmainwindow.h"
#include "dolphinviewcontainer.h"
#include "testdir.h"
#include "views/dolphinview.h"
#include "whaleaiagents.h"
#include "whaleaitranscript.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KSharedConfig>

#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

/**
 * The AI panel (AI3) against the scripted agent whaleacpfakeagent: sending, the transcript of a
 * turn, permission buttons, stopping, failures, choosing another agent, links, and the panel in
 * the main window.
 */
class WhaleAiPanelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void setupWithoutAgents();
    void promptTurn();
    void enterAndShiftEnter();
    void stopWithEscape();
    void agentFails();
    void otherAgentEndsConversation();
    void newConversation();
    void removedAgentEndsConversation();
    void links();
    void panelInMainWindow();

private:
    static WhaleAiAgent fakeAgent(const QString &id, const QString &scenario);
    static QList<WhaleAiTranscript::Kind> kinds(WhaleAiPanel &panel);
    static QString texts(WhaleAiPanel &panel);
    static bool waitForReady(WhaleAiPanel &panel);

    QTemporaryDir m_home;
    QTemporaryDir m_work;
};

void WhaleAiPanelTest::initTestCase()
{
    // A configuration of its own: other tests change the shared one of the test mode.
    QVERIFY(m_home.isValid());
    for (const char *variable : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"}) {
        const QString path = m_home.filePath(QString::fromLatin1(variable).toLower());
        QVERIFY(QDir().mkpath(path));
        qputenv(variable, QFile::encodeName(path));
    }
    QVERIFY(m_work.isValid());
}

void WhaleAiPanelTest::init()
{
    WhaleAiAgents::setAgents({fakeAgent(QStringLiteral("basic"), QStringLiteral("basic")), fakeAgent(QStringLiteral("crash"), QStringLiteral("crash"))});
    WhaleAiAgents::setDefaultAgentId(QStringLiteral("basic"));
}

WhaleAiAgent WhaleAiPanelTest::fakeAgent(const QString &id, const QString &scenario)
{
    return {id, QStringLiteral("Fake ") + id, QStringLiteral("'%1' %2").arg(QStringLiteral(WHALE_ACP_FAKE_AGENT), scenario), {}, {}};
}

QList<WhaleAiTranscript::Kind> WhaleAiPanelTest::kinds(WhaleAiPanel &panel)
{
    QList<WhaleAiTranscript::Kind> list;
    const QList<QWidget *> entries = panel.transcript()->entries();
    for (const QWidget *entry : entries) {
        list.append(WhaleAiTranscript::kindOf(entry));
    }
    return list;
}

QString WhaleAiPanelTest::texts(WhaleAiPanel &panel)
{
    QStringList list;
    const QList<QWidget *> entries = panel.transcript()->entries();
    for (const QWidget *entry : entries) {
        const auto labels = entry->findChildren<QLabel *>();
        if (auto *label = qobject_cast<const QLabel *>(entry)) {
            list.append(label->text());
        }
        for (const QLabel *label : labels) {
            list.append(label->text());
        }
    }
    return list.join(QLatin1Char('|'));
}

bool WhaleAiPanelTest::waitForReady(WhaleAiPanel &panel)
{
    return QTest::qWaitFor(
        [&panel] {
            return panel.session() && panel.session()->state() == WhaleAcpSession::State::Ready;
        },
        10000);
}

void WhaleAiPanelTest::setupWithoutAgents()
{
    WhaleAiAgents::setAgents({});
    WhaleAiPanel panel;
    auto *stack = panel.findChild<QStackedWidget *>();
    QCOMPARE(stack->currentIndex(), 1);
    QSignalSpy configure(&panel, &WhaleAiPanel::configureRequested);
    panel.findChild<QPushButton *>(QStringLiteral("setUp"))->click();
    QCOMPARE(configure.size(), 1);
    QVERIFY(!panel.sendPrompt(QStringLiteral("hello")));

    // Set up in the settings: the panel shows the chat.
    WhaleAiAgents::setAgents({fakeAgent(QStringLiteral("basic"), QStringLiteral("basic"))});
    panel.readSettings();
    QCOMPARE(stack->currentIndex(), 0);
    QCOMPARE(panel.agentId(), QStringLiteral("basic"));
}

void WhaleAiPanelTest::promptTurn()
{
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    QCOMPARE(panel.findChild<QComboBox *>(QStringLiteral("agents"))->currentText(), QStringLiteral("Fake basic"));
    auto *info = panel.findChild<QLabel *>(QStringLiteral("sessionInfo"));
    QCOMPARE(info->text(), QStringLiteral("Starts with your first message."));
    auto *send = panel.findChild<QToolButton *>(QStringLiteral("send"));
    QVERIFY(!send->isEnabled());

    QVERIFY(panel.sendPrompt(QStringLiteral("hello")));
    QVERIFY(panel.session());
    // The agent asks; the user allows it.
    QWidget *permission = nullptr;
    QVERIFY(QTest::qWaitFor(
        [&] {
            permission = panel.transcript()->findChild<QWidget *>(QStringLiteral("permission-n7"));
            return permission != nullptr;
        },
        10000));
    QVERIFY(send->property("busy").toBool());
    QVERIFY(texts(panel).contains(QLatin1String("The agent asks for permission: Write note.txt")));
    permission->findChild<QPushButton *>(QStringLiteral("option-yes"))->click();
    QVERIFY(waitForReady(panel));
    QVERIFY(!send->property("busy").toBool());

    using Kind = WhaleAiTranscript::Kind;
    // Notice of the folder, the message, the thought, the answer, the tool call, the request,
    // and the answer after it as an entry of its own.
    QCOMPARE(kinds(panel),
             (QList<Kind>{Kind::Notice, Kind::UserMessage, Kind::Thought, Kind::AgentMessage, Kind::ToolCall, Kind::Permission, Kind::AgentMessage}));
    const QString all = texts(panel);
    QVERIFY2(all.contains(QStringLiteral("Fake basic works in %1.").arg(m_work.path())), qPrintable(all));
    QVERIFY2(all.contains(QStringLiteral("cwd=%1;mcp=;text=hello;links=0").arg(m_work.path())), qPrintable(all));
    QVERIFY(all.contains(QLatin1String("permission=yes")));
    QVERIFY(all.contains(QLatin1String("Answered: Yes")));
    QVERIFY(!all.contains(QLatin1String("WRONG SESSION")));
    // The thought is folded.
    QVERIFY(!panel.transcript()->entries().at(2)->findChildren<QLabel *>().first()->isVisibleTo(panel.transcript()->entries().at(2)));

    QWidget *toolCall = panel.transcript()->findChild<QWidget *>(QStringLiteral("toolCall-t1"));
    QVERIFY(toolCall);
    QCOMPARE(toolCall->findChild<QLabel *>(QStringLiteral("title"))->text(), QStringLiteral("Write note.txt"));
    QCOMPARE(toolCall->findChild<QLabel *>(QStringLiteral("status"))->property("status").toInt(), int(WhaleAcpToolCall::Status::Completed));

    QVERIFY2(info->text().contains(QLatin1String("Model: Small")), qPrintable(info->text()));
    QVERIFY(info->text().contains(QLatin1String("Mode: Auto")));
    QVERIFY(info->text().contains(QLatin1String("Context: 4 %")));

    // The same conversation goes on, in the folder it started in.
    panel.setUrl(QUrl::fromLocalFile(QDir::tempPath()));
    WhaleAcpSession *session = panel.session();
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission again")));
    QVERIFY(waitForReady(panel));
    QCOMPARE(panel.session(), session);
    QVERIFY(texts(panel).contains(QStringLiteral("cwd=%1;mcp=;text=nopermission again").arg(m_work.path())));
}

void WhaleAiPanelTest::enterAndShiftEnter()
{
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    auto *input = panel.findChild<QPlainTextEdit *>(QStringLiteral("input"));
    input->setFocus();
    QTest::keyClicks(input, QStringLiteral("first"));
    QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier);
    QTest::keyClicks(input, QStringLiteral("nopermission"));
    QCOMPARE(input->toPlainText(), QStringLiteral("first\nnopermission"));
    QVERIFY(!panel.session());

    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(input->toPlainText().isEmpty());
    QVERIFY(panel.session());
    QVERIFY(waitForReady(panel));
    QVERIFY(texts(panel).contains(QLatin1String("text=first\nnopermission")));
}

void WhaleAiPanelTest::stopWithEscape()
{
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission waitforcancel")));
    QVERIFY(QTest::qWaitFor(
        [&] {
            return panel.transcript()->findChild<QWidget *>(QStringLiteral("toolCall-t1")) != nullptr;
        },
        10000));
    // One prompt at a time.
    QVERIFY(!panel.sendPrompt(QStringLiteral("more")));
    QTest::keyClick(panel.findChild<QPlainTextEdit *>(QStringLiteral("input")), Qt::Key_Escape);
    QVERIFY(waitForReady(panel));
    QCOMPARE(kinds(panel).last(), WhaleAiTranscript::Kind::Notice);
    QVERIFY(texts(panel).endsWith(QLatin1String("Stopped.")));
    QCOMPARE(panel.transcript()->findChild<QWidget *>(QStringLiteral("toolCall-t1"))->findChild<QLabel *>(QStringLiteral("status"))->property("status").toInt(),
             int(WhaleAcpToolCall::Status::Cancelled));
}

void WhaleAiPanelTest::agentFails()
{
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    panel.setAgentId(QStringLiteral("crash"));
    QVERIFY(panel.sendPrompt(QStringLiteral("boom")));
    QTRY_VERIFY_WITH_TIMEOUT(kinds(panel).contains(WhaleAiTranscript::Kind::Error), 10000);
    QVERIFY(!panel.session());
    QVERIFY(texts(panel).contains(QLatin1String("The agent stopped working.")));
    QVERIFY(texts(panel).contains(QLatin1String("the model exploded")));
    QVERIFY(!panel.findChild<QToolButton *>(QStringLiteral("send"))->property("busy").toBool());

    // The next prompt starts the agent again.
    QVERIFY(panel.sendPrompt(QStringLiteral("boom")));
    QVERIFY(panel.session());
    QTRY_COMPARE_WITH_TIMEOUT(kinds(panel).count(WhaleAiTranscript::Kind::Error), 2, 10000);

    // A program that does not exist.
    WhaleAiAgents::setAgents({{QStringLiteral("gone"), QStringLiteral("Gone"), QStringLiteral("orcinus-no-such-agent"), {}, {}}});
    panel.readSettings();
    QVERIFY(!panel.sendPrompt(QStringLiteral("hello")));
    QVERIFY(!panel.session());
    QVERIFY(texts(panel).contains(QLatin1String("Gone cannot be started")));
    QVERIFY(!texts(panel).contains(QLatin1String("hello")));
}

void WhaleAiPanelTest::otherAgentEndsConversation()
{
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(panel));
    QPointer<WhaleAcpSession> session = panel.session();
    panel.setAgentId(QStringLiteral("crash"));
    QVERIFY(!panel.session());
    QCOMPARE(session->state(), WhaleAcpSession::State::Closed);
    QVERIFY(texts(panel).endsWith(QLatin1String("Now talking to Fake crash. The next message starts a new conversation.")));
}

void WhaleAiPanelTest::newConversation()
{
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(panel));
    panel.findChild<QToolButton *>(QStringLiteral("newConversation"))->click();
    QVERIFY(!panel.session());
    QVERIFY(panel.transcript()->entries().isEmpty());
}

void WhaleAiPanelTest::removedAgentEndsConversation()
{
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(panel));
    WhaleAiAgents::setAgents({fakeAgent(QStringLiteral("crash"), QStringLiteral("crash"))});
    panel.readSettings();
    QVERIFY(!panel.session());
    QCOMPARE(panel.agentId(), QStringLiteral("crash"));
    QVERIFY(texts(panel).contains(QLatin1String("The agent of this conversation was removed.")));
}

void WhaleAiPanelTest::links()
{
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(panel));
    QSignalSpy activated(&panel, &WhaleAiPanel::localUrlActivated);
    Q_EMIT panel.transcript()->linkActivated(QStringLiteral("note.txt"));
    Q_EMIT panel.transcript()->linkActivated(QStringLiteral("/etc/hostname"));
    Q_EMIT panel.transcript()->linkActivated(QStringLiteral("file:///tmp"));
    QCOMPARE(activated.size(), 3);
    QCOMPARE(activated.at(0).first().toUrl(), QUrl::fromLocalFile(m_work.filePath(QStringLiteral("note.txt"))));
    QCOMPARE(activated.at(1).first().toUrl(), QUrl::fromLocalFile(QStringLiteral("/etc/hostname")));
    QCOMPARE(activated.at(2).first().toUrl(), QUrl(QStringLiteral("file:///tmp")));
}

void WhaleAiPanelTest::panelInMainWindow()
{
    TestDir dir;
    dir.createFile(QStringLiteral("a.txt"));
    DolphinMainWindow mainWindow;
    mainWindow.openDirectories({dir.url()}, false);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));

    QAction *action = mainWindow.actionCollection()->action(QStringLiteral("show_ai_panel"));
    QVERIFY(action);
    QCOMPARE(action->shortcut(), QKeySequence(Qt::Key_F8));
    auto *panels = qobject_cast<KActionMenu *>(mainWindow.actionCollection()->action(QStringLiteral("panels")));
    QVERIFY(panels->menu()->actions().contains(action));

    auto *dock = mainWindow.findChild<DolphinDockWidget *>(QStringLiteral("aiDock"));
    QVERIFY(dock);
    QVERIFY(!dock->isVisible());
    action->trigger();
    QVERIFY(dock->isVisible());
    auto *panel = dock->findChild<WhaleAiPanel *>();
    QVERIFY(panel);
    QTRY_COMPARE(panel->url(), dir.url());

    // A file in an answer: its folder with the file selected.
    Q_EMIT panel->localUrlActivated(QUrl::fromLocalFile(dir.path() + QStringLiteral("/a.txt")));
    QTRY_COMPARE(mainWindow.activeViewContainer()->view()->selectedItems().count(), 1);
}

QTEST_MAIN(WhaleAiPanelTest)

#include "whaleaipaneltest.moc"
