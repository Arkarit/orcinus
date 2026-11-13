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
#include "whaleaicontextchips.h"
#include "whaleaitranscript.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KSharedConfig>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <memory>

/**
 * The AI panel (AI3) against the scripted agent whaleacpfakeagent: sending, the transcript of a
 * turn, permission buttons, stopping, failures, choosing another agent, links, and the panel in
 * the main window. The start mode of AI5: read-only first, the stored mode, waiting for the user.
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

    void storedStartMode();
    void chosenModeIsStored();
    void waitsWithoutReadOnlyMode();
    void waitsWhenModeRefused();
    void waitsWhenStoredModeIsGone();
    void agentWithoutModes();
    void agentSwitchesMode();
    void stopWhileWaitingForMode();
    void permissionBar();
    void requestDetails();
    void requestFocus();
    void requestShowsHiddenPanel();
    void contextChips();
    void dropFiles();

private:
    static WhaleAiAgent fakeAgent(const QString &id, const QString &scenario);
    static QList<WhaleAiTranscript::Kind> kinds(WhaleAiPanel &panel);
    static QString texts(WhaleAiPanel &panel);
    static bool waitForReady(WhaleAiPanel &panel);
    static QWidget *waitForPermission(WhaleAiPanel &panel);
    static WhaleAiPanel *panelWith(const QString &scenario, const QString &startMode, const QString &workPath);

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
    // Start modes and known modes of earlier tests must not leak.
    KSharedConfig::openConfig()->deleteGroup(QStringLiteral("AI"));
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
            // A message that waits for the start mode is not done yet.
            return panel.session() && panel.session()->state() == WhaleAcpSession::State::Ready
                && !panel.findChild<QToolButton *>(QStringLiteral("send"))->property("busy").toBool();
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
    // Notice of the folder, the message, the mode, the thought, the answer, the tool call, the
    // request, and the answer after it as an entry of its own.
    QCOMPARE(
        kinds(panel),
        (QList<Kind>{Kind::Notice, Kind::UserMessage, Kind::Notice, Kind::Thought, Kind::AgentMessage, Kind::ToolCall, Kind::Permission, Kind::AgentMessage}));
    const QString all = texts(panel);
    // Without a stored start mode: the read-only one, before the message went out.
    QVERIFY2(all.contains(QStringLiteral("The agent works in the mode “Plan”.")), qPrintable(all));
    QVERIFY(WhaleAiAgents::agent(QStringLiteral("basic")).startMode.isEmpty());
    QVERIFY2(all.contains(QStringLiteral("Fake basic works in %1.").arg(m_work.path())), qPrintable(all));
    QVERIFY2(all.contains(QStringLiteral("cwd=%1;mcp=;text=hello;links=1").arg(m_work.path())), qPrintable(all));
    QVERIFY(all.contains(QLatin1String("permission=yes")));
    QVERIFY(all.contains(QLatin1String("Answered: Yes")));
    QVERIFY(!all.contains(QLatin1String("WRONG SESSION")));
    // The thought is folded.
    QVERIFY(!panel.transcript()->entries().at(3)->findChildren<QLabel *>().first()->isVisibleTo(panel.transcript()->entries().at(3)));

    QWidget *toolCall = panel.transcript()->findChild<QWidget *>(QStringLiteral("toolCall-t1"));
    QVERIFY(toolCall);
    QCOMPARE(toolCall->findChild<QLabel *>(QStringLiteral("title"))->text(), QStringLiteral("Write note.txt"));
    QCOMPARE(toolCall->findChild<QLabel *>(QStringLiteral("status"))->property("status").toInt(), int(WhaleAcpToolCall::Status::Completed));

    QVERIFY2(info->text().contains(QLatin1String("Model: Small")), qPrintable(info->text()));
    // The mode is in the bar, not in the info line.
    QVERIFY(!info->text().contains(QLatin1String("Mode:")));
    QCOMPARE(panel.findChild<QComboBox *>(QStringLiteral("modes"))->currentData().toString(), QStringLiteral("plan"));
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
    // The selection is context of the next message.
    QTRY_VERIFY(panel->findChild<QWidget *>(QStringLiteral("chip-selection")));
    QCOMPARE(panel->findChild<QWidget *>(QStringLiteral("chip-selection"))->findChild<QLabel *>(QStringLiteral("text"))->text(),
             QStringLiteral("Selected: a.txt"));
    QCOMPARE(panel->contextChips()->context().folder, dir.path());
    QCOMPARE(panel->contextChips()->context().selection, QStringList{dir.path() + QStringLiteral("/a.txt")});
    QVERIFY(panel->contextChips()->context().otherFolder.isEmpty());

    // The user gives a task and closes the panel; the agent asks: the panel comes back.
    QVERIFY(panel->sendPrompt(QStringLiteral("hello")));
    action->trigger();
    QVERIFY(!dock->isVisible());
    QVERIFY(waitForPermission(*panel));
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(action->isChecked());
    panel->findChild<QPushButton *>(QStringLiteral("option-no"))->click();
    QVERIFY(waitForReady(*panel));
}

WhaleAiPanel *WhaleAiPanelTest::panelWith(const QString &scenario, const QString &startMode, const QString &workPath)
{
    WhaleAiAgent agent = fakeAgent(scenario, scenario);
    agent.startMode = startMode;
    WhaleAiAgents::setAgents({agent});
    WhaleAiAgents::setDefaultAgentId(scenario);
    auto *panel = new WhaleAiPanel;
    panel->setUrl(QUrl::fromLocalFile(workPath));
    return panel;
}

void WhaleAiPanelTest::storedStartMode()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("basic"), QStringLiteral("ask"), m_work.path()));
    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(*panel));
    const QString all = texts(*panel);
    QVERIFY2(all.contains(QStringLiteral("The agent works in the mode “Ask”.")), qPrintable(all));
    QVERIFY(all.contains(QLatin1String("text=nopermission")));
    QCOMPARE(panel->session()->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("ask"));
    // The order: the mode is noted before the agent's answer.
    QVERIFY(all.indexOf(QStringLiteral("mode “Ask”")) < all.indexOf(QLatin1String("text=nopermission")));
}

void WhaleAiPanelTest::chosenModeIsStored()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("basic"), QString(), m_work.path()));
    // Before a conversation: only stored.
    panel->chooseMode(QStringLiteral("ask"));
    QCOMPARE(WhaleAiAgents::agent(QStringLiteral("basic")).startMode, QStringLiteral("ask"));
    QVERIFY(!panel->session());

    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(*panel));
    QCOMPARE(panel->session()->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("ask"));

    // In the conversation: switched, noted, stored.
    panel->chooseMode(QStringLiteral("auto"));
    QTRY_COMPARE(panel->session()->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("auto"));
    QTRY_VERIFY(texts(*panel).endsWith(QStringLiteral("You switched the agent to the mode “Auto”.")));
    QCOMPARE(WhaleAiAgents::agent(QStringLiteral("basic")).startMode, QStringLiteral("auto"));
    // A mode the agent does not offer is ignored.
    panel->chooseMode(QStringLiteral("nonsense"));
    QCOMPARE(WhaleAiAgents::agent(QStringLiteral("basic")).startMode, QStringLiteral("auto"));
}

void WhaleAiPanelTest::waitsWithoutReadOnlyMode()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("noreadonly"), QString(), m_work.path()));
    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QTRY_VERIFY(panel->isWaitingForMode());
    QString all = texts(*panel);
    QVERIFY2(all.contains(QLatin1String("Orcinus does not know which mode of Fake noreadonly only reads.")), qPrintable(all));
    QVERIFY(all.contains(QLatin1String("Your message is sent once the agent works in the chosen mode.")));
    QVERIFY(!all.contains(QLatin1String("text=")));
    QCOMPARE(kinds(*panel).last(), WhaleAiTranscript::Kind::Error);
    QCOMPARE(panel->session()->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("auto"));
    // One waiting message at a time; the send button does not stop.
    QVERIFY(!panel->sendPrompt(QStringLiteral("more")));
    QVERIFY(!panel->findChild<QToolButton *>(QStringLiteral("send"))->property("busy").toBool());

    // The bar asks for a choice instead of showing the agent's permissive mode.
    auto *modes = panel->findChild<QComboBox *>(QStringLiteral("modes"));
    QCOMPARE(modes->currentIndex(), -1);
    QCOMPARE(modes->placeholderText(), QStringLiteral("Choose a mode…"));
    QVERIFY(modes->isEnabled());
    QCOMPARE(modes->count(), 2);

    const int ask = modes->findData(QStringLiteral("ask"));
    modes->setCurrentIndex(ask);
    Q_EMIT modes->activated(ask);
    QVERIFY(waitForReady(*panel));
    QTRY_VERIFY(texts(*panel).contains(QLatin1String("text=nopermission")));
    QCOMPARE(modes->currentData().toString(), QStringLiteral("ask"));
    QVERIFY(!panel->isWaitingForMode());
    QCOMPARE(WhaleAiAgents::agent(QStringLiteral("noreadonly")).startMode, QStringLiteral("ask"));
}

void WhaleAiPanelTest::waitsWhenModeRefused()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("refusemode"), QString(), m_work.path()));
    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QTRY_VERIFY(panel->isWaitingForMode());
    const QString all = texts(*panel);
    QVERIFY2(all.contains(QStringLiteral("The agent did not switch to the mode “Plan”: mode is locked")), qPrintable(all));
    QVERIFY(!all.contains(QLatin1String("text=")));
}

void WhaleAiPanelTest::waitsWhenStoredModeIsGone()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("basic"), QStringLiteral("gone"), m_work.path()));
    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QTRY_VERIFY(panel->isWaitingForMode());
    const QString all = texts(*panel);
    QVERIFY2(all.contains(QStringLiteral("Fake basic does not offer the mode “gone” any more.")), qPrintable(all));
    QVERIFY(!all.contains(QLatin1String("text=")));
    // Not the read-only mode in its place: the user chose something else before.
    QCOMPARE(panel->session()->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("auto"));
}

void WhaleAiPanelTest::agentWithoutModes()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("nomode"), QString(), m_work.path()));
    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(*panel));
    const QString all = texts(*panel);
    QVERIFY2(all.contains(QLatin1String("Fake nomode offers no modes, so Orcinus cannot choose what it may do.")), qPrintable(all));
    QVERIFY(all.contains(QLatin1String("text=nopermission")));
}

void WhaleAiPanelTest::agentSwitchesMode()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("basic"), QString(), m_work.path()));
    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(*panel));
    // The fake agent moves the mode to "ask" when the model changes, as a real one may.
    panel->session()->setConfigOption(QStringLiteral("model"), QStringLiteral("big"));
    QTRY_VERIFY(texts(*panel).endsWith(QStringLiteral("The agent switched to the mode “Ask”.")));
    // Not the user's choice: not stored.
    QVERIFY(WhaleAiAgents::agent(QStringLiteral("basic")).startMode.isEmpty());
}

void WhaleAiPanelTest::stopWhileWaitingForMode()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("basic"), QString(), m_work.path()));
    auto *send = panel->findChild<QToolButton *>(QStringLiteral("send"));
    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(send->property("busy").toBool());
    send->click();
    QVERIFY(!panel->session());
    QVERIFY(texts(*panel).endsWith(QLatin1String("Stopped.")));
}

void WhaleAiPanelTest::permissionBar()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("basic"), QString(), m_work.path()));
    auto *modes = panel->findChild<QComboBox *>(QStringLiteral("modes"));
    QVERIFY(modes);
    // Nothing known about the agent yet: the rule.
    QCOMPARE(modes->count(), 0);
    QVERIFY(!modes->isEnabled());
    QCOMPARE(modes->placeholderText(), QStringLiteral("Read-only mode"));
    QVERIFY(modes->toolTip().contains(QLatin1String("Orcinus can only answer what it is asked")));

    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(*panel));
    QCOMPARE(modes->count(), 3);
    QVERIFY(modes->isEnabled());
    QCOMPARE(modes->currentData().toString(), QStringLiteral("plan"));
    QCOMPARE(modes->itemData(modes->findData(QStringLiteral("ask")), Qt::ToolTipRole).toString(), QStringLiteral("Always ask"));

    const int autoIndex = modes->findData(QStringLiteral("auto"));
    modes->setCurrentIndex(autoIndex);
    Q_EMIT modes->activated(autoIndex);
    QTRY_COMPARE(panel->session()->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("auto"));
    QCOMPARE(WhaleAiAgents::agent(QStringLiteral("basic")).startMode, QStringLiteral("auto"));
    QCOMPARE(modes->currentData().toString(), QStringLiteral("auto"));

    // The next conversation: the modes are known before it runs, the last choice is current.
    panel->newConversation();
    QVERIFY(!panel->session());
    QCOMPARE(modes->count(), 3);
    QVERIFY(modes->isEnabled());
    QCOMPARE(modes->currentData().toString(), QStringLiteral("auto"));
    // Choosing before it runs stores the start mode.
    const int plan = modes->findData(QStringLiteral("plan"));
    modes->setCurrentIndex(plan);
    Q_EMIT modes->activated(plan);
    QCOMPARE(WhaleAiAgents::agent(QStringLiteral("basic")).startMode, QStringLiteral("plan"));

    // A new panel (window) knows them too.
    std::unique_ptr<WhaleAiPanel> other(new WhaleAiPanel);
    auto *otherModes = other->findChild<QComboBox *>(QStringLiteral("modes"));
    QCOMPARE(otherModes->count(), 3);
    QCOMPARE(otherModes->currentData().toString(), QStringLiteral("plan"));
}

QWidget *WhaleAiPanelTest::waitForPermission(WhaleAiPanel &panel)
{
    QWidget *permission = nullptr;
    QTest::qWaitFor(
        [&] {
            permission = panel.transcript()->findChild<QWidget *>(QStringLiteral("permission-n7"));
            return permission != nullptr;
        },
        10000);
    return permission;
}

void WhaleAiPanelTest::requestDetails()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("basic"), QString(), m_work.path()));
    // An edit: kind, path, the agent's input to unfold.
    QVERIFY(panel->sendPrompt(QStringLiteral("hello")));
    QWidget *permission = waitForPermission(*panel);
    QVERIFY(permission);
    QCOMPARE(permission->findChild<QLabel *>(QStringLiteral("kind"))->text(), QStringLiteral("Change files"));
    QCOMPARE(permission->findChild<QLabel *>(QStringLiteral("paths"))->text(), m_work.path() + QStringLiteral("/note.txt"));
    QVERIFY(!permission->findChild<QLabel *>(QStringLiteral("command")));
    QWidget *details = permission->findChild<QWidget *>(QStringLiteral("details"));
    QVERIFY(details);
    QVERIFY(details->findChild<QLabel *>()->text().contains(QLatin1String("\"content\": \"hi\"")));
    permission->findChild<QPushButton *>(QStringLiteral("option-no"))->click();
    QVERIFY(waitForReady(*panel));

    // A command: in sight, not folded.
    QVERIFY(panel->sendPrompt(QStringLiteral("askexecute")));
    permission = nullptr;
    QVERIFY(QTest::qWaitFor(
        [&] {
            const auto frames = panel->transcript()->findChildren<QWidget *>(QStringLiteral("permission-n7"));
            permission = frames.size() == 2 ? frames.last() : nullptr;
            return permission != nullptr;
        },
        10000));
    QCOMPARE(permission->findChild<QLabel *>(QStringLiteral("kind"))->text(), QStringLiteral("Run a command"));
    QLabel *command = permission->findChild<QLabel *>(QStringLiteral("command"));
    QVERIFY(command);
    QCOMPARE(command->text(), QStringLiteral("rm -r out"));
    QVERIFY(command->isVisibleTo(permission));
    QVERIFY(texts(*panel).contains(QLatin1String("The agent asks for permission: Remove out")));
    permission->findChild<QPushButton *>(QStringLiteral("option-no"))->click();
    QVERIFY(waitForReady(*panel));

    // A change: the diff to unfold.
    QVERIFY(panel->sendPrompt(QStringLiteral("askdiff")));
    QVERIFY(QTest::qWaitFor(
        [&] {
            const auto frames = panel->transcript()->findChildren<QWidget *>(QStringLiteral("permission-n7"));
            permission = frames.size() == 3 ? frames.last() : nullptr;
            return permission != nullptr;
        },
        10000));
    QWidget *changes = permission->findChild<QWidget *>(QStringLiteral("changes"));
    QVERIFY(changes);
    QVERIFY(!permission->findChild<QWidget *>(QStringLiteral("details")));
    QLabel *diff = changes->findChild<QLabel *>();
    QVERIFY(!diff->isVisibleTo(changes));
    QCOMPARE(diff->text(), QStringLiteral("%1/note.txt\n- old line\n+ new line\n+ second").arg(m_work.path()));
    changes->findChild<QToolButton *>(QStringLiteral("toggle"))->click();
    QVERIFY(diff->isVisibleTo(changes));
    permission->findChild<QPushButton *>(QStringLiteral("option-yes"))->click();
    QVERIFY(waitForReady(*panel));
}

void WhaleAiPanelTest::requestFocus()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("basic"), QString(), m_work.path()));
    panel->show();
    QVERIFY(QTest::qWaitForWindowActive(panel.get()));
    auto *input = panel->findChild<QPlainTextEdit *>(QStringLiteral("input"));
    input->setFocus();
    QTest::keyClicks(input, QStringLiteral("hello"));
    QTest::keyClick(input, Qt::Key_Return);
    QWidget *permission = waitForPermission(*panel);
    QVERIFY(permission);
    // The first "allow" option: Space answers it.
    auto *yes = permission->findChild<QPushButton *>(QStringLiteral("option-yes"));
    QTRY_VERIFY(yes->hasFocus());
    QTest::keyClick(yes, Qt::Key_Space);
    QVERIFY(waitForReady(*panel));
    QVERIFY(texts(*panel).contains(QLatin1String("permission=yes")));
}

void WhaleAiPanelTest::requestShowsHiddenPanel()
{
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("basic"), QString(), m_work.path()));
    QSignalSpy attention(panel.get(), &WhaleAiPanel::attentionRequested);
    // Never shown, like a closed dock.
    QVERIFY(!panel->isVisible());
    QVERIFY(panel->sendPrompt(QStringLiteral("hello")));
    QVERIFY(waitForPermission(*panel));
    QCOMPARE(attention.count(), 1);
}

static QString chipText(WhaleAiPanel &panel, const QString &name)
{
    auto *chip = panel.contextChips()->findChild<QWidget *>(name);
    return chip ? chip->findChild<QLabel *>(QStringLiteral("text"))->text() : QString();
}

void WhaleAiPanelTest::contextChips()
{
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("a/sub")));
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("b")));
    const QString a = dir.filePath(QStringLiteral("a"));
    const QString b = dir.filePath(QStringLiteral("b"));
    WhaleAiContext window{a, {a + QStringLiteral("/x.jpg"), a + QStringLiteral("/y.jpg")}, b, {}};

    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("basic"), QString(), m_work.path()));
    panel->setContextProvider([&window] {
        return window;
    });
    QVERIFY(panel->contextChips()->isVisibleTo(panel.get()));
    QCOMPARE(chipText(*panel, QStringLiteral("chip-folder")), QStringLiteral("Folder: a"));
    QCOMPARE(chipText(*panel, QStringLiteral("chip-selection")), QStringLiteral("Selected: 2 items"));
    QCOMPARE(chipText(*panel, QStringLiteral("chip-other")), QStringLiteral("Other view: b"));
    QCOMPARE(panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-selection"))->toolTip(),
             a + QStringLiteral("/x.jpg\n") + a + QStringLiteral("/y.jpg"));

    // Removed for the next message.
    panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-selection"))->findChild<QToolButton *>(QStringLiteral("remove"))->click();
    QTRY_VERIFY(!panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-selection")));
    QVERIFY(panel->contextChips()->context().selection.isEmpty());

    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(*panel));
    const QString all = texts(*panel);
    QVERIFY2(all.contains(QStringLiteral("With: folder “a”, other view “b”")), qPrintable(all));
    QVERIFY(all.contains(QStringLiteral("text=nopermission;links=2;context=Context from the file manager Orcinus")));
    QVERIFY(all.contains(QStringLiteral("- Current folder: %1 (subfolders: sub)").arg(a)));
    QVERIFY(!all.contains(QLatin1String("x.jpg")));
    // Back for the message after.
    QCOMPARE(chipText(*panel, QStringLiteral("chip-selection")), QStringLiteral("Selected: 2 items"));

    // A removed chip comes back when its content changes.
    panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-folder"))->findChild<QToolButton *>(QStringLiteral("remove"))->click();
    QTRY_VERIFY(!panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-folder")));
    panel->refreshContext();
    QVERIFY(!panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-folder")));
    window.folder = b;
    window.selection.clear();
    window.otherFolder.clear();
    panel->refreshContext();
    QCOMPARE(chipText(*panel, QStringLiteral("chip-folder")), QStringLiteral("Folder: b"));
    QVERIFY(!panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-selection")));
    QVERIFY(!panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-other")));

    // Nothing to show: no row.
    window = {};
    panel->refreshContext();
    QVERIFY(!panel->contextChips()->isVisibleTo(panel.get()));
}

void WhaleAiPanelTest::dropFiles()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("notes.md"));
    QFile(file).open(QIODevice::WriteOnly);
    std::unique_ptr<WhaleAiPanel> panel(panelWith(QStringLiteral("basic"), QString(), m_work.path()));
    auto *input = panel->findChild<QPlainTextEdit *>(QStringLiteral("input"));

    auto drop = [&](const QList<QUrl> &urls) {
        QMimeData mime;
        mime.setUrls(urls);
        QDragEnterEvent enter(QPoint(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(input->viewport(), &enter);
        if (!enter.isAccepted()) {
            return false;
        }
        QDropEvent dropEvent(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(input->viewport(), &dropEvent);
        return dropEvent.isAccepted();
    };
    QVERIFY(drop({QUrl::fromLocalFile(file), QUrl::fromLocalFile(dir.path())}));
    // Chips, not text.
    QVERIFY(input->toPlainText().isEmpty());
    const auto added = panel->contextChips()->findChildren<QWidget *>(QStringLiteral("chip-added"));
    QCOMPARE(added.size(), 2);
    QCOMPARE(added.first()->findChild<QLabel *>(QStringLiteral("text"))->text(), QStringLiteral("notes.md"));
    QCOMPARE(panel->contextChips()->context().added, (QStringList{file, dir.path()}));
    // The same again is not added twice.
    QVERIFY(drop({QUrl::fromLocalFile(file)}));
    QCOMPARE(panel->contextChips()->context().added.size(), 2);

    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(*panel));
    const QString all = texts(*panel);
    QVERIFY2(all.contains(QStringLiteral("2 added items")), qPrintable(all));
    QVERIFY(all.contains(QStringLiteral("- Added by the user for this message (2): %1, %2").arg(file, dir.path())));
    // Used once.
    QVERIFY(panel->contextChips()->findChildren<QWidget *>(QStringLiteral("chip-added")).isEmpty());
    QVERIFY(panel->contextChips()->context().added.isEmpty());
}

QTEST_MAIN(WhaleAiPanelTest)

#include "whaleaipaneltest.moc"
