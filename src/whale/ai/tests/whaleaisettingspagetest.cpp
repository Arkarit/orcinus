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
#include "whaleaimemorywidget.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KMessageBox>
#include <KSharedConfig>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

/**
 * The "AI Support" page of the settings dialog (AI2): adding known and other agents, editing,
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
    void addLocalModel();
    void localModelOfOwnConfig();
    void addStrataModel();
    void freeDelays();
    void editAgent();
    void removeAndDefault();
    void testReady();
    void testNeedsLogin();
    void loginHintText();
    void defaultsKeepAgents();
    void systemPrompt();
    void allowAdmin();
    void conversationLanguage();
    void pageInDialog();
    void memoryGroup();

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
    /** Options that find a fake LM Studio with two models in @p folder (its server not running). */
    WhaleAiLocalModels::Options lmStudioOptions(const QString &folder) const;
    /** @return The JSON of OPENCODE_CONFIG_CONTENT in @p environment. */
    static QJsonObject openCodeConfig(const QString &environment);

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
    // The disclaimer has its own test (WhaleAiPanelTest::disclaimer()).
    WhaleAiAgents::setDisclaimerAccepted(true);
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

WhaleAiLocalModels::Options WhaleAiSettingsPageTest::lmStudioOptions(const QString &folder) const
{
    auto write = [](const QString &path, const QByteArray &content) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    };
    // Port 1: nothing listens there.
    write(folder + QStringLiteral("/.lmstudio/.internal/http-server-config.json"), R"({"port": 1, "networkInterface": "127.0.0.1"})");
    write(folder + QStringLiteral("/.lmstudio/.internal/model-index-cache.json"), R"({"models": [
      {"domain": "llm", "defaultIdentifier": "alpha-chat", "displayName": "Alpha Chat", "contextLength": 32768, "trainedForToolUse": false},
      {"domain": "llm", "defaultIdentifier": "qwen", "displayName": "Qwen 27B", "params": "27B", "quant": {"name": "Q6_K"},
       "sizeBytes": 23400000000, "contextLength": 262144, "trainedForToolUse": true, "visionAdapter": {}}
    ]})");
    WhaleAiLocalModels::Options options;
    options.homePath = folder;
    options.searchPaths = {m_bin.path()};
    options.procPath = folder + QStringLiteral("/proc");
    return options;
}

QJsonObject WhaleAiSettingsPageTest::openCodeConfig(const QString &environment)
{
    for (const QString &line : environment.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("OPENCODE_CONFIG_CONTENT="))) {
            return QJsonDocument::fromJson(line.section(QLatin1Char('='), 1).toUtf8()).object();
        }
    }
    return {};
}

