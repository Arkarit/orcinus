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
#include "whaleaiconversationsdialog.h"
#include "whaleaiconversationstore.h"
#include "whaleaifakelmstudio.h"
#include "whaleaifakestrata.h"
#include "whaleailocalruntime.h"
#include "whaleaimcpserver.h"
#include "whaleaimemory.h"
#include "whaleaisettingspage.h"
#include "whaleaitranscript.h"

#include <KAboutData>
#include <KActionCollection>
#include <KActionMenu>
#include <KMessageWidget>
#include <KSharedConfig>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

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

    void disclaimer();
    void setupWithoutAgents();
    void promptTurn();
    void enterAndShiftEnter();
    void stopWithEscape();
    void agentFails();
    void otherAgentEndsConversation();
    void newConversation();
    void savedConversation();
    void openSavedConversation();
    void resumeSavedConversation();
    void memoryNotesSent();
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
    void elideChipText();

    void backChannel();
    void agentTools();
    void localModel();
    void localModelFails();
    void localModelSwitched();
    void strataKeptAfterClosing();
    void inputKeepsFocus();

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
    // Accepted: only disclaimer() is about it.
    WhaleAiAgents::setDisclaimerAccepted(true);
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

void WhaleAiPanelTest::disclaimer()
{
    // Over the panel and on the settings page until it is accepted; nothing goes out before.
    WhaleAiAgents::setDisclaimerAccepted(false);
    WhaleAiPanel panel;
    auto *stack = panel.findChild<QStackedWidget *>();
    auto *shown = panel.findChild<QWidget *>(QStringLiteral("aiDisclaimer"));
    QVERIFY(shown);
    QCOMPARE(stack->currentWidget(), shown);
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    QVERIFY(!panel.sendPrompt(QStringLiteral("hello")));
    QVERIFY(!panel.session());
    WhaleAiSettingsPage page;
    auto *pages = page.findChild<QStackedWidget *>();
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("aiDisclaimer"));

    // The last part is about the warranty and liability of the GPL 2; the license is one click away.
    const QString text = shown->findChild<QLabel *>(QStringLiteral("text"))->text();
    QVERIFY(text.contains(QLatin1String("Your system, your responsibility.")));
    QVERIFY(text.contains(QLatin1String("sections 11 and 12")));
    // With the application's license, as in Orcinus (its text refers to the KAboutData).
    KAboutData about(QStringLiteral("whaleaipaneltest"), QStringLiteral("Test"), QStringLiteral("1.0"), QString(), KAboutLicense::GPL);
    KAboutData::setApplicationData(about);
    shown->findChild<QToolButton *>(QStringLiteral("viewLicense"))->click();
    auto *licenseDialog = panel.findChild<QDialog *>(QStringLiteral("licenseDialog"));
    QVERIFY(licenseDialog);
    QVERIFY(licenseDialog->findChild<QPlainTextEdit *>()->toPlainText().contains(QLatin1String("GNU GENERAL PUBLIC LICENSE")));
    licenseDialog->close();

    // Continue only once the user says they understand; accepted in the panel, gone in both.
    auto *understood = shown->findChild<QCheckBox *>(QStringLiteral("understood"));
    auto *proceed = shown->findChild<QPushButton *>(QStringLiteral("continue"));
    QVERIFY(!proceed->isEnabled());
    understood->click();
    QVERIFY(proceed->isEnabled());
    proceed->click();
    QVERIFY(WhaleAiAgents::disclaimerAccepted());
    QCOMPARE(stack->currentIndex(), 0);
    QVERIFY(pages->currentWidget()->objectName() != QStringLiteral("aiDisclaimer"));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(panel));

    // Not again, in a new panel or page.
    WhaleAiPanel next;
    QCOMPARE(next.findChild<QStackedWidget *>()->currentIndex(), 0);
    WhaleAiSettingsPage nextPage;
    QVERIFY(nextPage.findChild<QStackedWidget *>()->currentWidget()->objectName() != QStringLiteral("aiDisclaimer"));

    // Accepted on the settings page: the panel follows.
    WhaleAiAgents::setDisclaimerAccepted(false);
    WhaleAiPanel third;
    WhaleAiSettingsPage thirdPage;
    QCOMPARE(third.findChild<QStackedWidget *>()->currentWidget()->objectName(), QStringLiteral("aiDisclaimer"));
    auto *onPage = thirdPage.findChild<QWidget *>(QStringLiteral("aiDisclaimer"));
    onPage->findChild<QCheckBox *>(QStringLiteral("understood"))->click();
    onPage->findChild<QPushButton *>(QStringLiteral("continue"))->click();
    QCOMPARE(third.findChild<QStackedWidget *>()->currentIndex(), 0);
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
    // The chat has its own way to the settings.
    panel.findChild<QToolButton *>(QStringLiteral("configure"))->click();
    QCOMPARE(configure.size(), 2);
}

void WhaleAiPanelTest::savedConversation()
{
    const WhaleAiConversationStore store;
    const qsizetype before = store.list().size();
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    QVERIFY(panel.conversationId().isEmpty());
    QVERIFY(panel.sendPrompt(QStringLiteral("hello")));
    QWidget *permission = waitForPermission(panel);
    QVERIFY(permission);
    permission->findChild<QPushButton *>(QStringLiteral("option-yes"))->click();
    QVERIFY(waitForReady(panel));

    // Saved at the end of the turn, without the greeting before it.
    const QString id = panel.conversationId();
    QVERIFY(!id.isEmpty());
    std::optional<WhaleAiConversation> saved = store.load(id);
    QVERIFY(saved);
    QCOMPARE(saved->title, QStringLiteral("hello"));
    QCOMPARE(saved->agentId, QStringLiteral("basic"));
    QCOMPARE(saved->agentName, QStringLiteral("Fake basic"));
    QCOMPARE(saved->folder, QDir::cleanPath(m_work.path()));
    QVERIFY(!saved->sessionId.isEmpty());
    QCOMPARE(saved->sessionId, panel.session()->sessionId());
    QCOMPARE(saved->entries, panel.transcript()->record(1));
    QStringList kinds;
    for (const QJsonValue &entry : std::as_const(saved->entries)) {
        kinds.append(entry.toObject().value(QLatin1String("kind")).toString());
    }
    QCOMPARE(kinds.first(), QStringLiteral("Notice"));
    QVERIFY(kinds.contains(QStringLiteral("UserMessage")));
    QVERIFY(kinds.contains(QStringLiteral("Thought")));
    QVERIFY(kinds.contains(QStringLiteral("AgentMessage")));
    QVERIFY(kinds.contains(QStringLiteral("ToolCall")));
    const auto request = std::find_if(saved->entries.begin(), saved->entries.end(), [](const QJsonValue &entry) {
        return entry.toObject().value(QLatin1String("kind")).toString() == QLatin1String("Permission");
    });
    QVERIFY(request != saved->entries.end());
    QVERIFY(!(*request).toObject().value(QLatin1String("answer")).toString().isEmpty());

    // Built again from it: the same entries, with their old times.
    WhaleAiTranscript restored;
    restored.restore(saved->entries);
    QCOMPARE(restored.record(), saved->entries);
    QCOMPARE(WhaleAiTranscript::timeOf(restored.entries().constFirst()), WhaleAiTranscript::timeOf(panel.transcript()->entries().at(1)));
    QVERIFY(!restored.hasOpenPermissionRequest());

    // A new conversation: the old one stays saved, the next is another one.
    panel.newConversation();
    QVERIFY(panel.conversationId().isEmpty());
    QVERIFY(store.load(id));
    QCOMPARE(store.list().size(), before + 1);

    // Not saved when the user does not want it.
    WhaleAiAgents::setSaveConversations(false);
    QVERIFY(panel.sendPrompt(QStringLiteral("hello again")));
    permission = waitForPermission(panel);
    QVERIFY(permission);
    permission->findChild<QPushButton *>(QStringLiteral("option-yes"))->click();
    QVERIFY(waitForReady(panel));
    QVERIFY(!store.load(panel.conversationId()));
    QCOMPARE(store.list().size(), before + 1);
}

