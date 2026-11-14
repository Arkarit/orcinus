/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaisettingspage.h"
#include "dolphinmainwindow.h"
#include "settings/dolphinsettingsdialog.h"
#include "testdir.h"
#include "whaleaiagentdialog.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KMessageBox>
#include <KSharedConfig>

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

/**
 * The "AI Agents" page of the settings dialog (AI2): adding known and other agents, editing,
 * removing, the default, and the test button against the scripted agent whaleacpfakeagent.
 */
class WhaleAiSettingsPageTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void emptyPage();
    void addKnownAgent();
    void addOtherAgent();
    void editAgent();
    void removeAndDefault();
    void testReady();
    void testNeedsLogin();
    void loginHintText();
    void defaultsKeepAgents();
    void systemPrompt();
    void pageInDialog();

private:
    template<typename T>
    static T *child(QWidget &page, const QString &name)
    {
        return page.findChild<T *>(name);
    }
    static QMenu *addMenu(WhaleAiSettingsPage &page);
    static QAction *menuAction(WhaleAiSettingsPage &page, const QString &name);
    /** Fills the next agent dialog that opens with @p name, @p command and @p environment. */
    static void answerNextDialog(const QString &name, const QString &command, const QString &environment = QString());
    static WhaleAiAgent fakeAgent(const QString &id, const QString &scenario);

    QTemporaryDir m_home;
    QTemporaryDir m_bin;
};