void WhaleAiSettingsPageTest::addLocalModel()
{
    // Without LM Studio the entry is there, but off.
    QTemporaryDir empty;
    WhaleAiSettingsPage page;
    WhaleAiLocalModels::Options none;
    none.homePath = empty.path();
    none.searchPaths = {m_bin.path()};
    page.setLocalModelOptions(none);
    QAction *local = menuAction(page, QStringLiteral("localModel"));
    QVERIFY(local);
    QVERIFY(!local->isEnabled());
    QVERIFY(local->text().contains(QLatin1String("not found")));

    QTemporaryDir lmStudio;
    page.setLocalModelOptions(lmStudioOptions(lmStudio.path()));
    local = menuAction(page, QStringLiteral("localModel"));
    QVERIFY(local->isEnabled());
    QCOMPARE(local->text(), QStringLiteral("Local Model (LM Studio)…"));

    QStringList checked;
    QTimer::singleShot(0, [&checked] {
        auto *dialog = qobject_cast<WhaleAiAgentDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        // A failed check must not leave the dialog open.
        auto close = qScopeGuard([dialog] {
            if (dialog->isVisible()) {
                dialog->reject();
            }
        });
        auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("name"));
        auto *command = dialog->findChild<QLineEdit *>(QStringLiteral("command"));
        auto *environment = dialog->findChild<QPlainTextEdit *>(QStringLiteral("environment"));
        auto *model = dialog->findChild<QComboBox *>(QStringLiteral("localModel"));
        auto *context = dialog->findChild<QSpinBox *>(QStringLiteral("contextLength"));
        auto *state = dialog->findChild<QLabel *>(QStringLiteral("localState"));
        // Filled in: the model that can call tools, run by OpenCode, configured for it.
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Add Local Model"));
        QCOMPARE(name->text(), QStringLiteral("Qwen 27B (local)"));
        QCOMPARE(command->text(), QStringLiteral("opencode acp"));
        QCOMPARE(model->currentData().toString(), QStringLiteral("qwen"));
        QCOMPARE(model->count(), 4); // None, a separator and the two models
        QVERIFY(model->itemText(2).contains(QLatin1String("no tool calls")));
        QCOMPARE(model->currentText(), QStringLiteral("Qwen 27B – 27B Q6_K, %1 GB").arg(QLocale().toString(23.4, 'f', 1)));
        QCOMPARE(context->value(), 65536);
        QVERIFY(state->isHidden());
        QJsonObject config = openCodeConfig(environment->toPlainText());
        QCOMPARE(config.value(QLatin1String("model")).toString(), QStringLiteral("lmstudio/qwen"));
        const QJsonObject provider = config.value(QLatin1String("provider")).toObject().value(QLatin1String("lmstudio")).toObject();
        QCOMPARE(provider.value(QLatin1String("options")).toObject().value(QLatin1String("baseURL")).toString(), QStringLiteral("http://127.0.0.1:1/v1"));
        QJsonObject entry = provider.value(QLatin1String("models")).toObject().value(QLatin1String("qwen")).toObject();
        QCOMPARE(entry.value(QLatin1String("limit")).toObject().value(QLatin1String("context")).toInt(), 65536);
        QVERIFY(entry.value(QLatin1String("tool_call")).toBool());
        QVERIFY(entry.contains(QLatin1String("modalities")));

        // Another model: name, context length and configuration follow; a warning for it.
        model->setCurrentIndex(model->findData(QStringLiteral("alpha-chat")));
        QCOMPARE(name->text(), QStringLiteral("Alpha Chat (local)"));
        QCOMPARE(context->value(), 32768);
        QVERIFY(!state->isHidden());
        QVERIFY(state->text().contains(QLatin1String("not trained for tool calls")));
        config = openCodeConfig(environment->toPlainText());
        QCOMPARE(config.value(QLatin1String("model")).toString(), QStringLiteral("lmstudio/alpha-chat"));
        QCOMPARE(environment->toPlainText().count(QLatin1String("OPENCODE_CONFIG_CONTENT=")), 1);

        // A name of the user's own stays; the context length goes into the configuration.
        model->setCurrentIndex(model->findData(QStringLiteral("qwen")));
        name->setText(QStringLiteral("My Qwen"));
        context->setValue(40960);
        model->setCurrentIndex(model->findData(QStringLiteral("qwen")));
        QCOMPARE(name->text(), QStringLiteral("My Qwen"));
        entry = openCodeConfig(environment->toPlainText())
                    .value(QLatin1String("provider"))
                    .toObject()
                    .value(QLatin1String("lmstudio"))
                    .toObject()
                    .value(QLatin1String("models"))
                    .toObject()
                    .value(QLatin1String("qwen"))
                    .toObject();
        QCOMPARE(entry.value(QLatin1String("limit")).toObject().value(QLatin1String("context")).toInt(), 40960);
        checked.append(QStringLiteral("done"));
        dialog->accept();
    });
    local->trigger();
    QCOMPARE(checked, QStringList{QStringLiteral("done")});
    QCOMPARE(page.agents().size(), 1);
    const WhaleAiAgent agent = page.agents().first();
    QCOMPARE(agent.id, QStringLiteral("my-qwen"));
    QCOMPARE(agent.localProvider, QStringLiteral("lmstudio"));
    QCOMPARE(agent.localModel, QStringLiteral("qwen"));
    QCOMPARE(agent.localContextLength, 40960);
    QCOMPARE(openCodeConfig(agent.environment.join(QLatin1Char('\n'))).value(QLatin1String("model")).toString(), QStringLiteral("lmstudio/qwen"));

    // Edited: no local model any more, the rest stays as it is.
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<WhaleAiAgentDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto close = qScopeGuard([dialog] {
            if (dialog->isVisible()) {
                dialog->reject();
            }
        });
        auto *model = dialog->findChild<QComboBox *>(QStringLiteral("localModel"));
        QCOMPARE(model->currentData().toString(), QStringLiteral("qwen"));
        QCOMPARE(dialog->findChild<QSpinBox *>(QStringLiteral("contextLength"))->value(), 40960);
        model->setCurrentIndex(0);
        QVERIFY(!dialog->findChild<QSpinBox *>(QStringLiteral("contextLength"))->isEnabled());
        dialog->accept();
    });
    child<QListWidget>(page, QStringLiteral("agents"))->setCurrentRow(0);
    child<QPushButton>(page, QStringLiteral("edit"))->click();
    QVERIFY(!page.agents().first().usesLocalModel());
    QCOMPARE(page.agents().first().localContextLength, 0);
    QCOMPARE(page.agents().first().environment, agent.environment);
}