void WhaleAiPanelTest::openSavedConversation()
{
    const WhaleAiConversationStore store;
    for (const WhaleAiConversation &conversation : store.list()) {
        store.remove(conversation.id);
    }
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    auto *button = panel.findChild<QToolButton *>(QStringLiteral("conversations"));
    QVERIFY(button);
    auto *menu = panel.findChild<QMenu *>(QStringLiteral("conversationsMenu"));
    QVERIFY(menu);
    auto action = [menu](const QString &name) {
        Q_EMIT menu->aboutToShow();
        return menu->findChild<QAction *>(name);
    };
    Q_EMIT menu->aboutToShow();
    QCOMPARE(menu->actions().first()->text(), QStringLiteral("No Saved Conversations"));
    QVERIFY(!menu->actions().first()->isEnabled());
    // Nothing shown: nothing to export (AI15).
    QVERIFY(!action(QStringLiteral("exportPdf"))->isEnabled());
    QVERIFY(!action(QStringLiteral("exportMarkdown"))->isEnabled());

    // One saved; not to be opened while the agent works.
    QVERIFY(panel.sendPrompt(QStringLiteral("hello")));
    QWidget *permission = waitForPermission(panel);
    QVERIFY(permission);
    QVERIFY(!button->isEnabled());
    QVERIFY(!panel.openConversation(panel.conversationId()));
    permission->findChild<QPushButton *>(QStringLiteral("option-yes"))->click();
    QVERIFY(waitForReady(panel));
    QVERIFY(button->isEnabled());
    const QString id = panel.conversationId();
    QVERIFY(action(QStringLiteral("exportPdf"))->isEnabled());
    QVERIFY(action(QStringLiteral("exportMarkdown"))->isEnabled());
    QCOMPARE(panel.shownConversation()->title, QStringLiteral("hello"));
    QCOMPARE(panel.shownConversation()->entries, store.load(id)->entries);
    // The one shown cannot be opened again.
    QAction *entry = action(QStringLiteral("conversation-") + id);
    QVERIFY(entry);
    QVERIFY(entry->text().startsWith(QLatin1String("hello\t")));
    QVERIFY(!entry->isEnabled());
    QVERIFY(entry->isChecked());

    // Another agent was chosen meanwhile; opening it brings its agent back.
    panel.newConversation();
    panel.setAgentId(QStringLiteral("crash"));
    entry = action(QStringLiteral("conversation-") + id);
    QVERIFY(entry && entry->isEnabled());
    entry->trigger();
    QCOMPARE(panel.openedConversationId(), id);
    QCOMPARE(panel.agentId(), QStringLiteral("basic"));
    QVERIFY(!panel.session());
    const QJsonArray saved = store.load(id)->entries;
    QCOMPARE(panel.transcript()->record(0, saved.size()), saved);
    QVERIFY(texts(panel).contains(QLatin1String("Saved conversation of")));
    QVERIFY(!action(QStringLiteral("conversation-") + id)->isEnabled());

    // A new conversation instead (resuming is resumeSavedConversation); the opened one stays as it was.
    panel.newConversation();
    QVERIFY(panel.openedConversationId().isEmpty());
    QVERIFY(panel.sendPrompt(QStringLiteral("next")));
    permission = waitForPermission(panel);
    QVERIFY(permission);
    permission->findChild<QPushButton *>(QStringLiteral("option-yes"))->click();
    QVERIFY(waitForReady(panel));
    QVERIFY(panel.openedConversationId().isEmpty());
    QVERIFY(panel.conversationId() != id);
    QCOMPARE(store.load(id)->entries, saved);
    QCOMPARE(store.load(panel.conversationId())->title, QStringLiteral("next"));
    QCOMPARE(store.list().size(), 2);

    // Show All: search, delete; the open one cannot be deleted. (Shown before the check runs.)
    bool checked = false;
    QTimer::singleShot(0, menu->findChild<QAction *>(QStringLiteral("showAllConversations")), &QAction::trigger);
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<WhaleAiConversationsDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto close = qScopeGuard([dialog] {
            dialog->reject();
        });
        auto *list = dialog->findChild<QTreeWidget *>(QStringLiteral("conversations"));
        auto *search = dialog->findChild<QLineEdit *>(QStringLiteral("search"));
        auto *open = dialog->findChild<QPushButton *>(QStringLiteral("open"));
        auto *remove = dialog->findChild<QPushButton *>(QStringLiteral("remove"));
        QCOMPARE(list->topLevelItemCount(), 2);
        // Newest first: the open one.
        QVERIFY(list->currentItem()->text(0).startsWith(QLatin1String("next")));
        QVERIFY(!open->isEnabled());
        QVERIFY(!remove->isEnabled());
        // The open one can be exported (AI15).
        QVERIFY(dialog->findChild<QPushButton *>(QStringLiteral("exportPdf"))->isEnabled());
        QVERIFY(dialog->findChild<QPushButton *>(QStringLiteral("exportMarkdown"))->isEnabled());
        // What was written is searched, too: the fake agent's answer.
        search->setText(QStringLiteral("no such words"));
        QVERIFY(!list->currentItem());
        QVERIFY(!open->isEnabled());
        search->setText(QStringLiteral("HELLO"));
        QCOMPARE(list->currentItem()->data(0, Qt::UserRole).toString(), id);
        QVERIFY(open->isEnabled());
        remove->click();
        QCOMPARE(list->topLevelItemCount(), 1);
        checked = true;
    });
    QTRY_VERIFY(checked);
    QVERIFY(!store.load(id));
    QCOMPARE(store.list().size(), 1);
}