void WhaleAiSettingsPageTest::initTestCase()
{
    // A configuration of its own: other tests change the shared one of the test mode.
    QVERIFY(m_home.isValid());
    for (const char *variable : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"}) {
        const QString path = m_home.filePath(QString::fromLatin1(variable).toLower());
        QVERIFY(QDir().mkpath(path));
        qputenv(variable, QFile::encodeName(path));
    }
    // Only OpenCode is "installed".
    QFile opencode(m_bin.filePath(QStringLiteral("opencode")));
    QVERIFY(opencode.open(QIODevice::WriteOnly));
    opencode.write("#!/bin/sh\n");
    opencode.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

void WhaleAiSettingsPageTest::init()
{
    KSharedConfig::openConfig()->deleteGroup(QStringLiteral("AI"));
    KSharedConfig::openConfig()->sync();
}

QMenu *WhaleAiSettingsPageTest::addMenu(WhaleAiSettingsPage &page)
{
    QMenu *menu = child<QPushButton>(page, QStringLiteral("add"))->menu();
    Q_EMIT menu->aboutToShow();
    return menu;
}

QAction *WhaleAiSettingsPageTest::menuAction(WhaleAiSettingsPage &page, const QString &name)
{
    const QList<QAction *> actions = addMenu(page)->actions();
    for (QAction *action : actions) {
        if (action->objectName() == name) {
            return action;
        }
    }
    return nullptr;
}

void WhaleAiSettingsPageTest::answerNextDialog(const QString &name, const QString &command, const QString &environment)
{
    QTimer::singleShot(0, [name, command, environment] {
        auto *dialog = qobject_cast<WhaleAiAgentDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        dialog->findChild<QLineEdit *>(QStringLiteral("name"))->setText(name);
        dialog->findChild<QLineEdit *>(QStringLiteral("command"))->setText(command);
        dialog->findChild<QPlainTextEdit *>(QStringLiteral("environment"))->setPlainText(environment);
        dialog->accept();
    });
}

WhaleAiAgent WhaleAiSettingsPageTest::fakeAgent(const QString &id, const QString &scenario)
{
    return {id, id.toUpper(), QStringLiteral("'%1' %2").arg(QStringLiteral(WHALE_ACP_FAKE_AGENT), scenario), {}, {}};
}

void WhaleAiSettingsPageTest::emptyPage()
{
    WhaleAiSettingsPage page;
    QCOMPARE(child<QListWidget>(page, QStringLiteral("agents"))->count(), 0);
    QVERIFY(child<QPushButton>(page, QStringLiteral("add"))->isEnabled());
    for (const QString &name : {QStringLiteral("edit"), QStringLiteral("remove"), QStringLiteral("default"), QStringLiteral("test")}) {
        QVERIFY2(!child<QPushButton>(page, name)->isEnabled(), qPrintable(name));
    }
}

void WhaleAiSettingsPageTest::addKnownAgent()
{
    WhaleAiSettingsPage page;
    page.setPresetSearchPaths({m_bin.path()});
    QSignalSpy changed(&page, &WhaleAiSettingsPage::changed);

    QAction *claude = menuAction(page, QStringLiteral("preset-claude"));
    QVERIFY(claude);
    QVERIFY(!claude->isEnabled());
    QVERIFY(claude->text().contains(QLatin1String("not found")));
    QVERIFY(claude->toolTip().contains(QLatin1String("npm install -g @agentclientprotocol/claude-agent-acp")));
    QVERIFY(menuAction(page, QStringLiteral("custom")));

    QAction *opencode = menuAction(page, QStringLiteral("preset-opencode"));
    QVERIFY(opencode->isEnabled());
    opencode->trigger();
    QCOMPARE(changed.size(), 1);
    QCOMPARE(page.agents().size(), 1);
    QCOMPARE(page.agents().first().id, QStringLiteral("opencode"));
    QCOMPARE(page.agents().first().commandLine, QStringLiteral("opencode acp"));
    // The first agent is the default.
    QCOMPARE(page.defaultAgentId(), QStringLiteral("opencode"));
    auto *list = child<QListWidget>(page, QStringLiteral("agents"));
    QCOMPARE(list->item(0)->text(), QStringLiteral("OpenCode (default)"));
    QVERIFY(list->item(0)->font().bold());

    // Twice: an id of its own.
    menuAction(page, QStringLiteral("preset-opencode"))->trigger();
    QCOMPARE(page.agents().at(1).id, QStringLiteral("opencode-2"));
    QCOMPARE(page.defaultAgentId(), QStringLiteral("opencode"));
    QCOMPARE(list->currentRow(), 1);

    // Nothing is stored before Apply.
    QVERIFY(WhaleAiAgents::agents().isEmpty());
    page.applySettings();
    QCOMPARE(WhaleAiAgents::agents(), page.agents());
    QCOMPARE(WhaleAiAgents::defaultAgentId(), QStringLiteral("opencode"));
}

void WhaleAiSettingsPageTest::addOtherAgent()
{
    WhaleAiSettingsPage page;
    answerNextDialog(QStringLiteral("Local Qwen"), QStringLiteral("opencode acp --pure"), QStringLiteral("A=1\n\n  B=2  "));
    menuAction(page, QStringLiteral("custom"))->trigger();
    QCOMPARE(page.agents().size(), 1);
    const WhaleAiAgent agent = page.agents().first();
    QCOMPARE(agent.id, QStringLiteral("local-qwen"));
    QCOMPARE(agent.name, QStringLiteral("Local Qwen"));
    QCOMPARE(agent.commandLine, QStringLiteral("opencode acp --pure"));
    QCOMPARE(agent.environment, (QStringList{QStringLiteral("A=1"), QStringLiteral("B=2")}));
}

void WhaleAiSettingsPageTest::editAgent()
{
    WhaleAiAgents::setAgents({fakeAgent(QStringLiteral("a"), QStringLiteral("basic"))});
    WhaleAiSettingsPage page;
    QSignalSpy changed(&page, &WhaleAiSettingsPage::changed);
    answerNextDialog(QStringLiteral("Renamed"), QStringLiteral("sh"));
    child<QPushButton>(page, QStringLiteral("edit"))->click();
    QCOMPARE(page.agents().first().id, QStringLiteral("a"));
    QCOMPARE(page.agents().first().name, QStringLiteral("Renamed"));
    QCOMPARE(changed.size(), 1);

    // The same values again: nothing changed.
    answerNextDialog(QStringLiteral("Renamed"), QStringLiteral("sh"));
    child<QPushButton>(page, QStringLiteral("edit"))->click();
    QCOMPARE(changed.size(), 1);
}

void WhaleAiSettingsPageTest::removeAndDefault()
{
    WhaleAiAgents::setAgents({fakeAgent(QStringLiteral("a"), QStringLiteral("basic")), fakeAgent(QStringLiteral("b"), QStringLiteral("basic"))});
    WhaleAiAgents::setDefaultAgentId(QStringLiteral("a"));
    WhaleAiSettingsPage page;
    auto *list = child<QListWidget>(page, QStringLiteral("agents"));
    auto *makeDefault = child<QPushButton>(page, QStringLiteral("default"));
    // The default is selected at first; it cannot be made the default again.
    QCOMPARE(list->currentRow(), 0);
    QVERIFY(!makeDefault->isEnabled());

    list->setCurrentRow(1);
    QVERIFY(makeDefault->isEnabled());
    makeDefault->click();
    QCOMPARE(page.defaultAgentId(), QStringLiteral("b"));
    QCOMPARE(list->item(1)->text(), QStringLiteral("B (default)"));

    // Removing the default makes the first one left the default.
    list->setCurrentRow(1);
    child<QPushButton>(page, QStringLiteral("remove"))->click();
    QCOMPARE(page.agents().size(), 1);
    QCOMPARE(page.defaultAgentId(), QStringLiteral("a"));
    child<QPushButton>(page, QStringLiteral("remove"))->click();
    QVERIFY(page.agents().isEmpty());
    QVERIFY(page.defaultAgentId().isEmpty());
    page.applySettings();
    QVERIFY(WhaleAiAgents::agents().isEmpty());
}

void WhaleAiSettingsPageTest::testReady()
{
    WhaleAiAgents::setAgents({fakeAgent(QStringLiteral("a"), QStringLiteral("basic"))});
    WhaleAiSettingsPage page;
    page.setTestTimeout(5000);
    auto *test = child<QPushButton>(page, QStringLiteral("test"));
    auto *result = child<QLabel>(page, QStringLiteral("testResult"));
    test->click();
    QVERIFY(!test->isEnabled());
    QVERIFY(result->text().contains(QLatin1String("Starting A")));
    QTRY_VERIFY_WITH_TIMEOUT(test->isEnabled(), 10000);
    QVERIFY2(result->text().contains(QLatin1String("<b>Fake Agent 1.2.3</b> is ready.")), qPrintable(result->text()));
    QVERIFY2(result->text().contains(QLatin1String("Mode: Auto (of: Ask, Plan, Auto)")), qPrintable(result->text()));
    QVERIFY2(result->text().contains(QLatin1String("Model: Small")), qPrintable(result->text()));
}

void WhaleAiSettingsPageTest::testNeedsLogin()
{
    WhaleAiAgents::setAgents({fakeAgent(QStringLiteral("a"), QStringLiteral("auth"))});
    WhaleAiSettingsPage page;
    page.setTestTimeout(5000);
    auto *test = child<QPushButton>(page, QStringLiteral("test"));
    auto *result = child<QLabel>(page, QStringLiteral("testResult"));
    test->click();
    QTRY_VERIFY_WITH_TIMEOUT(test->isEnabled(), 10000);
    const QString text = result->text();
    QVERIFY2(text.contains(QLatin1String("needs a login")), qPrintable(text));
    QVERIFY2(text.contains(QLatin1String("Login: Run `fake login` in a terminal")), qPrintable(text));
    QVERIFY2(text.contains(QLatin1String("fake agent started")), qPrintable(text));
    // Not a known agent: nothing to run.
    QVERIFY(!child<QPushButton>(page, QStringLiteral("logIn"))->isVisible());
}

void WhaleAiSettingsPageTest::loginHintText()
{
    WhaleAiAgentTest::Result result;
    result.error = QStringLiteral("The agent needs a login first.");
    result.needsLogin = true;
    result.agentInfo.authMethods = {{QStringLiteral("chat-gpt"), QStringLiteral("ChatGPT"), QStringLiteral("Use ChatGPT to authenticate")}};
    WhaleAiLoginHint hint;
    hint.text = QStringLiteral("Run codex login & so on");
    hint.command = QStringLiteral("codex login");
    hint.url = QUrl(QStringLiteral("https://github.com/openai/codex"));

    const QString text = WhaleAiSettingsPage::resultText(result, hint);
    QVERIFY2(text.contains(QLatin1String("Run codex login &amp; so on")), qPrintable(text));
    QVERIFY2(text.contains(QLatin1String("<a href=\"https://github.com/openai/codex\">")), qPrintable(text));
    // The agent's own ways, which Orcinus cannot run, are left out.
    QVERIFY(!text.contains(QLatin1String("ChatGPT")));
    // Without a hint, they are shown.
    QVERIFY(WhaleAiSettingsPage::resultText(result).contains(QLatin1String("ChatGPT: Use ChatGPT to authenticate")));
}

void WhaleAiSettingsPageTest::defaultsKeepAgents()
{
    WhaleAiAgents::setAgents({fakeAgent(QStringLiteral("a"), QStringLiteral("basic"))});
    WhaleAiSettingsPage page;
    page.restoreDefaults();
    page.applySettings();
    QCOMPARE(WhaleAiAgents::agents().size(), 1);
}

void WhaleAiSettingsPageTest::systemPrompt()
{
    {
        WhaleAiSettingsPage page;
        auto *edit = child<QCheckBox>(page, QStringLiteral("editPrompt"));
        auto *field = child<QPlainTextEdit>(page, QStringLiteral("systemPrompt"));
        auto *reset = child<QPushButton>(page, QStringLiteral("resetPrompt"));
        QVERIFY(edit && field && reset);
        // Default: the built-in text, read-only, editing off.
        QVERIFY(!edit->isChecked());
        QVERIFY(field->isReadOnly());
        QVERIFY(!reset->isEnabled());
        QCOMPARE(field->toPlainText(), WhaleAiAgents::defaultSystemPrompt());

        // Turning editing on (the warning is suppressed in the test) unlocks the field.
        KMessageBox::saveDontShowAgainContinue(QStringLiteral("whale-ai-edit-system-prompt"));
        edit->setChecked(true);
        QVERIFY(!field->isReadOnly());
        QVERIFY(reset->isEnabled());
        field->setPlainText(QStringLiteral("Answer only in haiku."));
        page.applySettings();
    }
    QVERIFY(WhaleAiAgents::customSystemPrompt());
    QCOMPARE(WhaleAiAgents::systemPrompt(), QStringLiteral("Answer only in haiku."));

    // A fresh page shows the stored custom text, editable; turning editing off goes back to the
    // built-in text and, on apply, stops using the custom one (which stays stored).
    {
        WhaleAiSettingsPage page;
        auto *edit = child<QCheckBox>(page, QStringLiteral("editPrompt"));
        auto *field = child<QPlainTextEdit>(page, QStringLiteral("systemPrompt"));
        QVERIFY(edit->isChecked());
        QVERIFY(!field->isReadOnly());
        QCOMPARE(field->toPlainText(), QStringLiteral("Answer only in haiku."));
        edit->setChecked(false);
        QVERIFY(field->isReadOnly());
        QCOMPARE(field->toPlainText(), WhaleAiAgents::defaultSystemPrompt());
        page.applySettings();
    }
    QVERIFY(!WhaleAiAgents::customSystemPrompt());
    QCOMPARE(WhaleAiAgents::systemPrompt(), WhaleAiAgents::defaultSystemPrompt());
}

void WhaleAiSettingsPageTest::pageInDialog()
{
    TestDir dir;
    dir.createFile(QStringLiteral("a.txt"));
    DolphinMainWindow mainWindow;
    mainWindow.openDirectories({dir.url()}, false);
    DolphinSettingsDialog dialog(dir.url(), &mainWindow, mainWindow.actionCollection());
    auto *page = dialog.findChild<WhaleAiSettingsPage *>();
    QVERIFY(page);

    QPushButton *apply = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply);
    QVERIFY(!apply->isEnabled());
    answerNextDialog(QStringLiteral("Shell"), QStringLiteral("sh"));
    menuAction(*page, QStringLiteral("custom"))->trigger();
    QVERIFY(apply->isEnabled());
    apply->click();
    QCOMPARE(WhaleAiAgents::agents().size(), 1);
    QCOMPARE(WhaleAiAgents::defaultAgentId(), QStringLiteral("shell"));
}

QTEST_MAIN(WhaleAiSettingsPageTest)

#include "whaleaisettingspagetest.moc"