void WhaleAiSettingsPageTest::localModelOfOwnConfig()
{
    // An agent with an OpenCode configuration of the user's own (and a program in npx' cache,
    // as an agent once proposed it): the configuration is not touched, the dialog says so.
    QTemporaryDir lmStudio;
    WhaleAiAgent own{QStringLiteral("own"),
                     QStringLiteral("Own"),
                     QStringLiteral("/home/u/.npm/_npx/e2094862b59aac7b/node_modules/.bin/opencode acp"),
                     {QStringLiteral("OPENCODE_CONFIG_CONTENT={\"model\":\"mine/x\"}"), QStringLiteral("A=1")},
                     {}};
    WhaleAiAgents::setAgents({own});
    WhaleAiSettingsPage page;
    page.setLocalModelOptions(lmStudioOptions(lmStudio.path()));
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<WhaleAiAgentDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto close = qScopeGuard([dialog] {
            if (dialog->isVisible()) {
                dialog->reject();
            }
        });
        auto *model = dialog->findChild<QComboBox *>(QStringLiteral("localModel"));
        auto *state = dialog->findChild<QLabel *>(QStringLiteral("localState"));
        QVERIFY(state->text().contains(QLatin1String("npx' cache")));
        model->setCurrentIndex(model->findData(QStringLiteral("qwen")));
        QCOMPARE(dialog->findChild<QLineEdit *>(QStringLiteral("name"))->text(), QStringLiteral("Own"));
        QVERIFY(state->text().contains(QLatin1String("your own")));
        QCOMPARE(dialog->findChild<QPlainTextEdit *>(QStringLiteral("environment"))->toPlainText(),
                 QStringLiteral("OPENCODE_CONFIG_CONTENT={\"model\":\"mine/x\"}\nA=1"));
        dialog->accept();
    });
    child<QListWidget>(page, QStringLiteral("agents"))->setCurrentRow(0);
    child<QPushButton>(page, QStringLiteral("edit"))->click();
    QCOMPARE(page.agents().first().localModel, QStringLiteral("qwen"));
    QCOMPARE(page.agents().first().environment, own.environment);
}