void WhaleAiPanelTest::resumeSavedConversation()
{
    const WhaleAiConversationStore store;
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    auto turn = [&panel](const QString &text) {
        QVERIFY(panel.sendPrompt(text));
        QVERIFY(waitForReady(panel));
    };
    turn(QStringLiteral("nopermission hello"));
    const QString id = panel.conversationId();
    panel.chooseMode(QStringLiteral("ask"));
    QTRY_COMPARE(panel.session()->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("ask"));
    panel.newConversation();
    QCOMPARE(store.load(id)->mode, QStringLiteral("ask"));

    // Opened from elsewhere: the agent continues it where it worked, in its mode.
    QTemporaryDir elsewhere;
    panel.setUrl(QUrl::fromLocalFile(elsewhere.path()));
    QVERIFY(panel.openConversation(id));
    QVERIFY(panel.canResumeOpened());
    QVERIFY(texts(panel).contains(QLatin1String("The next message continues it.")));
    turn(QStringLiteral("nopermission more"));
    QVERIFY(panel.session()->isResumed());
    QCOMPARE(panel.session()->property("workingDirectory").toString(), QDir::cleanPath(m_work.path()));
    QCOMPARE(panel.session()->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("ask"));
    QString all = texts(panel);
    QVERIFY2(all.contains(QLatin1String("resumed=yes;")), qPrintable(all));
    QVERIFY(!all.contains(QLatin1String("replayed history")));
    QVERIFY(all.contains(QLatin1String("continues the conversation in")));
    // The same conversation, longer; no system prompt again (the fake echoes what it got).
    QCOMPARE(panel.conversationId(), id);
    std::optional<WhaleAiConversation> saved = store.load(id);
    QCOMPARE(saved->title, QStringLiteral("nopermission hello"));
    QCOMPARE(saved->entries, panel.transcript()->record());
    QStringList users;
    for (const QJsonValue &entry : std::as_const(saved->entries)) {
        if (entry.toObject().value(QLatin1String("kind")).toString() == QLatin1String("UserMessage")) {
            users.append(entry.toObject().value(QLatin1String("text")).toString());
        }
    }
    QCOMPARE(users, (QStringList{QStringLiteral("nopermission hello"), QStringLiteral("nopermission more")}));
    const qsizetype count = store.list().size();

    // The agent forgot the session: a new conversation, the saved one stays as it was.
    panel.newConversation();
    saved = store.load(id);
    saved->sessionId = QStringLiteral("gone");
    QVERIFY(store.save(*saved));
    QVERIFY(panel.openConversation(id));
    turn(QStringLiteral("nopermission once more"));
    QVERIFY(!panel.session()->isResumed());
    QVERIFY(texts(panel).contains(QLatin1String("The agent could not continue the saved conversation, so this is a new one.")));
    QVERIFY(panel.conversationId() != id);
    QCOMPARE(store.load(id)->entries, saved->entries);
    QCOMPARE(store.load(panel.conversationId())->title, QStringLiteral("nopermission once more"));
    QCOMPARE(store.list().size(), count + 1);

    // Another agent chosen: no resume.
    panel.newConversation();
    saved->sessionId = QStringLiteral("s1");
    QVERIFY(store.save(*saved));
    QVERIFY(panel.openConversation(id));
    panel.setAgentId(QStringLiteral("crash"));
    QVERIFY(!panel.canResumeOpened());
    QVERIFY(panel.openedConversationId().isEmpty());
    QVERIFY(texts(panel).contains(QLatin1String("Now talking to Fake crash. The next message starts a new conversation.")));
}

void WhaleAiPanelTest::memoryNotesSent()
{
    const WhaleAiMemory memory;
    QFile::remove(memory.path());
    const QString sub = m_work.filePath(QStringLiteral("notes-sub"));
    QVERIFY(QDir().mkpath(sub));
    WhaleAiAgents::setMemoryMode(WhaleAiAgents::MemoryMode::Manual);
    QVERIFY(memory.add(QStringLiteral("global note G"), QString(), QString(), false));
    QVERIFY(memory.add(QStringLiteral("work note W"), m_work.path(), QString(), false));
    QVERIFY(memory.add(QStringLiteral("elsewhere note E"), QStringLiteral("/elsewhere"), QString(), false));

    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    auto turn = [&panel](const QString &text) {
        QVERIFY(panel.sendPrompt(text));
        QVERIFY(waitForReady(panel));
    };
    auto count = [&panel](const QString &text) {
        return texts(panel).count(text);
    };
    // The first message: the notes for here and the global ones, after the system prompt.
    turn(QStringLiteral("nopermission one"));
    QString all = texts(panel);
    QCOMPARE(count(QStringLiteral("Notes the user keeps for you")), 1);
    QVERIFY2(all.contains(QLatin1String("work note W")), qPrintable(all));
    QVERIFY(all.contains(QLatin1String("global note G")));
    QVERIFY(all.contains(QLatin1String("[everywhere, note")));
    QVERIFY(!all.contains(QLatin1String("elsewhere note E")));
    QVERIFY(all.indexOf(QLatin1String("Notes the user keeps")) > all.indexOf(QLatin1String("Orcinus")));
    QVERIFY(all.contains(QLatin1String("2 notes from the memory")));

    // The next message: nothing again.
    turn(QStringLiteral("nopermission two"));
    QCOMPARE(count(QStringLiteral("Notes the user keeps for you")), 1);

    // Into a subfolder with a note of its own: only that one.
    QVERIFY(memory.add(QStringLiteral("sub note S"), sub, QString(), false));
    panel.setUrl(QUrl::fromLocalFile(sub));
    turn(QStringLiteral("nopermission three"));
    QCOMPARE(count(QStringLiteral("Notes the user keeps for you")), 2);
    QCOMPARE(count(QStringLiteral("sub note S")), 1);
    QCOMPARE(count(QStringLiteral("work note W")), 1);
    QVERIFY(texts(panel).contains(QLatin1String("1 note from the memory")));

    // A changed note goes again.
    const QString id = memory.relevant({sub}, 2000).first().id;
    QVERIFY(memory.update(id, QStringLiteral("sub note S2"), sub));
    turn(QStringLiteral("nopermission four"));
    QCOMPARE(count(QStringLiteral("sub note S2")), 1);

    // Memory off: no notes at all.
    WhaleAiAgents::setMemoryMode(WhaleAiAgents::MemoryMode::Off);
    panel.newConversation();
    turn(QStringLiteral("nopermission five"));
    QCOMPARE(count(QStringLiteral("Notes the user keeps for you")), 0);
    QFile::remove(memory.path());
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
    // AI13f: no spinner while the user is asked; it comes back while the agent goes on.
    auto *busy = panel.findChild<QWidget *>(QStringLiteral("busy"));
    QVERIFY(busy);
    QVERIFY(busy->isHidden());
    permission->findChild<QPushButton *>(QStringLiteral("option-yes"))->click();
    QVERIFY(waitForReady(panel));
    QVERIFY(!send->property("busy").toBool());
    QVERIFY(busy->isHidden());
    // No local model: no start/unload button.
    QVERIFY(panel.findChild<QToolButton *>(QStringLiteral("localModelButton"))->isHidden());

    using Kind = WhaleAiTranscript::Kind;
    // Orcinus' greeting, notice of the folder, the message, the mode, the thought, the answer, the
    // tool call, the request, and the answer after it as an entry of its own.
    QCOMPARE(kinds(panel),
             (QList<Kind>{Kind::Notice,
                          Kind::Notice,
                          Kind::UserMessage,
                          Kind::Notice,
                          Kind::Thought,
                          Kind::AgentMessage,
                          Kind::ToolCall,
                          Kind::Permission,
                          Kind::AgentMessage}));
    const QString all = texts(panel);
    // Without a stored start mode: the read-only one, before the message went out.
    QVERIFY2(all.contains(QStringLiteral("The agent works in the mode “Plan”.")), qPrintable(all));
    QVERIFY(WhaleAiAgents::agent(QStringLiteral("basic")).startMode.isEmpty());
    QVERIFY2(all.contains(QStringLiteral("Fake basic works in %1.").arg(m_work.path())), qPrintable(all));
    QVERIFY2(all.contains(QStringLiteral("cwd=%1;mcp=orcinus:%2;text=hello;links=1").arg(m_work.path(), WhaleAiMcpServer::relay())), qPrintable(all));
    QVERIFY(all.contains(QLatin1String("permission=yes")));
    QVERIFY(all.contains(QLatin1String("Answered: Yes")));
    QVERIFY(!all.contains(QLatin1String("WRONG SESSION")));
    // The thought is folded.
    QVERIFY(!panel.transcript()->entries().at(4)->findChildren<QLabel *>().first()->isVisibleTo(panel.transcript()->entries().at(4)));

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
    QVERIFY(texts(panel).contains(QStringLiteral("cwd=%1;mcp=orcinus:%2;text=nopermission again").arg(m_work.path(), WhaleAiMcpServer::relay())));

    // The system prompt leads the first message of the conversation, once: the agent echoes the
    // briefing before the first message, and nothing of it before the second.
    QCOMPARE(texts(panel).count(QLatin1String("working with the user's files through the Orcinus file manager")), 1);
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
    // Only Orcinus' own greeting is left, as in a fresh panel; the agent is not started for it.
    QCOMPARE(kinds(panel), QList<WhaleAiTranscript::Kind>{WhaleAiTranscript::Kind::Notice});
    QVERIFY(texts(panel).startsWith(QLatin1String("Ask the agent about your files.")));
    WhaleAiPanel fresh;
    QCOMPARE(texts(fresh), texts(panel));
    QVERIFY(!fresh.session());
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
    // The selection is context of the next message (without a chip: the view shows it).
    QTRY_COMPARE(panel->contextChips()->context().selection, QStringList{dir.path() + QStringLiteral("/a.txt")});
    QVERIFY(!panel->findChild<QWidget *>(QStringLiteral("chip-selection")));
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

    // The back channel (AI6c): the agent opens a folder and selects in it at once, while the
    // view still loads it.
    dir.createFile(QStringLiteral("sub/b.txt"));
    dir.createFile(QStringLiteral("sub/c.txt"));
    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission mcp:set_location:%1/sub mcp:select_items:b.txt,c.txt").arg(dir.path())));
    QVERIFY(waitForReady(*panel));
    DolphinView *view = mainWindow.activeViewContainer()->view();
    QCOMPARE(view->url(), QUrl::fromLocalFile(dir.path() + QStringLiteral("/sub")));
    QTRY_COMPARE(view->selectedItems().count(), 2);
    QCOMPARE(view->selectedItems().urlList().contains(QUrl::fromLocalFile(dir.path() + QStringLiteral("/sub/b.txt"))), true);
    // In a loaded folder, the selection is replaced.
    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission mcp:select_items:c.txt")));
    QVERIFY(waitForReady(*panel));
    QTRY_COMPARE(view->selectedItems().count(), 1);
    QCOMPARE(view->selectedItems().first().url(), QUrl::fromLocalFile(dir.path() + QStringLiteral("/sub/c.txt")));
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
    // The options one below the other, a long one elided: they never widen the panel.
    panel->resize(300, 600);
    panel->show();
    QVERIFY(QTest::qWaitForWindowExposed(panel.get()));
    const auto options = permission->findChild<QWidget *>(QStringLiteral("buttons"))->findChildren<QPushButton *>();
    QCOMPARE(options.size(), 3);
    QVERIFY(options.at(1)->y() > options.at(0)->y());
    QVERIFY(options.at(1)->width() < 300);
    QVERIFY(options.at(1)->toolTip().startsWith(QLatin1String("Always, and do not ask again")));
    QVERIFY(options.at(0)->toolTip().isEmpty());
    QVERIFY(permission->width() <= 300);
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
    // The folder shown needs no chip: the user sees it. It still goes with the message.
    QVERIFY(!panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-folder")));
    QCOMPARE(panel->contextChips()->context().folder, a);
    QCOMPARE(chipText(*panel, QStringLiteral("chip-other")), QStringLiteral("Other view: b"));
    // The selection has no chip (the view shows it) but goes along.
    QVERIFY(!panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-selection")));
    QCOMPARE(panel->contextChips()->context().selection.size(), 2);

    // Removed for the next message.
    panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-other"))->findChild<QToolButton *>(QStringLiteral("remove"))->click();
    QTRY_VERIFY(!panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-other")));
    QVERIFY(panel->contextChips()->context().otherFolder.isEmpty());

    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(*panel));
    const QString all = texts(*panel);
    QVERIFY2(all.contains(QStringLiteral("With: folder “a”, 2 selected items")), qPrintable(all));
    QVERIFY(!all.contains(QLatin1String("other view “b”")));
    // The first message carries the system prompt, then the live context.
    QVERIFY(all.contains(QStringLiteral("text=nopermission;links=3;context=")));
    QVERIFY2(all.contains(QLatin1String("Context from the file manager Orcinus")), qPrintable(all));
    QVERIFY(all.contains(QStringLiteral("- Current folder: %1 (subfolders: sub)").arg(a)));
    QVERIFY(all.contains(QLatin1String("x.jpg")));
    // Back for the message after.
    QCOMPARE(chipText(*panel, QStringLiteral("chip-other")), QStringLiteral("Other view: b"));

    // A removed chip comes back when its content changes.
    panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-other"))->findChild<QToolButton *>(QStringLiteral("remove"))->click();
    QTRY_VERIFY(!panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-other")));
    panel->refreshContext();
    QVERIFY(!panel->contextChips()->findChild<QWidget *>(QStringLiteral("chip-other")));
    window.otherFolder = a;
    panel->refreshContext();
    QCOMPARE(chipText(*panel, QStringLiteral("chip-other")), QStringLiteral("Other view: a"));

    // A folder with a trailing slash (as views give it) is named, not shown as a path.
    window.otherFolder = b + QLatin1Char('/');
    panel->refreshContext();
    QCOMPARE(chipText(*panel, QStringLiteral("chip-other")), QStringLiteral("Other view: b"));
    window.folder = a + QLatin1Char('/');
    panel->refreshContext();
    QCOMPARE(panel->contextChips()->context().folder, a);

    // A long name does not widen the panel: the chip elides it.
    const QString longName = QString(200, QLatin1Char('x'));
    window.otherFolder = dir.filePath(longName);
    panel->refreshContext();
    QCOMPARE(chipText(*panel, QStringLiteral("chip-other")), QStringLiteral("Other view: ") + longName);
    QVERIFY2(panel->contextChips()->minimumSizeHint().width() < 200, qPrintable(QString::number(panel->contextChips()->minimumSizeHint().width())));
    QVERIFY(panel->minimumSizeHint().width() < 400);

    // All rows get their room, also in a short panel: the transcript gives way.
    window = {dir.filePath(QStringLiteral("aaaaaaaaaaaaaaaaaaaa")),
              {a + QStringLiteral("/bbbbbbbbbbbbbbbbbbbb.png")},
              dir.filePath(QStringLiteral("cccccccccccccccccccc")),
              {}};
    panel->refreshContext();
    panel->resize(300, 260);
    panel->show();
    QVERIFY(QTest::qWaitForWindowExposed(panel.get()));
    panel->contextChips()->addFiles({a + QStringLiteral("/dddddddddddddddddddd.png")});
    for (const QString &name : {QStringLiteral("chip-other"), QStringLiteral("chip-added")}) {
        QWidget *chip = panel->contextChips()->findChild<QWidget *>(name);
        QTRY_VERIFY2(panel->contextChips()->rect().contains(chip->geometry()), qPrintable(name));
    }
    panel->hide();

    // Nothing to show: no row (added files go with the message).
    panel->contextChips()->messageSent();
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

void WhaleAiPanelTest::elideChipText()
{
    const QFontMetrics metrics(QApplication::font());
    const QString name = QStringLiteral("ein-sehr-langer-ordnername-der-nicht-passt-2026-final");
    const QString text = QStringLiteral("Folder: ") + name;
    auto elide = [&](int width) {
        return WhaleAiContextChips::elidedText(metrics, text, name, width);
    };

    // Fits: unchanged.
    QCOMPARE(elide(metrics.horizontalAdvance(text)), text);

    // Too long: the label, the first 6 characters, "…", the end of the name.
    const int width = metrics.horizontalAdvance(QStringLiteral("Folder: ein-se…passt-2026-final"));
    const QString elided = elide(width);
    QVERIFY2(elided.startsWith(QStringLiteral("Folder: ein-se…")), qPrintable(elided));
    QVERIFY2(elided.endsWith(QLatin1String("2026-final")), qPrintable(elided));
    QVERIFY(metrics.horizontalAdvance(elided) <= width);

    // Narrower: the head goes before the end does.
    const QString narrow = elide(metrics.horizontalAdvance(QStringLiteral("Folder: …final")));
    QVERIFY2(narrow.startsWith(QStringLiteral("Folder: …")) && narrow.endsWith(QLatin1String("final")), qPrintable(narrow));

    // Hardly any room: still within the width.
    QVERIFY(metrics.horizontalAdvance(elide(metrics.horizontalAdvance(QStringLiteral("Fol…")))) <= metrics.horizontalAdvance(QStringLiteral("Fol…")));

    // Without a name (e.g. "Selected: 3 items"): the end goes.
    const QString plain = WhaleAiContextChips::elidedText(metrics, text, QString(), width);
    QVERIFY2(plain.startsWith(QLatin1String("Folder: ein-sehr")) && plain.endsWith(QStringLiteral("…")), qPrintable(plain));
}

QTEST_MAIN(WhaleAiPanelTest)

void WhaleAiPanelTest::agentTools()
{
    // AI11: the agent reads the agents and proposes one; the user adds it.
    WhaleAiAgent secret = fakeAgent(QStringLiteral("secret"), QStringLiteral("basic"));
    secret.environment = {QStringLiteral("OPENAI_API_KEY=sk-123")};
    WhaleAiAgents::setAgents({fakeAgent(QStringLiteral("basic"), QStringLiteral("basic")), secret});
    WhaleAiPanel panel;
    panel.setViewActions({});
    QHash<QString, WhaleAiWindowTool> tools;
    for (const WhaleAiWindowTool &tool : panel.viewActions().windowTools) {
        tools.insert(tool.name, tool);
    }
    QVERIFY(tools.value(QStringLiteral("list_agents")).readOnly);
    QVERIFY(!tools.value(QStringLiteral("propose_agent")).readOnly);
    QVERIFY(WhaleAiMcpServer::windowToolNames().contains(QStringLiteral("list_agents")));
    QVERIFY(WhaleAiMcpServer::windowToolNames().contains(QStringLiteral("propose_agent")));
    // AI12: what there is for a local AI, read only (its content: whaleailocalmodelstest).
    QVERIFY(tools.value(QStringLiteral("list_local_models")).readOnly);
    QVERIFY(WhaleAiMcpServer::windowToolNames().contains(QStringLiteral("list_local_models")));
    bool error = false;
    QString notice;
    const QJsonObject listed = tools.value(QStringLiteral("list_agents")).call({}, &error, &notice);
    QVERIFY(!error);
    const QJsonArray agents = listed.value(QLatin1String("agents")).toArray();
    QCOMPARE(agents.size(), 2);
    QCOMPARE(agents.at(1).toObject().value(QLatin1String("environmentVariables")).toArray(), QJsonArray{QStringLiteral("OPENAI_API_KEY")});
    QVERIFY(!QJsonDocument(listed).toJson().contains("sk-123"));
    QVERIFY(agents.at(0).toObject().value(QLatin1String("current")).toBool());

    // A proposal is only shown; nothing is added.
    auto *bar = panel.findChild<KMessageWidget *>(QStringLiteral("agentProposal"));
    QVERIFY(bar);
    QVERIFY(bar->isHidden());
    tools.value(QStringLiteral("propose_agent")).call({{QStringLiteral("name"), QStringLiteral("x")}}, &error, &notice);
    QVERIFY(error);
    QVERIFY(bar->isHidden());
    error = false;
    const QJsonObject proposed = tools.value(QStringLiteral("propose_agent"))
                                     .call({{QStringLiteral("name"), QStringLiteral("Local Qwen")},
                                            {QStringLiteral("command"), QStringLiteral("opencode acp")},
                                            {QStringLiteral("environment"), QJsonArray{QStringLiteral("API_KEY")}}},
                                           &error,
                                           &notice);
    QVERIFY(!error);
    QVERIFY(proposed.value(QLatin1String("note")).toString().contains(QLatin1String("API keys")));
    QVERIFY(notice.contains(QLatin1String("Local Qwen")));
    QVERIFY(!bar->isHidden());
    QVERIFY(bar->text().contains(QLatin1String("Local Qwen")));
    QVERIFY(bar->text().contains(QLatin1String("API_KEY")));
    QCOMPARE(WhaleAiAgents::agents().size(), 2);
    QVERIFY(panel.proposedAgent());

    // Discard forgets it.
    QAction *discard = panel.findChild<QAction *>(QStringLiteral("proposalDiscard"));
    QVERIFY(discard);
    discard->trigger();
    QVERIFY(!panel.proposedAgent());
    QCOMPARE(WhaleAiAgents::agents().size(), 2);

    // Proposed again and added (as the dialog returns it): in the list, the bar gone.
    tools.value(QStringLiteral("propose_agent"))
        .call({{QStringLiteral("name"), QStringLiteral("Local Qwen")}, {QStringLiteral("command"), QStringLiteral("opencode acp")}}, &error, &notice);
    WhaleAiAgent confirmed = *panel.proposedAgent();
    confirmed.environment = {QStringLiteral("API_KEY=typed by the user")};
    panel.addProposedAgent(confirmed);
    QTRY_VERIFY(bar->isHidden());
    QCOMPARE(WhaleAiAgents::agents().size(), 3);
    QCOMPARE(WhaleAiAgents::agents().last().name, QStringLiteral("Local Qwen"));
    QCOMPARE(WhaleAiAgents::agents().last().id, QStringLiteral("local-qwen"));
    QCOMPARE(WhaleAiAgents::agents().last().environment, QStringList{QStringLiteral("API_KEY=typed by the user")});
    auto *combo = panel.findChild<QComboBox *>();
    QVERIFY(combo);
    QVERIFY(combo->findData(QStringLiteral("local-qwen")) >= 0);
    QCOMPARE(panel.agentId(), QStringLiteral("basic"));
    QVERIFY(texts(panel).contains(QLatin1String("Local Qwen")));
}

void WhaleAiPanelTest::localModel()
{
    // AI12: LM Studio and the model start before the agent, and go when the panel stays hidden.
    FakeLmStudio lmStudio;
    lmStudio.addModel(QStringLiteral("qwen"), QStringLiteral("Qwen 27B"));
    lmStudio.setLoadDelay(300);
    WhaleAiLocalRuntime runtime(lmStudio.options(400));
    WhaleAiLocalRuntime::setInstanceForTests(&runtime);
    auto reset = qScopeGuard([] {
        WhaleAiLocalRuntime::setInstanceForTests(nullptr);
    });
    WhaleAiAgent local = fakeAgent(QStringLiteral("local"), QStringLiteral("basic"));
    local.localProvider = QStringLiteral("lmstudio");
    local.localModel = QStringLiteral("qwen");
    local.localContextLength = 8192;
    WhaleAiAgents::setAgents({fakeAgent(QStringLiteral("basic"), QStringLiteral("basic")), local});

    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    panel.setAgentId(QStringLiteral("local"));
    // AI13f: the button loads the model ahead of the first message, and unloads it.
    auto *modelButton = panel.findChild<QToolButton *>(QStringLiteral("localModelButton"));
    auto *busy = panel.findChild<QWidget *>(QStringLiteral("busy"));
    QVERIFY(!modelButton->isHidden());
    QVERIFY(modelButton->isEnabled());
    QCOMPARE(modelButton->icon().name(), QStringLiteral("media-playback-start"));
    modelButton->click();
    QVERIFY(panel.session());
    QCOMPARE(modelButton->icon().name(), QStringLiteral("media-eject"));
    QVERIFY(!busy->isHidden());
    QTRY_VERIFY_WITH_TIMEOUT(panel.session()->state() == WhaleAcpSession::State::Ready, 10000);
    QTRY_VERIFY(busy->isHidden());
    QCOMPARE(lmStudio.loaded(QStringLiteral("qwen")), QStringList{QStringLiteral("qwen")});
    modelButton->click();
    QVERIFY(!panel.session());
    QVERIFY(texts(panel).contains(QLatin1String("The agent was stopped and its local model unloaded.")));
    // At once, not after the grace period.
    QTRY_VERIFY_WITH_TIMEOUT(lmStudio.loaded(QStringLiteral("qwen")).isEmpty(), 3000);
    QCOMPARE(modelButton->icon().name(), QStringLiteral("media-playback-start"));
    const qint64 firstService = lmStudio.servicePid();
    QTRY_VERIFY(!FakeLmStudio::isRunning(firstService));
    // As in the real /proc: the ended service is gone from it.
    QDir(lmStudio.procPath() + QStringLiteral("/") + QString::number(firstService)).removeRecursively();

    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(texts(panel).contains(QLatin1String("Starting the local model qwen first.")));
    // Busy while it loads: the agent itself is not started yet, Stop is offered, the spinner turns.
    QCOMPARE(panel.session()->state(), WhaleAcpSession::State::Idle);
    QVERIFY(panel.findChild<QToolButton *>(QStringLiteral("send"))->property("busy").toBool());
    QVERIFY(!busy->isHidden());
    auto *info = panel.findChild<QLabel *>(QStringLiteral("sessionInfo"));
    QVERIFY(info);
    QTRY_VERIFY_WITH_TIMEOUT(info->text().startsWith(QStringLiteral("Loading Qwen 27B … ")), 10000);
    QVERIFY(waitForReady(panel));
    QCOMPARE(lmStudio.loaded(QStringLiteral("qwen")), QStringList{QStringLiteral("qwen")});
    QVERIFY(lmStudio.requests().join(QLatin1Char('\n')).contains(QLatin1String("\"context_length\":8192")));
    const qint64 service = lmStudio.servicePid();
    QVERIFY(FakeLmStudio::isRunning(service));

    // Hidden shorter than the grace period: nothing happens.
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.hide();
    QTest::qWait(100);
    panel.show();
    QTest::qWait(500);
    QVERIFY(panel.session());
    QCOMPARE(lmStudio.loaded(QStringLiteral("qwen")), QStringList{QStringLiteral("qwen")});

    // Hidden for longer: the agent stops, the model and LM Studio go at once.
    panel.hide();
    QTRY_VERIFY(!panel.session());
    QVERIFY(texts(panel).contains(QLatin1String("free the memory of the local model")));
    QTRY_VERIFY(lmStudio.loaded(QStringLiteral("qwen")).isEmpty());
    QTRY_VERIFY(!FakeLmStudio::isRunning(service));

    // The next message starts it all again; a new conversation gives it back after the grace period.
    panel.show();
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(panel));
    QCOMPARE(lmStudio.loaded(QStringLiteral("qwen")), QStringList{QStringLiteral("qwen")});
    QSignalSpy freed(&runtime, &WhaleAiLocalRuntime::modelFreed);
    panel.newConversation();
    QCOMPARE(freed.count(), 0);
    QTRY_COMPARE(freed.count(), 1);
    QVERIFY(lmStudio.loaded(QStringLiteral("qwen")).isEmpty());

    // An agent without a local model starts as before.
    panel.setAgentId(QStringLiteral("basic"));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(panel));
    QVERIFY(!texts(panel).endsWith(QLatin1String("first.")));
    panel.hide();
    QTest::qWait(600);
    QVERIFY(panel.session());
}

void WhaleAiPanelTest::localModelSwitched()
{
    // AI12f: another agent, or another model for this agent, frees the old model at once, not
    // after the grace period (two models would not fit together).
    FakeLmStudio lmStudio;
    lmStudio.addModel(QStringLiteral("qwen"), QStringLiteral("Qwen 27B"));
    lmStudio.addModel(QStringLiteral("gemma"), QStringLiteral("Gemma"));
    WhaleAiLocalRuntime runtime(lmStudio.options(60000));
    WhaleAiLocalRuntime::setInstanceForTests(&runtime);
    auto reset = qScopeGuard([] {
        WhaleAiLocalRuntime::setInstanceForTests(nullptr);
    });
    WhaleAiAgent qwen = fakeAgent(QStringLiteral("qwen"), QStringLiteral("basic"));
    qwen.localProvider = QStringLiteral("lmstudio");
    qwen.localModel = QStringLiteral("qwen");
    WhaleAiAgent gemma = fakeAgent(QStringLiteral("gemma"), QStringLiteral("basic"));
    gemma.localProvider = QStringLiteral("lmstudio");
    gemma.localModel = QStringLiteral("gemma");
    WhaleAiAgents::setAgents({qwen, gemma});

    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    panel.setAgentId(QStringLiteral("qwen"));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(panel));
    QCOMPARE(lmStudio.loaded(QStringLiteral("qwen")), QStringList{QStringLiteral("qwen")});

    panel.setAgentId(QStringLiteral("gemma"));
    QVERIFY(!panel.session());
    QTRY_VERIFY_WITH_TIMEOUT(lmStudio.loaded(QStringLiteral("qwen")).isEmpty(), 2000);
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(panel));
    QCOMPARE(lmStudio.loaded(QStringLiteral("gemma")), QStringList{QStringLiteral("gemma")});

    // The agent's model changed in the settings: the conversation ends, the old model goes.
    gemma.localModel = QStringLiteral("qwen");
    WhaleAiAgents::setAgents({qwen, gemma});
    panel.readSettings();
    QVERIFY(!panel.session());
    QVERIFY(texts(panel).contains(QLatin1String("The local model of this agent was changed.")));
    QTRY_VERIFY_WITH_TIMEOUT(lmStudio.loaded(QStringLiteral("gemma")).isEmpty(), 2000);

    // A closed panel frees it after a moment (here 3 s), not after the grace period.
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QVERIFY(waitForReady(panel));
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.hide();
    QTRY_VERIFY_WITH_TIMEOUT(lmStudio.loaded(QStringLiteral("qwen")).isEmpty(), 6000);
    QVERIFY(!panel.session());
    QVERIFY(texts(panel).contains(QLatin1String("when the panel was closed")));
}