void WhaleAiSettingsPageTest::addStrataModel()
{
    // AI13d: Strata's sizes next to LM Studio's models; the context length is its config's.
    QTemporaryDir home;
    WhaleAiLocalModels::Options options = lmStudioOptions(home.path());
    auto write = [](const QString &path, const QByteArray &content) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    };
    const QString strata = home.filePath(QStringLiteral("Strata"));
    write(strata + QStringLiteral("/serve/server.py"), "");
    write(strata + QStringLiteral("/.venv/bin/python"), "#!/bin/sh\n");
    QFile::setPermissions(strata + QStringLiteral("/.venv/bin/python"), QFile::ReadOwner | QFile::ExeOwner);
    write(strata + QStringLiteral("/strata-iq2_xs.json"),
          R"({"model_name": "qwen3.8-flash-next-iq2_xs", "port": 8099, "args": ["--max-context", "262144", "--vision"], "log": "/nonexistent/a.log"})");
    write(strata + QStringLiteral("/strata-iq3_s.json"), R"({"model_name": "qwen3.8-flash-next-iq3_s", "port": 8099, "args": ["--max-context", "65536"]})");
    // The size that ran last (its log is newest) is offered first.
    write(home.filePath(QStringLiteral("iq3.log")), "x");
    write(strata + QStringLiteral("/strata-iq3_s.json"),
          QStringLiteral(R"({"model_name": "qwen3.8-flash-next-iq3_s", "port": 8099, "args": ["--max-context", "65536"], "log": "%1"})")
              .arg(home.filePath(QStringLiteral("iq3.log")))
              .toUtf8());

    WhaleAiSettingsPage page;
    page.setLocalModelOptions(options);
    QAction *local = menuAction(page, QStringLiteral("localModel-strata"));
    QVERIFY(local);
    QVERIFY(local->isEnabled());
    QCOMPARE(local->text(), QStringLiteral("Local Model (Strata)…"));

    QStringList checked;
    QTimer::singleShot(0, [&checked] {
        auto *dialog = qobject_cast<WhaleAiAgentDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto close = qScopeGuard([dialog] {
            if (dialog->isVisible()) {
                dialog->reject();
            }
        });
        auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("name"));
        auto *model = dialog->findChild<QComboBox *>(QStringLiteral("localModel"));
        auto *context = dialog->findChild<QSpinBox *>(QStringLiteral("contextLength"));
        auto *environment = dialog->findChild<QPlainTextEdit *>(QStringLiteral("environment"));
        // Both providers in the list: None, LM Studio's two, Strata's two (with separators).
        QCOMPARE(model->count(), 7);
        QCOMPARE(model->currentData(Qt::UserRole).toString(), QStringLiteral("qwen3.8-flash-next-iq3_s"));
        QCOMPARE(name->text(), QStringLiteral("Strata IQ3_S (local)"));
        QVERIFY(!context->isEnabled());
        QCOMPARE(context->value(), 65536);
        QJsonObject config = openCodeConfig(environment->toPlainText());
        QCOMPARE(config.value(QLatin1String("model")).toString(), QStringLiteral("strata/qwen3.8-flash-next-iq3_s"));
        const QJsonObject provider = config.value(QLatin1String("provider")).toObject().value(QLatin1String("strata")).toObject();
        QCOMPARE(provider.value(QLatin1String("name")).toString(), QStringLiteral("Strata"));
        QCOMPARE(provider.value(QLatin1String("options")).toObject().value(QLatin1String("baseURL")).toString(), QStringLiteral("http://127.0.0.1:8099/v1"));

        // The other size; then an LM Studio model: the configuration follows, it is Orcinus' own.
        int iq2 = -1;
        for (int i = 0; i < model->count(); ++i) {
            if (model->itemData(i, Qt::UserRole).toString() == QLatin1String("qwen3.8-flash-next-iq2_xs")) {
                iq2 = i;
            }
        }
        model->setCurrentIndex(iq2);
        QCOMPARE(context->value(), 262144);
        QCOMPARE(name->text(), QStringLiteral("Strata IQ2_XS (local)"));
        model->setCurrentIndex(model->findData(QStringLiteral("qwen")));
        QVERIFY(context->isEnabled());
        QCOMPARE(context->value(), 65536);
        config = openCodeConfig(environment->toPlainText());
        QCOMPARE(config.value(QLatin1String("model")).toString(), QStringLiteral("lmstudio/qwen"));
        QCOMPARE(environment->toPlainText().count(QLatin1String("OPENCODE_CONFIG_CONTENT=")), 1);
        model->setCurrentIndex(iq2);
        QCOMPARE(openCodeConfig(environment->toPlainText()).value(QLatin1String("model")).toString(), QStringLiteral("strata/qwen3.8-flash-next-iq2_xs"));
        checked.append(QStringLiteral("done"));
        dialog->accept();
    });
    local->trigger();
    QCOMPARE(checked, QStringList{QStringLiteral("done")});
    const WhaleAiAgent agent = page.agents().first();
    QCOMPARE(agent.localProvider, QStringLiteral("strata"));
    QCOMPARE(agent.localModel, QStringLiteral("qwen3.8-flash-next-iq2_xs"));
    QCOMPARE(agent.localContextLength, 0);

    // Edited again: the Strata size is chosen, its length shown.
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<WhaleAiAgentDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QComboBox *>(QStringLiteral("localModel"))->currentData(Qt::UserRole + 1).toString(), QStringLiteral("strata"));
        QCOMPARE(dialog->findChild<QSpinBox *>(QStringLiteral("contextLength"))->value(), 262144);
        dialog->accept();
    });
    child<QListWidget>(page, QStringLiteral("agents"))->setCurrentRow(0);
    child<QPushButton>(page, QStringLiteral("edit"))->click();
    QCOMPARE(page.agents().first(), agent);
}