void WhaleAiPanelTest::strataKeptAfterClosing()
{
    // AI13c: a Strata agent starts Strata; a closed panel keeps it for the delay set for Strata
    // (its reload takes 90 s), then frees it and stops the server Orcinus started.
    FakeStrata strata;
    WhaleAiLocalRuntime runtime(strata.options(60000));
    WhaleAiLocalRuntime::setInstanceForTests(&runtime);
    auto reset = qScopeGuard([] {
        WhaleAiLocalRuntime::setInstanceForTests(nullptr);
        WhaleAiAgents::setFreeDelaySeconds(QStringLiteral("strata"), WhaleAiAgents::defaultFreeDelaySeconds(QStringLiteral("strata")));
    });
    WhaleAiAgents::setFreeDelaySeconds(QStringLiteral("strata"), 2);
    WhaleAiAgent agent = fakeAgent(QStringLiteral("strata"), QStringLiteral("basic"));
    agent.localProvider = QStringLiteral("strata");
    agent.localModel = QStringLiteral("small");
    WhaleAiAgents::setAgents({agent});

    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    auto *info = panel.findChild<QLabel *>(QStringLiteral("sessionInfo"));
    QTRY_VERIFY_WITH_TIMEOUT(info->text().startsWith(QStringLiteral("Starting Strata Small … ")), 10000);
    QVERIFY(waitForReady(panel));
    QCOMPARE(strata.servers().size(), 1);

    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.hide();
    QTest::qWait(1000);
    QVERIFY(panel.session());
    QCOMPARE(strata.servers().size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!panel.session(), 5000);
    QTRY_VERIFY(strata.servers().isEmpty());
}

void WhaleAiPanelTest::localModelFails()
{
    FakeLmStudio lmStudio;
    WhaleAiLocalRuntime runtime(lmStudio.options(200));
    WhaleAiLocalRuntime::setInstanceForTests(&runtime);
    auto reset = qScopeGuard([] {
        WhaleAiLocalRuntime::setInstanceForTests(nullptr);
    });
    WhaleAiAgent local = fakeAgent(QStringLiteral("local"), QStringLiteral("basic"));
    local.localProvider = QStringLiteral("lmstudio");
    local.localModel = QStringLiteral("missing");
    WhaleAiAgents::setAgents({local});

    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission")));
    QTRY_VERIFY_WITH_TIMEOUT(kinds(panel).contains(WhaleAiTranscript::Kind::Error), 10000);
    QVERIFY(texts(panel).contains(QLatin1String("The local model could not be started.")));
    QVERIFY(!panel.session());
    QVERIFY(!panel.findChild<QToolButton *>(QStringLiteral("send"))->property("busy").toBool());
    // The service started for it goes again at once.
    const qint64 service = lmStudio.servicePid();
    QTRY_VERIFY(!FakeLmStudio::isRunning(service));
}

void WhaleAiPanelTest::backChannel()
{
    QVERIFY(!WhaleAiMcpServer::relay().isEmpty());
    WhaleAiPanel panel;
    panel.setUrl(QUrl::fromLocalFile(m_work.path()));
    const QString other = QDir::tempPath();
    const QString selected = m_work.filePath(QStringLiteral("a.txt"));
    panel.setContextProvider([&] {
        return WhaleAiContext{m_work.path(), {selected}, other, {}};
    });

    // The agent starts orcinus-mcp from session/new and asks where the user is.
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission mcp:get_location")));
    QVERIFY(waitForReady(panel));
    QVERIFY2(
        texts(panel).contains(QStringLiteral("tools=get_location,get_selection,set_location,select_items,list_actions,trigger_action;mcp={\"folder\":\"%1\","
                                             "\"otherFolder\":\"%2\"};")
                                  .arg(m_work.path(), other)),
        qPrintable(texts(panel)));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission mcp:get_selection")));
    QVERIFY(waitForReady(panel));
    QVERIFY2(texts(panel).contains(QStringLiteral("mcp={\"count\":1,\"folder\":\"%1\",\"items\":[\"%2\"],\"truncated\":false};").arg(m_work.path(), selected)),
             qPrintable(texts(panel)));

    // The agent changes the view through the window; the transcript says so.
    QStringList opened;
    QStringList selection;
    WhaleAiViewActions viewActions;
    viewActions.openFolder = [&](const QString &folder) {
        opened.append(folder);
    };
    viewActions.select = [&](const QStringList &paths) {
        selection = paths;
    };
    panel.setViewActions(viewActions);
    QFile file(selected);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY(QDir(m_work.path()).mkpath(QStringLiteral("sub")));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission mcp:set_location:%1 mcp:select_items:a.txt,missing").arg(m_work.filePath(QStringLiteral("sub")))));
    QVERIFY(waitForReady(panel));
    QCOMPARE(opened, QStringList{m_work.filePath(QStringLiteral("sub"))});
    QCOMPARE(selection, QStringList{selected});
    QString all = texts(panel);
    // The name only; the path in the tooltip.
    QVERIFY2(all.contains(QStringLiteral("The agent opened “sub”.")), qPrintable(all));
    bool tooltip = false;
    for (const QLabel *label : panel.transcript()->findChildren<QLabel *>()) {
        tooltip = tooltip || (label->text() == QStringLiteral("The agent opened “sub”.") && label->toolTip() == m_work.filePath(QStringLiteral("sub")));
    }
    QVERIFY(tooltip);
    QVERIFY2(all.contains(QLatin1String("The agent selected 1 item.")), qPrintable(all));
    QVERIFY2(all.contains(QLatin1String("\"notFound\":[\"missing\"],\"selected\":1")), qPrintable(all));

    // Commands of the window (AI9b): the transcript names the command with its menu.
    QStringList triggered;
    bool hidden = false;
    viewActions.actions = [&] {
        return QList<WhaleAiUiAction>{
            {QStringLiteral("details"), QStringLiteral("Details"), {}, QStringLiteral("View ▸ View Mode"), true, !triggered.isEmpty(), true, true},
            {QStringLiteral("show_hidden_files"), QStringLiteral("Show Hidden Files"), {}, QStringLiteral("View"), true, hidden, true, false},
        };
    };
    viewActions.trigger = [&](const QString &name) {
        triggered.append(name);
        hidden = hidden != (name == QLatin1String("show_hidden_files"));
    };
    panel.setViewActions(viewActions);
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission mcp:trigger_action:details mcp:trigger_action:show_hidden_files mcp:trigger_action:deletefile")));
    QVERIFY(waitForReady(panel));
    QCOMPARE(triggered, (QStringList{QStringLiteral("details"), QStringLiteral("show_hidden_files")}));
    all = texts(panel);
    QVERIFY2(all.contains(QStringLiteral("The agent used View ▸ View Mode ▸ Details.")), qPrintable(all));
    QVERIFY2(all.contains(QStringLiteral("The agent turned on View ▸ Show Hidden Files.")), qPrintable(all));
    QVERIFY2(all.contains(QLatin1String("not available")), qPrintable(all));

    // Settings (AI9d): refused in the read-only mode, allowed in another one (user, 2026-11-16).
    int settingCalls = 0;
    viewActions.windowTools = {{QStringLiteral("set_setting"),
                                QStringLiteral("Change a setting"),
                                QStringLiteral("Changes a setting"),
                                false,
                                {},
                                {},
                                [&settingCalls](const QJsonObject &, bool *, QString *notice) {
                                    ++settingCalls;
                                    *notice = QStringLiteral("The agent changed the setting “Status bar” to Disabled.");
                                    return QJsonObject{{QStringLiteral("value"), QStringLiteral("Disabled")}};
                                }}};
    panel.setViewActions(viewActions);
    panel.chooseMode(QStringLiteral("plan"));
    QTRY_COMPARE(panel.session()->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("plan"));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission mcp:set_setting:General/ShowStatusBar=Disabled")));
    QVERIFY(waitForReady(panel));
    QCOMPARE(settingCalls, 0);
    all = texts(panel);
    QVERIFY2(all.contains(QLatin1String("cannot be saved in the read-only mode")), qPrintable(all));
    // Canvas tools (CV3): writing, opening and using them is refused there too; reading is not.
    int toolCalls = 0;
    auto canvasTool = [&toolCalls](const QString &name, bool readOnly) {
        return WhaleAiWindowTool{name, name, QStringLiteral("A canvas tool"), readOnly, {}, {}, [&toolCalls](const QJsonObject &, bool *, QString *) {
                                     ++toolCalls;
                                     return QJsonObject{{QStringLiteral("ok"), true}};
                                 }};
    };
    const QList<WhaleAiWindowTool> settingTools = viewActions.windowTools;
    viewActions.windowTools = settingTools
        + QList<WhaleAiWindowTool>{canvasTool(QStringLiteral("tool_open"), false),
                                   canvasTool(QStringLiteral("tool_write_file"), false),
                                   canvasTool(QStringLiteral("tool_invoke"), false),
                                   canvasTool(QStringLiteral("tool_log"), true)};
    panel.setViewActions(viewActions);
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission mcp:tool_open:counter mcp:tool_write_file:counter mcp:tool_invoke:counter mcp:tool_log:counter")));
    QVERIFY(waitForReady(panel));
    QCOMPARE(toolCalls, 1);
    all = texts(panel);
    QVERIFY2(all.contains(QLatin1String("Canvas tools cannot be written, opened or used in the read-only mode")), qPrintable(all));
    viewActions.windowTools = settingTools;
    panel.setViewActions(viewActions);
    // The other modes by the names of the Mode list (the fake agent's "Ask" and "Auto").
    QVERIFY2(all.contains(QLatin1String("(\\\"Ask\\\", \\\"Auto\\\")")), qPrintable(all));
    panel.chooseMode(QStringLiteral("auto"));
    QTRY_COMPARE(panel.session()->configOption(QStringLiteral("mode"))->currentValue, QStringLiteral("auto"));
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission mcp:set_setting:General/ShowStatusBar=Disabled")));
    QVERIFY(waitForReady(panel));
    QCOMPARE(settingCalls, 1);
    QVERIFY2(texts(panel).contains(QStringLiteral("The agent changed the setting “Status bar” to Disabled.")), qPrintable(texts(panel)));
    viewActions.windowTools.clear();
    panel.setViewActions(viewActions);

    // In split view the notices say which view.
    viewActions.activePane = [] {
        return 1;
    };
    panel.setViewActions(viewActions);
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission mcp:set_location:%1 mcp:get_location").arg(m_work.filePath(QStringLiteral("sub")))));
    QVERIFY(waitForReady(panel));
    all = texts(panel);
    QVERIFY2(all.contains(QStringLiteral("The agent opened “sub” in the right view.")), qPrintable(all));
    QVERIFY2(all.contains(QLatin1String("\"activeView\":\"right\"")), qPrintable(all));

    // The Actions filter hides what the agent did, also what comes later, and remembers it.
    auto *showActions = panel.findChild<QCheckBox *>(QStringLiteral("showActions"));
    QVERIFY(showActions && showActions->isChecked());
    auto actionEntries = [&panel] {
        QList<QWidget *> result;
        for (QWidget *entry : panel.transcript()->entries()) {
            if (WhaleAiTranscript::kindOf(entry) == WhaleAiTranscript::Kind::Action) {
                result.append(entry);
            }
        }
        return result;
    };
    QVERIFY(actionEntries().size() >= 4);
    showActions->setChecked(false);
    QVERIFY(!WhaleAiAgents::showActions());
    for (QWidget *entry : panel.transcript()->entries()) {
        const WhaleAiTranscript::Kind kind = WhaleAiTranscript::kindOf(entry);
        QCOMPARE(entry->isHidden(),
                 kind == WhaleAiTranscript::Kind::Action || kind == WhaleAiTranscript::Kind::ToolCall || kind == WhaleAiTranscript::Kind::Thought);
    }
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission mcp:trigger_action:details")));
    QVERIFY(waitForReady(panel));
    QVERIFY(actionEntries().constLast()->isHidden());
    showActions->setChecked(true);
    QVERIFY(WhaleAiAgents::showActions());
    for (QWidget *entry : actionEntries()) {
        QVERIFY(!entry->isHidden());
    }

    // Times: always kept, shown only when asked (off at first), and remembered.
    auto *showTimestamps = panel.findChild<QCheckBox *>(QStringLiteral("showTimestamps"));
    QVERIFY(showTimestamps && !showTimestamps->isChecked());
    auto stamps = [&panel] {
        QList<QLabel *> result;
        for (QWidget *entry : panel.transcript()->entries()) {
            [&] {
                QVERIFY(WhaleAiTranscript::timeOf(entry).isValid());
                auto *stamp = entry->parentWidget()->findChild<QLabel *>(QStringLiteral("timestamp"), Qt::FindDirectChildrenOnly);
                QVERIFY(stamp && !stamp->text().isEmpty());
                result.append(stamp);
            }();
        }
        return result;
    };
    QVERIFY(!stamps().isEmpty());
    for (QLabel *stamp : stamps()) {
        QVERIFY(stamp->isHidden());
    }
    showTimestamps->setChecked(true);
    QVERIFY(WhaleAiAgents::showTimestamps());
    QVERIFY(panel.sendPrompt(QStringLiteral("nopermission mcp:trigger_action:details")));
    QVERIFY(waitForReady(panel));
    for (QLabel *stamp : stamps()) {
        QVERIFY(!stamp->isHidden());
    }
    // A hidden action takes its time along.
    showActions->setChecked(false);
    QVERIFY(!actionEntries().constLast()->isVisibleTo(panel.transcript()));
    QVERIFY(actionEntries().constLast()->parentWidget()->isHidden());
    showActions->setChecked(true);
    showTimestamps->setChecked(false);
    QVERIFY(!WhaleAiAgents::showTimestamps());
    for (QLabel *stamp : stamps()) {
        QVERIFY(stamp->isHidden());
    }

    // Requests for our own tools are answered at once, as each agent asks; others are not.
    for (const char *agent : {"claude", "codex", "opencode"}) {
        QVERIFY(panel.sendPrompt(QStringLiteral("askorcinus-%1").arg(QString::fromLatin1(agent))));
        QVERIFY2(waitForReady(panel), agent);
        QVERIFY(panel.transcript()->findChildren<QWidget *>(QStringLiteral("permission-n7")).isEmpty());
    }
    QCOMPARE(texts(panel).count(QLatin1String("permission=yes;")), 3);
    QVERIFY(panel.sendPrompt(QStringLiteral("askorcinus-spoof")));
    QVERIFY2(waitForPermission(panel), qPrintable(texts(panel).right(1500)));

    // Each conversation has its own socket; it goes with the conversation.
    const QString socket = panel.session()->findChild<WhaleAiMcpServer *>()->socketPath();
    QVERIFY(QFileInfo::exists(socket));
    panel.newConversation();
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(socket), 15000);
}