void WhaleAiSettingsPageTest::freeDelays()
{
    // AI13: how long a closed panel keeps the local model, per provider; the defaults restore them.
    WhaleAiSettingsPage page;
    auto *lmStudio = child<QSpinBox>(page, QStringLiteral("freeDelay-lmstudio"));
    auto *strata = child<QSpinBox>(page, QStringLiteral("freeDelay-strata"));
    QVERIFY(lmStudio);
    QVERIFY(strata);
    // LM Studio in seconds, Strata in minutes (stored in seconds).
    QCOMPARE(lmStudio->value(), 3);
    QCOMPARE(lmStudio->suffix(), QStringLiteral(" s"));
    QCOMPARE(strata->value(), 10);
    QCOMPARE(strata->suffix(), QStringLiteral(" min"));
    QSignalSpy changed(&page, &WhaleAiSettingsPage::changed);
    strata->setValue(20);
    lmStudio->setValue(0);
    QCOMPARE(changed.size(), 2);
    QCOMPARE(lmStudio->text(), QStringLiteral("At once"));
    page.applySettings();
    QCOMPARE(WhaleAiAgents::freeDelaySeconds(QStringLiteral("strata")), 1200.0);
    QCOMPARE(WhaleAiAgents::freeDelaySeconds(QStringLiteral("lmstudio")), 0.0);
    page.restoreDefaults();
    QCOMPARE(lmStudio->value(), 3);
    QCOMPARE(strata->value(), 10);
    page.applySettings();
    QCOMPARE(WhaleAiAgents::freeDelaySeconds(QStringLiteral("strata")), 600.0);
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

void WhaleAiSettingsPageTest::allowAdmin()
{
    // On by default: each command is confirmed by the user anyway.
    QVERIFY(WhaleAiAgents::allowAdmin());
    QVERIFY(WhaleAiAgents::adminGuidance(true).contains(QLatin1String("pkexec <command>")));
    QVERIFY(!WhaleAiAgents::adminGuidance(false).contains(QLatin1String("pkexec")));
    {
        WhaleAiSettingsPage page;
        auto *allow = child<QCheckBox>(page, QStringLiteral("allowAdmin"));
        QVERIFY(allow);
        QVERIFY(allow->isChecked());
        QSignalSpy changed(&page, &WhaleAiSettingsPage::changed);
        allow->setChecked(false);
        QCOMPARE(changed.count(), 1);
        page.applySettings();
    }
    QVERIFY(!WhaleAiAgents::allowAdmin());
    WhaleAiSettingsPage page;
    QVERIFY(!child<QCheckBox>(page, QStringLiteral("allowAdmin"))->isChecked());
    page.restoreDefaults();
    page.applySettings();
    QVERIFY(WhaleAiAgents::allowAdmin());
}

void WhaleAiSettingsPageTest::conversationLanguage()
{
    // Same as Orcinus by default: the agent hears Orcinus' language, nothing about how to talk.
    QVERIFY(WhaleAiAgents::conversationLanguage().isEmpty());
    QCOMPARE(WhaleAiAgents::languageGuidance(QStringLiteral("en_US"), QString()),
             QStringLiteral("Orcinus' user interface is in English: name its menus, buttons and settings as they appear there."));
    QVERIFY(WhaleAiAgents::languageGuidance(QStringLiteral("en_US"), QStringLiteral("de")).contains(QLatin1String("Talk to the user in German")));
    QVERIFY(WhaleAiAgents::availableConversationLanguages().contains(QLatin1String("en_US")));
    {
        WhaleAiSettingsPage page;
        auto *language = child<QComboBox>(page, QStringLiteral("conversationLanguage"));
        QVERIFY(language);
        QCOMPARE(language->currentIndex(), 0);
        QCOMPARE(language->currentData().toString(), QString());
        QSignalSpy changed(&page, &WhaleAiSettingsPage::changed);
        language->setCurrentIndex(language->findData(QStringLiteral("en_US")));
        QCOMPARE(changed.count(), 1);
        page.applySettings();
    }
    QCOMPARE(WhaleAiAgents::conversationLanguage(), QStringLiteral("en_US"));
    WhaleAiSettingsPage page;
    QCOMPARE(child<QComboBox>(page, QStringLiteral("conversationLanguage"))->currentData().toString(), QStringLiteral("en_US"));
    page.restoreDefaults();
    page.applySettings();
    QVERIFY(WhaleAiAgents::conversationLanguage().isEmpty());
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

void WhaleAiSettingsPageTest::memoryGroup()
{
    QTemporaryDir dir;
    const WhaleAiMemory memory(dir.filePath(QStringLiteral("memory.json")));
    QVERIFY(memory.add(QStringLiteral("I like order."), QString(), QString(), true));
    QVERIFY(memory.add(QStringLiteral("Gone folder note."), dir.filePath(QStringLiteral("gone")), QString(), false));
    WhaleAiAgents::setMemoryMode(WhaleAiAgents::MemoryMode::Manual);

    WhaleAiMemoryWidget widget(memory);
    auto *mode = widget.findChild<QComboBox *>(QStringLiteral("memoryMode"));
    auto *list = widget.findChild<QTreeWidget *>(QStringLiteral("notes"));
    QCOMPARE(WhaleAiAgents::MemoryMode(mode->currentData().toInt()), WhaleAiAgents::MemoryMode::Manual);
    QCOMPARE(list->topLevelItemCount(), 2);
    QCOMPARE(list->topLevelItem(0)->text(0), QStringLiteral("Global"));
    QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("I like order."));
    QVERIFY(list->topLevelItem(1)->text(0).endsWith(QLatin1String("(gone)")));

    // The mode only with apply.
    QSignalSpy changed(&widget, &WhaleAiMemoryWidget::changed);
    mode->setCurrentIndex(mode->findData(int(WhaleAiAgents::MemoryMode::Automatic)));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(WhaleAiAgents::memoryMode(), WhaleAiAgents::MemoryMode::Manual);
    widget.apply();
    QCOMPARE(WhaleAiAgents::memoryMode(), WhaleAiAgents::MemoryMode::Automatic);
    widget.restoreDefaults();
    QCOMPARE(WhaleAiAgents::MemoryMode(mode->currentData().toInt()), WhaleAiAgents::MemoryMode::Manual);

    // Notes at once: add (a folder note), edit (made global), delete.
    QTimer::singleShot(0, [&dir] {
        auto *dialog = qobject_cast<WhaleAiNoteDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        QVERIFY(!ok->isEnabled()); // no text yet
        dialog->findChild<QRadioButton *>(QStringLiteral("inFolder"))->setChecked(true);
        dialog->findChild<QLineEdit *>(QStringLiteral("folder"))->setText(QStringLiteral("relative"));
        dialog->findChild<QPlainTextEdit *>(QStringLiteral("text"))->setPlainText(QStringLiteral("Per year."));
        QVERIFY(!ok->isEnabled()); // not an absolute folder
        dialog->findChild<QLineEdit *>(QStringLiteral("folder"))->setText(dir.path() + QStringLiteral("/"));
        QVERIFY(ok->isEnabled());
        dialog->findChild<QPlainTextEdit *>(QStringLiteral("text"))->setPlainText(QString(501, QLatin1Char('x')));
        QVERIFY(!ok->isEnabled()); // too long
        dialog->findChild<QPlainTextEdit *>(QStringLiteral("text"))->setPlainText(QStringLiteral("Per year."));
        ok->click();
    });
    widget.findChild<QPushButton *>(QStringLiteral("addNote"))->click();
    QCOMPARE(memory.notes().size(), 3);
    QCOMPARE(memory.notes().constLast().folder, QDir::cleanPath(dir.path()));
    QVERIFY(!memory.notes().constLast().byAgent);
    QCOMPARE(list->topLevelItemCount(), 3);

    list->setCurrentItem(list->topLevelItem(2));
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<WhaleAiNoteDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->text(), QStringLiteral("Per year."));
        QVERIFY(dialog->findChild<QRadioButton *>(QStringLiteral("inFolder"))->isChecked());
        dialog->findChild<QRadioButton *>(QStringLiteral("global"))->setChecked(true);
        dialog->findChild<QPlainTextEdit *>(QStringLiteral("text"))->setPlainText(QStringLiteral("One folder per year."));
        dialog->accept();
    });
    widget.findChild<QPushButton *>(QStringLiteral("editNote"))->click();
    QCOMPARE(memory.notes().constLast().text, QStringLiteral("One folder per year."));
    QVERIFY(memory.notes().constLast().isGlobal());
    QCOMPARE(list->currentItem()->text(1), QStringLiteral("One folder per year."));

    widget.findChild<QPushButton *>(QStringLiteral("removeNote"))->click();
    QCOMPARE(memory.notes().size(), 2);
    QCOMPARE(list->topLevelItemCount(), 2);
}

#include "whaleaisettingspagetest.moc"