void WhaleAiPanelTest::inputKeepsFocus()
{
    // The window takes the focus when the agent runs a command; the user typing here keeps it.
    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *view = new QLineEdit(&window);
    auto *other = new QLineEdit(&window);
    auto *panel = new WhaleAiPanel(&window);
    layout->addWidget(view);
    layout->addWidget(other);
    layout->addWidget(panel);
    panel->setUrl(QUrl::fromLocalFile(m_work.path()));
    WhaleAiViewActions viewActions;
    viewActions.actions = [] {
        return QList<WhaleAiUiAction>{{QStringLiteral("details"), QStringLiteral("Details"), {}, QStringLiteral("View"), true, false, true, true}};
    };
    viewActions.trigger = [view](const QString &) {
        view->setFocus();
    };
    panel->setViewActions(viewActions);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *input = panel->findChild<QPlainTextEdit *>(QStringLiteral("input"));
    QVERIFY(input);

    input->setFocus();
    QTRY_VERIFY(input->hasFocus());
    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission mcp:trigger_action:details")));
    QVERIFY(waitForReady(*panel));
    QTRY_VERIFY(input->hasFocus());

    // Somewhere else: the command takes the focus, as it would for the user.
    other->setFocus();
    QTRY_VERIFY(other->hasFocus());
    QVERIFY(panel->sendPrompt(QStringLiteral("nopermission mcp:trigger_action:details")));
    QVERIFY(waitForReady(*panel));
    QTRY_VERIFY(view->hasFocus());

    // Answering a permission request on its focused button: the focus goes back to the input,
    // not on to the next widget.
    QVERIFY(panel->sendPrompt(QStringLiteral("hello")));
    QWidget *permission = waitForPermission(*panel);
    QVERIFY(permission);
    auto *no = permission->findChild<QPushButton *>(QStringLiteral("option-no"));
    no->setFocus();
    QTRY_VERIFY(no->hasFocus());
    no->click();
    QTRY_VERIFY(input->hasFocus());
    QVERIFY(waitForReady(*panel));
}

#include "whaleaipaneltest.moc"
