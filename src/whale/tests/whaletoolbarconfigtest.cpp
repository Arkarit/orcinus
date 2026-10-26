/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewcontainer.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalefileviewtoolbar.h"
#include "whalefileviewtoolbarclient.h"
#include "whalelocationbar.h"
#include "whaletoolbarconfigurator.h"
#include "whaletoolbarlayout.h"
#include "whaletoolbarmanager.h"
#include "whaleviewcommandscope.h"

#include <KActionCollection>
#include <KEditToolBar>
#include <KLocalizedString>
#include <KStandardAction>
#include <KToolBar>
#include <KXMLGUIFactory>

#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDomDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

namespace
{
const QString mainName = QStringLiteral("Main Toolbar");
const QString fileViewName = QStringLiteral("File View Toolbar");
const QString separatorText = QStringLiteral("--- separator ---");
const QString spacerText = QStringLiteral("--- expanding spacer ---");
const QStringList defaultLayout = {QStringLiteral("go_back"),
                                   QStringLiteral("go_forward"),
                                   QStringLiteral("go_up"),
                                   QStringLiteral("view_redisplay"),
                                   QStringLiteral("whale_location_bar"),
                                   QStringLiteral("view_settings")};
const QStringList defaultMainLayout = {QStringLiteral("|"),
                                       QStringLiteral("split_view"),
                                       QStringLiteral("split_stash"),
                                       QStringLiteral("toggle_search"),
                                       QStringLiteral("hamburger_menu")};

QString localFile(const QString &name)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kxmlgui5/") + QStringLiteral(WHALE_COMPONENT)
        + QLatin1Char('/') + name;
}

QString localFileViewFile()
{
    return localFile(WhaleFileViewToolbarClient::rcFile());
}

QString localMainFile()
{
    return localFile(QStringLiteral("dolphinui.rc"));
}

// The toolbar @p toolBarName as the file @p file defines it; "<no file>" if there is none.
QStringList savedLayout(const QString &file, const QString &toolBarName)
{
    QFile local(file);
    if (!local.open(QIODevice::ReadOnly)) {
        return {QStringLiteral("<no file>")};
    }
    QDomDocument document;
    document.setContent(&local);
    return WhaleToolbarLayout::fromDocument(document, toolBarName).toStringList();
}

QByteArray contentOf(const QString &file)
{
    QFile local(file);
    return local.open(QIODevice::ReadOnly) ? local.readAll() : QByteArray();
}

QStringList savedFileViewLayout()
{
    return savedLayout(localFileViewFile(), WhaleFileViewToolbarClient::toolBarName());
}

QStringList savedMainLayout()
{
    return savedLayout(localMainFile(), QStringLiteral("mainToolBar"));
}

QStringList textsOf(const QListWidget *list)
{
    QStringList result;
    for (int i = 0; i < list->count(); ++i) {
        result.append(list->item(i)->text());
    }
    return result;
}

/**
 * The widgets of Configure Toolbars (KEditToolBar keeps them private).
 */
class Editor
{
public:
    explicit Editor(KEditToolBar *dialog)
        : m_dialog(dialog)
    {
    }

    QComboBox *combo() const
    {
        return m_dialog->findChild<QComboBox *>();
    }

    // "Available actions" and "Current actions", in the order KEditToolBar creates them.
    QListWidget *available() const
    {
        return m_dialog->findChildren<QListWidget *>().value(0);
    }

    QListWidget *current() const
    {
        return m_dialog->findChildren<QListWidget *>().value(1);
    }

    QPushButton *button(QDialogButtonBox::StandardButton which) const
    {
        return m_dialog->findChild<QDialogButtonBox *>()->button(which);
    }

    QStringList toolBars() const
    {
        QStringList result;
        for (int i = 0; i < combo()->count(); ++i) {
            result.append(combo()->itemText(i));
        }
        return result;
    }

    bool select(const QString &toolBar) const
    {
        const int index = combo()->findText(toolBar);
        if (index < 0) {
            return false;
        }
        combo()->setCurrentIndex(index);
        Q_EMIT combo()->activated(index); // what choosing an entry does
        return true;
    }

    // Moves the entry with the text @p text to the end of "Current actions" (or behind the
    // entry inserted last).
    bool insert(const QString &text) const
    {
        const auto items = available()->findItems(text, Qt::MatchExactly);
        if (items.count() != 1) {
            return false;
        }
        available()->setCurrentItem(items.first());
        Q_EMIT available()->itemDoubleClicked(items.first());
        return true;
    }

    bool remove(const QString &text) const
    {
        const auto items = current()->findItems(text, Qt::MatchExactly);
        if (items.isEmpty()) {
            return false;
        }
        current()->setCurrentItem(items.first());
        Q_EMIT current()->itemDoubleClicked(items.first());
        return true;
    }

    // "Change Text…" for the entry @p text of "Current actions".
    bool changeText(const QString &text, const QString &newText) const
    {
        const auto items = current()->findItems(text, Qt::MatchExactly);
        if (items.count() != 1) {
            return false;
        }
        current()->setCurrentItem(items.first());
        QPushButton *changeText = nullptr;
        const auto buttons = m_dialog->findChildren<QPushButton *>();
        for (QPushButton *button : buttons) {
            if (button->icon().name() == QLatin1String("edit-rename")) {
                changeText = button;
            }
        }
        if (!changeText || !changeText->isEnabled()) {
            return false;
        }
        bool answered = false;
        QTimer::singleShot(100, m_dialog, [&answered, newText]() {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            auto edit = dialog ? dialog->findChild<QLineEdit *>() : nullptr;
            if (edit) {
                edit->setText(newText);
                answered = true;
                dialog->accept();
            } else if (dialog) {
                dialog->reject();
            }
        });
        changeText->click();
        return answered;
    }

    // "Defaults" and the answer to KDE's question; @p question receives its text.
    void defaults(bool confirm, QString *question = nullptr) const
    {
        QTimer::singleShot(100, m_dialog, [confirm, question]() {
            auto box = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!box) {
                return;
            }
            if (question) {
                const auto labels = box->findChildren<QLabel *>();
                for (const QLabel *label : labels) {
                    *question += label->text();
                }
            }
            auto buttons = box->findChild<QDialogButtonBox *>();
            QAbstractButton *answer = buttons ? buttons->button(confirm ? QDialogButtonBox::Yes : QDialogButtonBox::Cancel) : nullptr;
            if (answer) {
                answer->click();
            } else {
                box->reject();
            }
        });
        button(QDialogButtonBox::RestoreDefaults)->click();
    }

    void apply() const
    {
        button(QDialogButtonBox::Apply)->click();
    }

    void ok() const
    {
        button(QDialogButtonBox::Ok)->click();
    }

    void cancel() const
    {
        button(QDialogButtonBox::Cancel)->click();
    }

private:
    KEditToolBar *m_dialog;
};
}

/**
 * Configure Toolbars with the File View Toolbar (multi-toolbar plan, TB4): KDE's dialog
 * edits the main toolbar and the File View Toolbar; the client of the latter is part of the
 * window's XML GUI factory only while the dialog is open.
 */
class WhaleToolbarConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testActionPropertiesFromDocument();
    void testDialogListsBothToolBars();
    void testAvailableActions();
    void testApplyFileViewToolBar();
    void testApplyBothToolBars();
    void testApplyTwice();
    void testCancel();
    void testApplyThenCancel();
    void testOk();
    void testSeparatorAndSpacer();
    void testNewWindowReadsLayout();
    void testTwoWindows();
    void testChangeText();
    void testChangeIcon();
    void testDefaults();
    void testOpenAndCloseRepeatedly();
    void testClosingWindowWithDialog();
    void testNoShortcuts();
    void testFindActionWhileConfiguring();
    void testContextMenu();
    void testMainToolBarStillWorks();
    void testLocationBar();

private:
    DolphinViewContainer *showWindow(DolphinMainWindow *window);
    void showSplit();
    DolphinTabPage *tabPage() const;
    KEditToolBar *configure(DolphinMainWindow *window, const QString &descriptorId);
    static WhaleFileViewToolBar *toolBar(DolphinViewContainer *container);
    static QAction *local(DolphinViewContainer *container, const QString &name);
    static QStringList layoutOf(const QToolBar *toolBar);
    static QList<KXMLGUIClient *> editClients(DolphinMainWindow *window);
    static QString prototypeText(DolphinMainWindow *window, const QString &name);
    static void writeUserFile(const QByteArray &toolBarContent, const QByteArray &actionProperties = {});
    QAction *global(const QString &name) const;

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    DolphinViewContainer *m_first = nullptr;
    DolphinViewContainer *m_second = nullptr;
};

void WhaleToolbarConfigTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhaleToolbarConfigTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"), QByteArray(3000, 'a'));
    m_dir->createFile(QStringLiteral(".hidden"), QByteArray(100, 'h'));
    m_dir->createFile(QStringLiteral("sub/b.txt"), QByteArray(1000, 'b'));
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleToolbarConfigTest::cleanup()
{
    // Closing a dialog deletes other widgets without a parent: no plain pointers.
    QList<QPointer<QWidget>> windows;
    const auto topLevelWidgets = QApplication::topLevelWidgets();
    for (QWidget *window : topLevelWidgets) {
        windows.append(window);
    }
    for (const QPointer<QWidget> &window : std::as_const(windows)) {
        if (window && qobject_cast<QDialog *>(window) && window->isVisible()) {
            window->close();
        }
    }
    m_mainWindow.reset();
    m_dir.reset();
    m_first = m_second = nullptr;
    // The files of a test (also of a failed one) must not reach the next test.
    QFile::remove(localFileViewFile());
    QFile::remove(localMainFile());
}

DolphinViewContainer *WhaleToolbarConfigTest::showWindow(DolphinMainWindow *window)
{
    window->openDirectories({m_dir->url()}, false);
    window->resize(1200, 700);
    window->show();
    if (!QTest::qWaitForWindowActive(window)) {
        return nullptr;
    }
    DolphinViewContainer *container = window->activeViewContainer();
    container->view()->setHiddenFilesShown(false);
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 2;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

void WhaleToolbarConfigTest::showSplit()
{
    QVERIFY(showWindow(m_mainWindow.data()));
    global(QStringLiteral("split_view"))->trigger();
    m_first = tabPage()->primaryViewContainer();
    m_second = tabPage()->secondaryViewContainer();
    QVERIFY(m_first && m_second);
    QTRY_COMPARE(m_second->view()->itemsCount(), 2);
    QTRY_VERIFY(toolBar(m_first)->scope() && toolBar(m_second)->scope());
}

DolphinTabPage *WhaleToolbarConfigTest::tabPage() const
{
    return m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
}

KEditToolBar *WhaleToolbarConfigTest::configure(DolphinMainWindow *window, const QString &descriptorId)
{
    WhaleToolbarConfigurator *configurator = WhaleToolbarConfigurator::of(window);
    if (!configurator) {
        return nullptr;
    }
    configurator->configure(descriptorId);
    KEditToolBar *dialog = configurator->dialog();
    return dialog && dialog->isVisible() ? dialog : nullptr;
}

WhaleFileViewToolBar *WhaleToolbarConfigTest::toolBar(DolphinViewContainer *container)
{
    return container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly);
}

QAction *WhaleToolbarConfigTest::local(DolphinViewContainer *container, const QString &name)
{
    WhaleViewCommandScope *scope = toolBar(container)->scope();
    return scope ? scope->action(name) : nullptr;
}

QStringList WhaleToolbarConfigTest::layoutOf(const QToolBar *toolBar)
{
    QStringList result;
    const auto actions = toolBar->actions();
    for (QAction *action : actions) {
        if (WhaleFileViewToolBar::isLeadingPlaceholder(action)) {
            continue; // not part of the layout, as the lock of a split view on it
        }
        if (action->isSeparator()) {
            result.append(QStringLiteral("-"));
        } else if (action->objectName().isEmpty()) {
            result.append(QStringLiteral("|")); // the spacer widget
        } else {
            result.append(action->objectName());
        }
    }
    return result;
}

QList<KXMLGUIClient *> WhaleToolbarConfigTest::editClients(DolphinMainWindow *window)
{
    // The clients of the File View Toolbar's file in the factory of the window.
    QList<KXMLGUIClient *> result;
    const auto clients = window->guiFactory()->clients();
    for (KXMLGUIClient *client : clients) {
        if (dynamic_cast<WhaleFileViewToolbarClient *>(client)) {
            result.append(client);
        }
    }
    return result;
}

// The text of the entry of the action @p name in "Available actions" of the File View Toolbar.
QString WhaleToolbarConfigTest::prototypeText(DolphinMainWindow *window, const QString &name)
{
    const auto clients = editClients(window);
    const QAction *prototype = clients.isEmpty() ? nullptr : clients.first()->actionCollection()->action(name);
    return prototype ? KLocalizedString::removeAcceleratorMarker(prototype->text()) : QString();
}

void WhaleToolbarConfigTest::writeUserFile(const QByteArray &toolBarContent, const QByteArray &actionProperties)
{
    const QString file = localFileViewFile();
    QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
    QFile local(file);
    QVERIFY(local.open(QIODevice::WriteOnly));
    local.write(
        "<!DOCTYPE gui SYSTEM \"kpartgui.dtd\">\n<gui name=\"whale_fileview\" version=\"1\">"
        "<ToolBar noMerge=\"1\" name=\"whaleFileViewToolBar\"><text>File View Toolbar</text>"
        + toolBarContent + "</ToolBar>" + (actionProperties.isEmpty() ? QByteArray() : "<ActionProperties>" + actionProperties + "</ActionProperties>")
        + "</gui>");
    local.close();
    // The windows have read the files when they were created.
    WhaleToolbarManager::reloadAllFileViewLayouts();
}

QAction *WhaleToolbarConfigTest::global(const QString &name) const
{
    return m_mainWindow->actionCollection()->action(name);
}

void WhaleToolbarConfigTest::testActionPropertiesFromDocument()
{
    QDomDocument document;
    document.setContent(
        QByteArrayLiteral("<gui name=\"whale_fileview\" version=\"1\">"
                          "<ToolBar name=\"whaleFileViewToolBar\"><Action name=\"go_back\"/></ToolBar>"
                          "<ActionProperties scheme=\"Default\">"
                          "<Action name=\"go_back\" iconText=\"Before\" icon=\"go-first\" priority=\"0\" shortcut=\"F9\" whatever=\"x\"/>"
                          "<Action iconText=\"no name\"/>"
                          "</ActionProperties></gui>"));
    const WhaleToolbarLayout layout = WhaleToolbarLayout::fromDocument(document, WhaleFileViewToolbarClient::toolBarName());
    QCOMPARE(layout.toStringList(), QStringList{QStringLiteral("go_back")});
    const auto properties = layout.actionProperties();
    QCOMPARE(properties.count(), 1);
    // Texts, icon and priority; never a shortcut.
    const WhaleToolbarLayout::ActionProperties expected = {{QStringLiteral("iconText"), QStringLiteral("Before")},
                                                           {QStringLiteral("icon"), QStringLiteral("go-first")},
                                                           {QStringLiteral("priority"), QStringLiteral("0")}};
    QCOMPARE(properties.value(QStringLiteral("go_back")), expected);

    // The properties are part of the layout: a change of them alone is a change.
    QDomDocument plain;
    plain.setContent(QByteArrayLiteral("<gui><ToolBar name=\"whaleFileViewToolBar\"><Action name=\"go_back\"/></ToolBar></gui>"));
    QVERIFY(layout != WhaleToolbarLayout::fromDocument(plain, WhaleFileViewToolbarClient::toolBarName()));
}

void WhaleToolbarConfigTest::testDialogListsBothToolBars()
{
    QVERIFY(showWindow(m_mainWindow.data()));
    const auto clientsBefore = m_mainWindow->guiFactory()->clients();
    QVERIFY(editClients(m_mainWindow.data()).isEmpty());
    WhaleToolbarConfigurator *configurator = WhaleToolbarConfigurator::of(m_mainWindow.data());
    QVERIFY(configurator);
    QVERIFY(!configurator->dialog());

    // The window's "Configure Toolbars…": the main toolbar is chosen.
    global(KStandardAction::name(KStandardAction::ConfigureToolbars))->trigger();
    QPointer<KEditToolBar> dialog = configurator->dialog();
    QVERIFY(dialog && dialog->isVisible());
    QCOMPARE(dialog->parentWidget(), m_mainWindow.data());
    QVERIFY(!dialog->isModal());
    {
        const Editor editor(dialog);
        QVERIFY(editor.combo()->isVisible());
        // Both toolbars, without " <dolphin>" and " <whale_fileview>".
        QCOMPARE(editor.toolBars(), QStringList({mainName, fileViewName}));
        QCOMPARE(editor.combo()->currentText(), mainName);
        QCOMPARE(editClients(m_mainWindow.data()).count(), 1);
        editor.cancel();
    }
    QVERIFY(!configurator->dialog());
    QCOMPARE(m_mainWindow->guiFactory()->clients(), clientsBefore);
    QTRY_VERIFY(!dialog);

    // For the File View Toolbar.
    dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    {
        const Editor editor(dialog);
        QCOMPARE(editor.toolBars(), QStringList({mainName, fileViewName}));
        QCOMPARE(editor.combo()->currentText(), fileViewName);
        // The texts are cosmetics; the list still chooses the toolbars.
        QVERIFY(editor.select(mainName));
        QVERIFY(textsOf(editor.current()).contains(KLocalizedString::removeAcceleratorMarker(global(QStringLiteral("split_view"))->iconText())));
        QVERIFY(editor.select(fileViewName));
        QVERIFY(!textsOf(editor.current()).contains(KLocalizedString::removeAcceleratorMarker(global(QStringLiteral("split_view"))->iconText())));

        // Asking again while it is open: the same dialog, no second client.
        configurator->configure(WhaleToolbarDescriptor::globalId());
        QCOMPARE(configurator->dialog(), dialog.data());
        QCOMPARE(editClients(m_mainWindow.data()).count(), 1);
        editor.cancel();
    }
    QCOMPARE(m_mainWindow->guiFactory()->clients(), clientsBefore);
    // No window toolbar was added for the File View Toolbar.
    QCOMPARE(m_mainWindow->toolBars().count(), 1);
}

void WhaleToolbarConfigTest::testAvailableActions()
{
    QVERIFY(showWindow(m_mainWindow.data()));
    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    const Editor editor(dialog);

    // The File View Toolbar: exactly the allowed actions (the one definition), as prototypes.
    const auto clients = editClients(m_mainWindow.data());
    QCOMPARE(clients.count(), 1);
    KActionCollection *prototypes = clients.first()->actionCollection();
    QStringList names;
    const auto prototypeActions = prototypes->actions();
    for (const QAction *prototype : prototypeActions) {
        names.append(prototype->objectName());
        if (prototype->objectName() == QLatin1String("whale_location_bar")) {
            // No action of the window describes the location bar of a view.
            QVERIFY(!global(prototype->objectName()));
            QCOMPARE(prototype->text(), QStringLiteral("Location Bar"));
            QCOMPARE(prototype->icon().name(), QStringLiteral("edit-select-text"));
            continue;
        }
        // The texts and icons of the window's actions, but objects of their own ...
        const QAction *window = global(prototype->objectName());
        QVERIFY2(window, qPrintable(prototype->objectName()));
        QVERIFY(prototype != window);
        QCOMPARE(prototype->text(), window->text());
        QCOMPARE(prototype->iconText(), window->iconText());
        QCOMPARE(prototype->toolTip(), window->toolTip());
        QCOMPARE(prototype->icon().name(), window->icon().name());
        // ... and not those of a file view either.
        QVERIFY(prototype != local(m_mainWindow->activeViewContainer(), prototype->objectName()));
    }
    QStringList allowed = WhaleViewCommandScope::allowedActions() << QStringLiteral("whale_location_bar");
    names.sort();
    allowed.sort();
    QCOMPARE(names, allowed);
    QCOMPARE(WhaleToolbarManager::of(m_mainWindow.data())->descriptor(WhaleToolbarDescriptor::fileViewId()).allowedActions.count(), names.count());

    // What the dialog shows: separator, spacer and the actions that are not in the toolbar ...
    QStringList available = textsOf(editor.available());
    QCOMPARE(available.value(0), separatorText);
    QCOMPARE(available.value(1), spacerText);
    available = available.mid(2);
    // ... and the toolbar (the default layout).
    QStringList current = textsOf(editor.current());
    QCOMPARE(current.count(), defaultLayout.count());
    QCOMPARE(current.value(4), QStringLiteral("Location Bar"));
    QCOMPARE(available.count() + current.count(), allowed.count());
    QStringList shown = available;
    QStringList expected;
    for (const QAction *prototype : prototypeActions) {
        if (defaultLayout.contains(prototype->objectName())) {
            shown.append(KLocalizedString::removeAcceleratorMarker(prototype->iconText()));
            expected.append(KLocalizedString::removeAcceleratorMarker(prototype->iconText()));
        } else {
            expected.append(KLocalizedString::removeAcceleratorMarker(prototype->text()));
        }
    }
    shown.sort();
    expected.sort();
    QCOMPARE(shown, expected);

    // Actions of the window only are not offered.
    QVERIFY(!global(QStringLiteral("url_navigators"))); // no global location bars (L5)
    // (toggle_search is one of the view's since E8 was revised: the search bar of that view.)
    const QStringList excluded = {QStringLiteral("split_view"),
                                  QStringLiteral("hamburger_menu"),
                                  QStringLiteral("edit_cut"),
                                  QStringLiteral("edit_paste"),
                                  QStringLiteral("create_dir"),
                                  QStringLiteral("basic_actions"),
                                  QStringLiteral("open_terminal"),
                                  QStringLiteral("new_tab"),
                                  KStandardAction::name(KStandardAction::Preferences)};
    for (const QString &name : excluded) {
        QVERIFY2(global(name), qPrintable(name));
        QVERIFY2(!prototypes->action(name), qPrintable(name));
        const QString text = KLocalizedString::removeAcceleratorMarker(global(name)->text());
        QVERIFY2(!available.contains(text), qPrintable(name));
    }

    // The main toolbar: the window's actions, as ever.
    QVERIFY(editor.select(mainName));
    const QStringList mainAvailable = textsOf(editor.available());
    const QStringList mainCurrent = textsOf(editor.current());
    QCOMPARE(mainAvailable.value(0), separatorText);
    QCOMPARE(mainAvailable.value(1), spacerText);
    QCOMPARE(mainCurrent.count(), defaultMainLayout.count());
    QCOMPARE(mainCurrent.value(0), spacerText);
    QCOMPARE(mainAvailable.count() - 2 + mainCurrent.count() - 1, m_mainWindow->actionCollection()->count());
    for (const QString &name : excluded) {
        const QAction *action = global(name);
        const QString text = KLocalizedString::removeAcceleratorMarker(defaultMainLayout.contains(name) ? action->iconText() : action->text());
        QVERIFY2((defaultMainLayout.contains(name) ? mainCurrent : mainAvailable).contains(text), qPrintable(name));
    }
    editor.cancel();
}

void WhaleToolbarConfigTest::testApplyFileViewToolBar()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QCOMPARE(layoutOf(toolBar(m_first)), defaultLayout);
    QCOMPARE(layoutOf(toolBar(m_second)), defaultLayout);
    QPointer<WhaleViewCommandScope> firstScope = toolBar(m_first)->scope();
    QPointer<WhaleViewCommandScope> secondScope = toolBar(m_second)->scope();
    const QStringList mainBefore = layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar")));

    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    const Editor editor(dialog);
    QVERIFY(!editor.button(QDialogButtonBox::Apply)->isEnabled());
    QVERIFY(editor.insert(prototypeText(m_mainWindow.data(), QStringLiteral("go_home"))));
    QVERIFY(editor.insert(prototypeText(m_mainWindow.data(), QStringLiteral("show_hidden_files"))));
    QVERIFY(editor.button(QDialogButtonBox::Apply)->isEnabled());
    // Nothing happens before Apply.
    QVERIFY(!QFile::exists(localFileViewFile()));
    QCOMPARE(layoutOf(toolBar(m_first)), defaultLayout);

    QSignalSpy changed(WhaleToolbarManager::of(m_mainWindow.data()), &WhaleToolbarManager::fileViewLayoutChanged);
    editor.apply();
    QVERIFY(dialog->isVisible());
    QVERIFY(!editor.button(QDialogButtonBox::Apply)->isEnabled());
    const QStringList expected = defaultLayout + QStringList({QStringLiteral("go_home"), QStringLiteral("show_hidden_files")});
    QCOMPARE(savedFileViewLayout(), expected);
    QCOMPARE(changed.count(), 1);
    // The toolbars of both file views at once, with their own actions as before.
    QCOMPARE(layoutOf(toolBar(m_first)), expected);
    QCOMPARE(layoutOf(toolBar(m_second)), expected);
    QCOMPARE(toolBar(m_first)->scope(), firstScope.data());
    QCOMPARE(toolBar(m_second)->scope(), secondScope.data());
    QAction *firstHidden = local(m_first, QStringLiteral("show_hidden_files"));
    QAction *secondHidden = local(m_second, QStringLiteral("show_hidden_files"));
    QVERIFY(firstHidden && secondHidden && firstHidden != secondHidden);
    QVERIFY(toolBar(m_first)->actions().contains(firstHidden));
    QVERIFY(!toolBar(m_first)->actions().contains(secondHidden));
    QVERIFY(toolBar(m_second)->actions().contains(secondHidden));
    QVERIFY(firstHidden != global(QStringLiteral("show_hidden_files")));
    // No toolbar shows a prototype.
    const auto clients = editClients(m_mainWindow.data());
    QCOMPARE(clients.count(), 1);
    QVERIFY(!toolBar(m_first)->actions().contains(clients.first()->actionCollection()->action(QStringLiteral("show_hidden_files"))));

    // The new buttons act on their own view.
    GeneralSettings::self()->setGlobalViewProps(false);
    m_second->setUrl(QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    QTRY_COMPARE(m_second->view()->itemsCount(), 1);
    QVERIFY(!m_first->view()->hiddenFilesShown());
    QVERIFY(!m_second->view()->hiddenFilesShown());
    firstHidden->trigger();
    QTRY_VERIFY(m_first->view()->hiddenFilesShown());
    QVERIFY(!m_second->view()->hiddenFilesShown());
    QVERIFY(firstHidden->isChecked());
    QVERIFY(!secondHidden->isChecked());
    firstHidden->trigger();
    GeneralSettings::self()->setGlobalViewProps(true);

    // The main toolbar and its file are not concerned.
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), mainBefore);
    QVERIFY(!QFile::exists(localMainFile()));
    editor.cancel();
    QCOMPARE(savedFileViewLayout(), expected);
    QCOMPARE(layoutOf(toolBar(m_second)), expected);
}

void WhaleToolbarConfigTest::testApplyBothToolBars()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), defaultMainLayout);
    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::globalId());
    QVERIFY(dialog);
    const Editor editor(dialog);

    // First the main toolbar, then the File View Toolbar, one Apply.
    QVERIFY(editor.remove(KLocalizedString::removeAcceleratorMarker(global(QStringLiteral("toggle_search"))->iconText())));
    QVERIFY(editor.select(fileViewName));
    QVERIFY(editor.remove(prototypeText(m_mainWindow.data(), QStringLiteral("go_up"))));
    // The change of the main toolbar is still there.
    QVERIFY(editor.select(mainName));
    QCOMPARE(editor.current()->count(), defaultMainLayout.count() - 1);
    QVERIFY(editor.select(fileViewName));
    QCOMPARE(editor.current()->count(), defaultLayout.count() - 1);

    editor.apply();
    QStringList expectedMain = defaultMainLayout;
    expectedMain.removeOne(QStringLiteral("toggle_search"));
    QStringList expected = defaultLayout;
    expected.removeOne(QStringLiteral("go_up"));
    QCOMPARE(savedMainLayout(), expectedMain);
    QCOMPARE(savedFileViewLayout(), expected);
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), expectedMain);
    QCOMPARE(layoutOf(toolBar(m_first)), expected);
    QCOMPARE(layoutOf(toolBar(m_second)), expected);
    editor.ok();
    QVERIFY(editClients(m_mainWindow.data()).isEmpty());
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), expectedMain);
    QCOMPARE(layoutOf(toolBar(m_first)), expected);
}

void WhaleToolbarConfigTest::testApplyTwice()
{
    // After an Apply the window is still the first client of its factory: KEditToolBar takes
    // the first one for the window when it saves the next time.
    QVERIFY(showWindow(m_mainWindow.data()));
    const int menus = m_mainWindow->menuBar()->actions().count();
    QVERIFY(menus > 3);
    const int windowActions = m_mainWindow->actionCollection()->count();
    // Another client of the factory, as the terminal panel's (a part without toolbars).
    KXMLGUIClient part;
    m_mainWindow->guiFactory()->addClient(&part);
    const int clientsBefore = m_mainWindow->guiFactory()->clients().count();
    const auto removePart = qScopeGuard([this, &part]() {
        m_mainWindow->guiFactory()->removeClient(&part);
    });
    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    const Editor editor(dialog);
    QCOMPARE(editor.toolBars(), QStringList({mainName, fileViewName}));
    QStringList expected = defaultLayout;
    const QStringList added = {QStringLiteral("go_home"), QStringLiteral("show_preview"), QStringLiteral("stop")};
    for (const QString &name : added) {
        QVERIFY(editor.insert(prototypeText(m_mainWindow.data(), name)));
        editor.apply();
        expected.append(name);
        QCOMPARE(m_mainWindow->guiFactory()->clients().first(), static_cast<KXMLGUIClient *>(m_mainWindow.data()));
        QCOMPARE(editClients(m_mainWindow.data()).count(), 1);
        QVERIFY(m_mainWindow->guiFactory()->clients().contains(&part));
        QCOMPARE(m_mainWindow->guiFactory()->clients().count(), clientsBefore + 1);
        QCOMPARE(savedFileViewLayout(), expected);
        QCOMPARE(layoutOf(toolBar(m_mainWindow->activeViewContainer())), expected);
        // The window is complete: its menus (with the standard ones) and its toolbar.
        QCOMPARE(m_mainWindow->menuBar()->actions().count(), menus);
        QVERIFY(m_mainWindow->guiFactory()->container(QStringLiteral("help"), m_mainWindow.data()));
        QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), defaultMainLayout);
        QCOMPARE(m_mainWindow->actionCollection()->count(), windowActions);
    }
    // The file of the File View Toolbar holds its toolbar only (nothing of ui_standards.rc).
    QFile local(localFileViewFile());
    QVERIFY(local.open(QIODevice::ReadOnly));
    const QByteArray content = local.readAll();
    QVERIFY(!content.contains("<MenuBar"));
    QVERIFY(!content.contains("mainToolBar"));
    QVERIFY(editor.remove(prototypeText(m_mainWindow.data(), QStringLiteral("stop"))));
    editor.ok();
    expected.removeLast();
    QCOMPARE(savedFileViewLayout(), expected);
    QCOMPARE(m_mainWindow->menuBar()->actions().count(), menus);
    QCOMPARE(m_mainWindow->toolBars().count(), 1);
}

void WhaleToolbarConfigTest::testCancel()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::globalId());
    QVERIFY(dialog);
    const Editor editor(dialog);
    QVERIFY(editor.remove(KLocalizedString::removeAcceleratorMarker(global(QStringLiteral("toggle_search"))->iconText())));
    QVERIFY(editor.select(fileViewName));
    QVERIFY(editor.insert(prototypeText(m_mainWindow.data(), QStringLiteral("go_home"))));
    QSignalSpy changed(WhaleToolbarManager::of(m_mainWindow.data()), &WhaleToolbarManager::fileViewLayoutChanged);
    editor.cancel();
    QVERIFY(!QFile::exists(localFileViewFile()));
    QVERIFY(!QFile::exists(localMainFile()));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(layoutOf(toolBar(m_first)), defaultLayout);
    QCOMPARE(layoutOf(toolBar(m_second)), defaultLayout);
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), defaultMainLayout);
    QVERIFY(editClients(m_mainWindow.data()).isEmpty());

    // Nothing of it is left for the next time.
    dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    QCOMPARE(Editor(dialog).current()->count(), defaultLayout.count());
    Editor(dialog).cancel();
}

void WhaleToolbarConfigTest::testApplyThenCancel()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    const Editor editor(dialog);
    QVERIFY(editor.insert(prototypeText(m_mainWindow.data(), QStringLiteral("go_home"))));
    editor.apply();
    const QStringList applied = defaultLayout + QStringList({QStringLiteral("go_home")});
    QCOMPARE(savedFileViewLayout(), applied);

    QVERIFY(editor.insert(prototypeText(m_mainWindow.data(), QStringLiteral("stop"))));
    QVERIFY(editor.select(mainName));
    QVERIFY(editor.remove(KLocalizedString::removeAcceleratorMarker(global(QStringLiteral("toggle_search"))->iconText())));
    editor.cancel();
    // What was applied stays, the later changes are gone.
    QCOMPARE(savedFileViewLayout(), applied);
    QCOMPARE(layoutOf(toolBar(m_first)), applied);
    QCOMPARE(layoutOf(toolBar(m_second)), applied);
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), defaultMainLayout);
    QVERIFY(editClients(m_mainWindow.data()).isEmpty());
}

void WhaleToolbarConfigTest::testOk()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QPointer<KEditToolBar> dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    const Editor editor(dialog);
    QVERIFY(editor.insert(prototypeText(m_mainWindow.data(), QStringLiteral("go_home"))));
    editor.ok();
    const QStringList expected = defaultLayout + QStringList({QStringLiteral("go_home")});
    QCOMPARE(savedFileViewLayout(), expected);
    QCOMPARE(layoutOf(toolBar(m_first)), expected);
    QCOMPARE(layoutOf(toolBar(m_second)), expected);
    QVERIFY(!QFile::exists(localMainFile()));
    QVERIFY(editClients(m_mainWindow.data()).isEmpty());
    QTRY_VERIFY(!dialog);
}

void WhaleToolbarConfigTest::testSeparatorAndSpacer()
{
    // File -> dialog -> change -> file -> toolbars; the order stays.
    writeUserFile("<Action name=\"view_settings\"/><Separator/><Action name=\"go_home\"/><Spacer/><Action name=\"go_up\"/>");
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    const QStringList before = {QStringLiteral("view_settings"), QStringLiteral("-"), QStringLiteral("go_home"), QStringLiteral("|"), QStringLiteral("go_up")};
    QCOMPARE(layoutOf(toolBar(m_first)), before);
    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    const Editor editor(dialog);
    const QStringList current = textsOf(editor.current());
    QCOMPARE(current.count(), before.count());
    QCOMPARE(current.value(1), separatorText);
    QCOMPARE(current.value(3), spacerText);

    // Behind the last entry: a separator, an action, a spacer, an action.
    editor.current()->setCurrentRow(editor.current()->count() - 1);
    QVERIFY(editor.insert(separatorText));
    QVERIFY(editor.insert(prototypeText(m_mainWindow.data(), QStringLiteral("stop"))));
    QVERIFY(editor.insert(spacerText));
    QVERIFY(editor.insert(prototypeText(m_mainWindow.data(), QStringLiteral("go_back"))));
    // And the first separator away.
    QVERIFY(editor.remove(separatorText));
    editor.apply();
    const QStringList expected = {QStringLiteral("view_settings"),
                                  QStringLiteral("go_home"),
                                  QStringLiteral("|"),
                                  QStringLiteral("go_up"),
                                  QStringLiteral("-"),
                                  QStringLiteral("stop"),
                                  QStringLiteral("|"),
                                  QStringLiteral("go_back")};
    QCOMPARE(savedFileViewLayout(), expected);
    QCOMPARE(layoutOf(toolBar(m_first)), expected);
    QCOMPARE(layoutOf(toolBar(m_second)), expected);
    editor.ok();

    // The dialog reads what it wrote.
    dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    const QStringList again = textsOf(Editor(dialog).current());
    QCOMPARE(again.count(), expected.count());
    QCOMPARE(again.value(2), spacerText);
    QCOMPARE(again.value(4), separatorText);
    QCOMPARE(again.value(6), spacerText);
    Editor(dialog).cancel();
    QCOMPARE(savedFileViewLayout(), expected);
}

void WhaleToolbarConfigTest::testNewWindowReadsLayout()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    QVERIFY(Editor(dialog).insert(prototypeText(m_mainWindow.data(), QStringLiteral("go_home"))));
    QVERIFY(Editor(dialog).changeText(KLocalizedString::removeAcceleratorMarker(global(QStringLiteral("go_up"))->iconText()), QStringLiteral("Upwards")));
    Editor(dialog).ok();
    const QStringList expected = defaultLayout + QStringList({QStringLiteral("go_home")});
    QCOMPARE(layoutOf(toolBar(m_first)), expected);

    // As after a restart: a new window, and the views of a split view in it.
    m_mainWindow.reset(new DolphinMainWindow());
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QCOMPARE(layoutOf(toolBar(m_first)), expected);
    QCOMPARE(layoutOf(toolBar(m_second)), expected);
    QCOMPARE(local(m_first, QStringLiteral("go_up"))->iconText(), QStringLiteral("Upwards"));
    QCOMPARE(local(m_second, QStringLiteral("go_up"))->iconText(), QStringLiteral("Upwards"));
    QVERIFY(global(QStringLiteral("go_up"))->iconText() != QStringLiteral("Upwards"));
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), defaultMainLayout);
}

void WhaleToolbarConfigTest::testTwoWindows()
{
    QVERIFY(showWindow(m_mainWindow.data()));
    DolphinViewContainer *containerA = m_mainWindow->activeViewContainer();
    QScopedPointer<DolphinMainWindow> windowB(new DolphinMainWindow());
    DolphinViewContainer *containerB = showWindow(windowB.data());
    QVERIFY(containerB);
    QTRY_VERIFY(toolBar(containerA)->scope() && toolBar(containerB)->scope());
    QVERIFY(WhaleToolbarManager::of(m_mainWindow.data()) != WhaleToolbarManager::of(windowB.data()));
    WhaleToolbarConfigurator *configuratorA = WhaleToolbarConfigurator::of(m_mainWindow.data());
    WhaleToolbarConfigurator *configuratorB = WhaleToolbarConfigurator::of(windowB.data());
    QVERIFY(configuratorA && configuratorB && configuratorA != configuratorB);

    // Window A configures; window B (the active one) is there.
    KEditToolBar *dialogA = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialogA);
    QCOMPARE(dialogA->parentWidget(), m_mainWindow.data());
    QCOMPARE(editClients(m_mainWindow.data()).count(), 1);
    QVERIFY(editClients(windowB.data()).isEmpty());
    QVERIFY(!configuratorB->dialog());
    // The prototypes describe the actions of window A.
    KXMLGUIClient *clientA = editClients(m_mainWindow.data()).first();
    QCOMPARE(clientA->factory(), m_mainWindow->guiFactory());
    QVERIFY(clientA->actionCollection()->action(QStringLiteral("go_home")) != windowB->actionCollection()->action(QStringLiteral("go_home")));
    // Find Action of window A waits, the one of window B does not.
    QVERIFY(!global(QStringLiteral("open_kcommand_bar"))->isEnabled());
    QVERIFY(windowB->actionCollection()->action(QStringLiteral("open_kcommand_bar"))->isEnabled());

    // Window B can configure at the same time, with a client of its own.
    KEditToolBar *dialogB = configure(windowB.data(), WhaleToolbarDescriptor::globalId());
    QVERIFY(dialogB && dialogB != dialogA);
    QCOMPARE(dialogB->parentWidget(), windowB.data());
    QCOMPARE(editClients(windowB.data()).count(), 1);
    QVERIFY(editClients(windowB.data()).first() != clientA);
    QCOMPARE(Editor(dialogB).toolBars(), QStringList({mainName, fileViewName}));
    Editor(dialogB).cancel();
    QVERIFY(editClients(windowB.data()).isEmpty());
    QCOMPARE(editClients(m_mainWindow.data()).count(), 1);

    // Apply in A: one configuration, the toolbars of both windows follow.
    QVERIFY(Editor(dialogA).insert(prototypeText(m_mainWindow.data(), QStringLiteral("go_home"))));
    Editor(dialogA).apply();
    const QStringList expected = defaultLayout + QStringList({QStringLiteral("go_home")});
    QCOMPARE(layoutOf(toolBar(containerA)), expected);
    QCOMPARE(layoutOf(toolBar(containerB)), expected);
    QAction *homeA = toolBar(containerA)->scope()->action(QStringLiteral("go_home"));
    QAction *homeB = toolBar(containerB)->scope()->action(QStringLiteral("go_home"));
    QVERIFY(homeA != homeB);
    QVERIFY(toolBar(containerA)->actions().contains(homeA));
    QVERIFY(toolBar(containerB)->actions().contains(homeB));
    QCOMPARE(windowB->toolBars().count(), 1);

    // Window B goes with the dialog of A open, then A's dialog finishes.
    windowB.reset();
    QVERIFY(dialogA->isVisible());
    Editor(dialogA).ok();
    QVERIFY(editClients(m_mainWindow.data()).isEmpty());
    QCOMPARE(layoutOf(toolBar(containerA)), expected);
}

void WhaleToolbarConfigTest::testChangeText()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    const QString backText = global(QStringLiteral("go_back"))->text();
    const QString backIconText = global(QStringLiteral("go_back"))->iconText();
    const QString backToolTip = global(QStringLiteral("go_back"))->toolTip();
    const QString localBefore = local(m_first, QStringLiteral("go_back"))->iconText();
    const QString localToolTipBefore = local(m_first, QStringLiteral("go_back"))->toolTip();

    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    const Editor editor(dialog);
    QVERIFY(editor.changeText(KLocalizedString::removeAcceleratorMarker(backIconText), QStringLiteral("Previous Folder")));
    QVERIFY(textsOf(editor.current()).contains(QStringLiteral("Previous Folder")));
    QCOMPARE(local(m_first, QStringLiteral("go_back"))->iconText(), localBefore);
    editor.apply();

    // The toolbars of both file views ...
    QCOMPARE(local(m_first, QStringLiteral("go_back"))->iconText(), QStringLiteral("Previous Folder"));
    QCOMPARE(local(m_second, QStringLiteral("go_back"))->iconText(), QStringLiteral("Previous Folder"));
    QCOMPARE(layoutOf(toolBar(m_first)), defaultLayout);
    // ... but not the window's action of the same name, nor the main toolbar.
    QCOMPARE(global(QStringLiteral("go_back"))->text(), backText);
    QCOMPARE(global(QStringLiteral("go_back"))->iconText(), backIconText);
    QCOMPARE(global(QStringLiteral("go_back"))->toolTip(), backToolTip);
    QVERIFY(!QFile::exists(localMainFile()));
    // The dialog shows the changed text for the File View Toolbar only.
    QVERIFY(textsOf(editor.current()).contains(QStringLiteral("Previous Folder")));
    QVERIFY(editor.select(mainName));
    QVERIFY(!textsOf(editor.current()).contains(QStringLiteral("Previous Folder")));
    // (Back is not on the main toolbar by default: it offers it with its own text.)
    QVERIFY(textsOf(editor.available()).contains(KLocalizedString::removeAcceleratorMarker(backText)));
    editor.ok();

    // Open again: the changed text is what the dialog starts with.
    dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    QVERIFY(textsOf(Editor(dialog).current()).contains(QStringLiteral("Previous Folder")));

    // Back to the defaults: the text of before, in the dialog and in the toolbars.
    Editor(dialog).defaults(true);
    QVERIFY(!textsOf(Editor(dialog).current()).contains(QStringLiteral("Previous Folder")));
    QCOMPARE(local(m_first, QStringLiteral("go_back"))->iconText(), localBefore);
    QCOMPARE(local(m_second, QStringLiteral("go_back"))->iconText(), localBefore);
    QCOMPARE(local(m_first, QStringLiteral("go_back"))->toolTip(), localToolTipBefore);
    QCOMPARE(global(QStringLiteral("go_back"))->iconText(), backIconText);
    Editor(dialog).cancel();
}

void WhaleToolbarConfigTest::testChangeIcon()
{
    // "Change Icon…" asks with KIconDialog and writes icon="…" for the action; here the file
    // has it already.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    const QString globalIcon = global(QStringLiteral("go_up"))->icon().name();
    const QString localIcon = local(m_first, QStringLiteral("go_up"))->icon().name();
    QVERIFY(!globalIcon.isEmpty());
    QVERIFY(globalIcon != QLatin1String("go-top"));

    writeUserFile("<Action name=\"go_back\"/><Action name=\"go_up\"/>", "<Action name=\"go_up\" icon=\"go-top\" shortcut=\"F9\"/>");
    WhaleToolbarManager::reloadAllFileViewLayouts();
    QCOMPARE(layoutOf(toolBar(m_first)), QStringList({QStringLiteral("go_back"), QStringLiteral("go_up")}));
    QCOMPARE(local(m_first, QStringLiteral("go_up"))->icon().name(), QStringLiteral("go-top"));
    QCOMPARE(local(m_second, QStringLiteral("go_up"))->icon().name(), QStringLiteral("go-top"));
    QCOMPARE(global(QStringLiteral("go_up"))->icon().name(), globalIcon);
    // Never a shortcut from the file.
    QVERIFY(local(m_first, QStringLiteral("go_up"))->shortcuts().isEmpty());

    // The dialog keeps the icon when it saves another change ...
    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    const auto clients = editClients(m_mainWindow.data());
    QCOMPARE(clients.count(), 1);
    QCOMPARE(clients.first()->actionCollection()->action(QStringLiteral("go_up"))->icon().name(), QStringLiteral("go-top"));
    QVERIFY(clients.first()->actionCollection()->action(QStringLiteral("go_up"))->shortcuts().isEmpty());
    QVERIFY(Editor(dialog).insert(prototypeText(m_mainWindow.data(), QStringLiteral("go_home"))));
    Editor(dialog).apply();
    QCOMPARE(layoutOf(toolBar(m_second)), QStringList({QStringLiteral("go_back"), QStringLiteral("go_up"), QStringLiteral("go_home")}));
    QCOMPARE(local(m_first, QStringLiteral("go_up"))->icon().name(), QStringLiteral("go-top"));
    QCOMPARE(local(m_second, QStringLiteral("go_up"))->icon().name(), QStringLiteral("go-top"));
    QCOMPARE(global(QStringLiteral("go_up"))->icon().name(), globalIcon);
    QVERIFY(global(QStringLiteral("go_up"))->shortcuts() != QList<QKeySequence>{QKeySequence(Qt::Key_F9)});

    // ... and Defaults gives the action its icon back.
    Editor(dialog).defaults(true);
    QCOMPARE(local(m_first, QStringLiteral("go_up"))->icon().name(), localIcon);
    QCOMPARE(local(m_second, QStringLiteral("go_up"))->icon().name(), localIcon);
    QCOMPARE(clients.first()->actionCollection()->action(QStringLiteral("go_up"))->icon().name(), globalIcon);
    Editor(dialog).cancel();
}

void WhaleToolbarConfigTest::testDefaults()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    // Both toolbars changed and saved.
    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    QVERIFY(Editor(dialog).insert(prototypeText(m_mainWindow.data(), QStringLiteral("go_home"))));
    QVERIFY(Editor(dialog).select(mainName));
    QVERIFY(Editor(dialog).remove(KLocalizedString::removeAcceleratorMarker(global(QStringLiteral("toggle_search"))->iconText())));
    Editor(dialog).ok();
    QVERIFY(QFile::exists(localFileViewFile()));
    QVERIFY(QFile::exists(localMainFile()));
    QVERIFY(layoutOf(toolBar(m_first)) != defaultLayout);
    QVERIFY(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))) != defaultMainLayout);

    dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    // KDE asks, for all toolbars; "Cancel" changes nothing.
    QString question;
    Editor(dialog).defaults(false, &question);
    QVERIFY2(question.contains(QStringLiteral("all toolbars")), qPrintable(question));
    QVERIFY(QFile::exists(localFileViewFile()));
    QVERIFY(QFile::exists(localMainFile()));
    QVERIFY(layoutOf(toolBar(m_first)) != defaultLayout);

    // Confirmed: both files are gone and both kinds of toolbars show their defaults at once,
    // although the File View Toolbar was the chosen one.
    Editor(dialog).defaults(true);
    QVERIFY(dialog->isVisible());
    QVERIFY(!QFile::exists(localFileViewFile()));
    QVERIFY(!QFile::exists(localMainFile()));
    QCOMPARE(layoutOf(toolBar(m_first)), defaultLayout);
    QCOMPARE(layoutOf(toolBar(m_second)), defaultLayout);
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), defaultMainLayout);
    // The dialog was built again: still both toolbars with their plain names, the same one
    // chosen, the default entries.
    const Editor editor(dialog);
    QCOMPARE(editor.toolBars(), QStringList({mainName, fileViewName}));
    QCOMPARE(editor.combo()->currentText(), fileViewName);
    QCOMPARE(editor.current()->count(), defaultLayout.count());
    QCOMPARE(m_mainWindow->guiFactory()->clients().first(), static_cast<KXMLGUIClient *>(m_mainWindow.data()));
    QCOMPARE(editClients(m_mainWindow.data()).count(), 1);

    // It goes on working after that.
    QVERIFY(editor.insert(prototypeText(m_mainWindow.data(), QStringLiteral("stop"))));
    editor.ok();
    QCOMPARE(layoutOf(toolBar(m_second)), defaultLayout + QStringList({QStringLiteral("stop")}));
    QVERIFY(!QFile::exists(localMainFile()));
    QVERIFY(editClients(m_mainWindow.data()).isEmpty());
}

void WhaleToolbarConfigTest::testOpenAndCloseRepeatedly()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    const auto clientsBefore = m_mainWindow->guiFactory()->clients();
    const int windowActions = m_mainWindow->actions().count();
    const int toolBarsBefore = m_mainWindow->findChildren<KToolBar *>().count();
    const int topLevelToolBars = [] {
        int count = 0;
        const auto widgets = QApplication::topLevelWidgets();
        for (QWidget *widget : widgets) {
            count += qobject_cast<KToolBar *>(widget) ? 1 : 0;
        }
        return count;
    }();
    QList<QPointer<KEditToolBar>> dialogs;
    QList<QPointer<QAction>> prototypes;
    QStringList expected = defaultLayout;
    for (int i = 0; i < 12; ++i) {
        const bool fileView = i % 2 == 0;
        KEditToolBar *dialog = configure(m_mainWindow.data(), fileView ? WhaleToolbarDescriptor::fileViewId() : WhaleToolbarDescriptor::globalId());
        QVERIFY(dialog);
        dialogs.append(dialog);
        const Editor editor(dialog);
        QCOMPARE(editor.combo()->currentText(), fileView ? fileViewName : mainName);
        const auto clients = editClients(m_mainWindow.data());
        QCOMPARE(clients.count(), 1);
        prototypes.append(clients.first()->actionCollection()->action(QStringLiteral("go_back")));
        QVERIFY(prototypes.last());
        switch (i % 6) {
        case 0: // Cancel
            editor.cancel();
            break;
        case 1: // closed
            dialog->close();
            break;
        case 2: // OK without a change
            editor.ok();
            break;
        case 3: // a change, Cancel
            QVERIFY(editor.select(fileViewName));
            QVERIFY(editor.insert(separatorText));
            editor.cancel();
            break;
        case 4: // a change, Apply, another change, Cancel
            QVERIFY(editor.insert(separatorText));
            editor.apply();
            expected.append(QStringLiteral("-"));
            QVERIFY(editor.insert(spacerText));
            editor.cancel();
            break;
        case 5: // a change, Apply, another change, OK
            QVERIFY(editor.select(fileViewName));
            QVERIFY(editor.insert(spacerText));
            editor.apply();
            QVERIFY(editor.insert(separatorText));
            editor.ok();
            expected.append(QStringLiteral("|"));
            expected.append(QStringLiteral("-"));
            break;
        }
        // Finished: the client has left the factory and its actions are gone, at once.
        QVERIFY(!WhaleToolbarConfigurator::of(m_mainWindow.data())->dialog());
        QCOMPARE(m_mainWindow->guiFactory()->clients(), clientsBefore);
        QVERIFY(!prototypes.last());
        QVERIFY(global(QStringLiteral("open_kcommand_bar"))->isEnabled());
        QCOMPARE(layoutOf(toolBar(m_first)), expected);
        QCOMPARE(layoutOf(toolBar(m_second)), expected);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    for (const auto &dialog : std::as_const(dialogs)) {
        QTRY_VERIFY(!dialog);
    }
    // Nothing stayed: no action at the window, no toolbar in it or beside it.
    QCOMPARE(m_mainWindow->actions().count(), windowActions);
    QCOMPARE(m_mainWindow->findChildren<KToolBar *>().count(), toolBarsBefore);
    int topLevelToolBarsNow = 0;
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets) {
        topLevelToolBarsNow += qobject_cast<KToolBar *>(widget) ? 1 : 0;
    }
    QCOMPARE(topLevelToolBarsNow, topLevelToolBars);
    QCOMPARE(m_mainWindow->toolBars().count(), 1);
}

void WhaleToolbarConfigTest::testClosingWindowWithDialog()
{
    // The window goes with the dialog open: after a change, after an Apply, untouched.
    for (int variant = 0; variant < 3; ++variant) {
        if (variant > 0) {
            m_mainWindow.reset(new DolphinMainWindow());
        }
        showSplit();
        if (QTest::currentTestFailed()) {
            return;
        }
        QPointer<KEditToolBar> dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
        QVERIFY(dialog);
        QPointer<WhaleToolbarConfigurator> configurator = WhaleToolbarConfigurator::of(m_mainWindow.data());
        const auto clients = editClients(m_mainWindow.data());
        QCOMPARE(clients.count(), 1);
        QPointer<QAction> prototype = clients.first()->actionCollection()->action(QStringLiteral("go_back"));
        QPointer<WhaleFileViewToolBar> bar = toolBar(m_first);
        QVERIFY(prototype && configurator && bar);
        if (variant >= 1) {
            QVERIFY(Editor(dialog).insert(prototypeText(m_mainWindow.data(), QStringLiteral("go_home"))));
        }
        if (variant == 2) {
            Editor(dialog).apply();
            QVERIFY(Editor(dialog).insert(separatorText));
        }
        m_mainWindow.reset();
        m_first = m_second = nullptr;
        QVERIFY(!dialog);
        QVERIFY(!configurator);
        QVERIFY(!prototype);
        QVERIFY(!bar);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
    // Closed by the user instead (the window deletes itself then).
    auto window = new DolphinMainWindow();
    QPointer<DolphinMainWindow> guard = window;
    QVERIFY(showWindow(window));
    QPointer<KEditToolBar> dialog = configure(window, WhaleToolbarDescriptor::globalId());
    QVERIFY(dialog);
    QVERIFY(window->testAttribute(Qt::WA_DeleteOnClose));
    window->close();
    QTRY_VERIFY(!guard);
    QVERIFY(!dialog);
}

void WhaleToolbarConfigTest::testNoShortcuts()
{
    // The file even asks for a shortcut; the prototypes and the views' actions take none.
    writeUserFile("<Action name=\"go_back\"/><Action name=\"view_redisplay\"/><Action name=\"show_hidden_files\"/>",
                  "<Action name=\"view_redisplay\" shortcut=\"F5\"/><Action name=\"show_hidden_files\" shortcut=\"Alt+.\"/>");
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_first->setActive(true);
    QTRY_VERIFY(m_first->isActive());
    const QList<QKeySequence> reloadShortcuts = global(QStringLiteral("view_redisplay"))->shortcuts();
    const QList<QKeySequence> hiddenShortcuts = global(QStringLiteral("show_hidden_files"))->shortcuts();
    QVERIFY(!reloadShortcuts.isEmpty() && !hiddenShortcuts.isEmpty());

    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    const auto noPrototypeShortcuts = [this]() {
        const auto clients = editClients(m_mainWindow.data());
        if (clients.count() != 1 || clients.first()->actionCollection()->count() != WhaleViewCommandScope::allowedActions().count() + 1) {
            return false;
        }
        const auto prototypes = clients.first()->actionCollection()->actions();
        for (const QAction *prototype : prototypes) {
            if (!prototype->shortcuts().isEmpty() || KActionCollection::isShortcutsConfigurable(const_cast<QAction *>(prototype))) {
                return false;
            }
        }
        return true;
    };
    QVERIFY(noPrototypeShortcuts());
    QVERIFY(Editor(dialog).insert(prototypeText(m_mainWindow.data(), QStringLiteral("go_home"))));
    Editor(dialog).apply(); // the factory builds the client again
    QVERIFY(noPrototypeShortcuts());
    const QStringList scopeNames = WhaleViewCommandScope::allowedActions();
    for (const QString &name : scopeNames) {
        QVERIFY2(local(m_first, name)->shortcuts().isEmpty(), qPrintable(name));
        QVERIFY2(local(m_second, name)->shortcuts().isEmpty(), qPrintable(name));
    }
    // The window's shortcuts are what they were.
    QCOMPARE(global(QStringLiteral("view_redisplay"))->shortcuts(), reloadShortcuts);
    QCOMPARE(global(QStringLiteral("show_hidden_files"))->shortcuts(), hiddenShortcuts);

    // With the dialog open a shortcut is not ambiguous: it acts on the active view, and KDE
    // shows no "ambiguous shortcut" message.
    bool otherDialogShown = false;
    QTimer::singleShot(500, this, [&otherDialogShown, dialog]() {
        const auto windows = QApplication::topLevelWidgets();
        for (QWidget *window : windows) {
            if (auto other = qobject_cast<QDialog *>(window); other && other != dialog && other->isVisible()) {
                otherDialogShown = true;
                other->reject();
            }
        }
    });
    const bool firstBefore = m_first->view()->hiddenFilesShown();
    const bool secondBefore = m_second->view()->hiddenFilesShown();
    // The dialog is not modal: the user works in the window with the dialog open.
    m_mainWindow->QWidget::activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_mainWindow.data()));
    QVERIFY(dialog->isVisible());
    QTest::keyClick(m_first->view(), Qt::Key_Period, Qt::AltModifier); // show_hidden_files
    QTRY_VERIFY(m_first->view()->hiddenFilesShown() != firstBefore);
    QCOMPARE(m_second->view()->hiddenFilesShown(), secondBefore);
    QTest::keyClick(m_first->view(), Qt::Key_F5); // view_redisplay
    QTest::qWait(600);
    QVERIFY(!otherDialogShown);
    QTest::keyClick(m_first->view(), Qt::Key_Period, Qt::AltModifier);
    QTRY_COMPARE(m_first->view()->hiddenFilesShown(), firstBefore);
    Editor(dialog).cancel();
}

void WhaleToolbarConfigTest::testFindActionWhileConfiguring()
{
    QVERIFY(showWindow(m_mainWindow.data()));
    // What Find Action (KCommandBar) and the window offer: the actions of the factory's clients.
    const auto offered = [this]() {
        int count = 0;
        const auto clients = m_mainWindow->guiFactory()->clients();
        for (KXMLGUIClient *client : clients) {
            count += client->actionCollection()->count();
        }
        return count;
    };
    const int before = offered();
    QAction *findAction = global(QStringLiteral("open_kcommand_bar"));
    QVERIFY(findAction && findAction->isEnabled());

    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::globalId());
    QVERIFY(dialog);
    // While the prototypes are in the factory, Find Action is not available.
    QCOMPARE(offered(), before + WhaleViewCommandScope::allowedActions().count() + 1); // and the location bar
    QVERIFY(!findAction->isEnabled());
    Editor(dialog).apply();
    QVERIFY(!findAction->isEnabled());
    Editor(dialog).cancel();
    QVERIFY(findAction->isEnabled());
    QCOMPARE(offered(), before);

    // An action that was disabled before stays so.
    findAction->setEnabled(false);
    dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::globalId());
    QVERIFY(dialog);
    Editor(dialog).cancel();
    QVERIFY(!findAction->isEnabled());
    findAction->setEnabled(true);
}

void WhaleToolbarConfigTest::testContextMenu()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    for (DolphinViewContainer *container : {m_second, m_first}) {
        WhaleFileViewToolBar *bar = toolBar(container);
        // On the toolbar and on one of its buttons.
        for (QWidget *target : {static_cast<QWidget *>(bar), bar->widgetForAction(local(container, QStringLiteral("go_up")))}) {
            QVERIFY(target);
            const QPoint position = target == bar ? QPoint(bar->width() / 2, bar->height() / 2) : target->rect().center();
            QContextMenuEvent event(QContextMenuEvent::Mouse, position, target->mapToGlobal(position));
            QApplication::sendEvent(target, &event);
            QPointer<QMenu> menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            QCOMPARE(menu->parentWidget(), bar);
            // No second way to the same dialog; hiding is tested in whaletoolbarmigrationtest.
            QCOMPARE(menu->actions().count(), 2);
            QAction *configure = menu->actions().first();
            QCOMPARE(KLocalizedString::removeAcceleratorMarker(configure->text()), QStringLiteral("Configure File View Toolbar…"));
            QCOMPARE(menu->actions().at(1)->objectName(), QStringLiteral("whale_hide_file_view_toolbars"));
            configure->trigger();
            menu->close();

            KEditToolBar *dialog = WhaleToolbarConfigurator::of(m_mainWindow.data())->dialog();
            QVERIFY(dialog && dialog->isVisible());
            QCOMPARE(dialog->parentWidget(), m_mainWindow.data());
            QCOMPARE(Editor(dialog).combo()->currentText(), fileViewName);
            QCOMPARE(Editor(dialog).toolBars(), QStringList({mainName, fileViewName}));
            Editor(dialog).cancel();
            QVERIFY(editClients(m_mainWindow.data()).isEmpty());
            QTRY_VERIFY(!menu);
        }
    }
    // The window's own menu for toolbars and panels did not open on top.
    QVERIFY(!QApplication::activePopupWidget());
}

void WhaleToolbarConfigTest::testMainToolBarStillWorks()
{
    // What DolphinMainWindow::saveNewToolbarConfig() does for the main toolbar is still done.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    KToolBar *mainToolBar = m_mainWindow->toolBar(QStringLiteral("mainToolBar"));
    QPointer<WhaleLocationBar> firstLocationBar = toolBar(m_first)->locationBar();
    QVERIFY(firstLocationBar->isVisible());

    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::globalId());
    QVERIFY(dialog);
    const Editor editor(dialog);
    // An action both kinds of toolbars know, into the main toolbar.
    QVERIFY(editor.insert(KLocalizedString::removeAcceleratorMarker(global(QStringLiteral("go_home"))->text())));
    editor.apply();
    mainToolBar = m_mainWindow->toolBar(QStringLiteral("mainToolBar"));
    QCOMPARE(layoutOf(mainToolBar), defaultMainLayout + QStringList({QStringLiteral("go_home")}));
    // The window's action, not a prototype and not a view's.
    QVERIFY(mainToolBar->actions().contains(global(QStringLiteral("go_home"))));
    QCOMPARE(layoutOf(toolBar(m_first)), defaultLayout);
    QVERIFY(!QFile::exists(localFileViewFile()));
    // The views keep their location bars, and the menus still open.
    QCOMPARE(toolBar(m_first)->locationBar(), firstLocationBar.data());
    QVERIFY(firstLocationBar->isVisible());
    QVERIFY(mainToolBar->widgetForAction(global(QStringLiteral("hamburger_menu"))));
    QVERIFY(mainToolBar->widgetForAction(global(QStringLiteral("split_view"))));
    // The view mode entries of the plugin views are in the View menu again.
    auto viewMenu = qobject_cast<QMenu *>(m_mainWindow->guiFactory()->container(QStringLiteral("view"), m_mainWindow.data()));
    QVERIFY(viewMenu);
    QVERIFY(viewMenu->actions().count() > 5);
    editor.ok();
    QCOMPARE(layoutOf(m_mainWindow->toolBar(QStringLiteral("mainToolBar"))), defaultMainLayout + QStringList({QStringLiteral("go_home")}));
    // The window's home button acts on the active view.
    m_first->setActive(true);
    QTRY_VERIFY(m_first->isActive());
    m_first->setUrl(QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    QTRY_COMPARE(m_first->url(), QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    const QUrl secondUrl = m_second->url();
    global(QStringLiteral("go_home"))->trigger();
    QTRY_VERIFY(m_first->url() != QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    QCOMPARE(m_second->url(), secondUrl);
}

void WhaleToolbarConfigTest::testLocationBar()
{
    // The location bar of the views as an entry of the File View Toolbar (location bar plan, LB2).
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    const QString text = QStringLiteral("Location Bar");
    QPointer<WhaleLocationBar> first = toolBar(m_first)->locationBar();
    QPointer<WhaleLocationBar> second = toolBar(m_second)->locationBar();
    QTRY_VERIFY(first->isVisible() && second->isVisible());
    KEditToolBar *dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    const Editor editor(dialog);
    // In the toolbar: not offered a second time.
    QCOMPARE(editor.current()->findItems(text, Qt::MatchExactly).count(), 1);
    QCOMPARE(editor.available()->findItems(text, Qt::MatchExactly).count(), 0);
    // Not offered for the main toolbar.
    QVERIFY(editor.select(mainName));
    QCOMPARE(editor.available()->findItems(text, Qt::MatchExactly).count(), 0);
    QVERIFY(editor.select(fileViewName));

    // Removed and applied: no location bars in the toolbars, the views keep theirs.
    QVERIFY(editor.remove(text));
    QCOMPARE(editor.available()->findItems(text, Qt::MatchExactly).count(), 1);
    editor.apply();
    QStringList expected = defaultLayout;
    expected.removeOne(QStringLiteral("whale_location_bar"));
    QCOMPARE(savedFileViewLayout(), expected);
    QCOMPARE(layoutOf(toolBar(m_first)), expected);
    QVERIFY(first && second && !first->isVisible() && !second->isVisible());
    QCOMPARE(m_first->urlNavigator(), first->urlNavigator());

    // Back in, at the end, once.
    QVERIFY(editor.insert(text));
    QCOMPARE(editor.available()->findItems(text, Qt::MatchExactly).count(), 0);
    QCOMPARE(editor.current()->findItems(text, Qt::MatchExactly).count(), 1);
    editor.apply();
    expected.append(QStringLiteral("whale_location_bar"));
    QCOMPARE(savedFileViewLayout(), expected);
    QCOMPARE(layoutOf(toolBar(m_second)), expected);
    QTRY_VERIFY(first->isVisible() && second->isVisible());
    QCOMPARE(toolBar(m_first)->locationBar(), first.data());

    // Another text is saved; the widget shows no text.
    QVERIFY(editor.changeText(text, QStringLiteral("Path")));
    editor.apply();
    QVERIFY(contentOf(localFileViewFile()).contains("whale_location_bar"));
    QVERIFY(contentOf(localFileViewFile()).contains("Path"));
    QTRY_VERIFY(first->isVisible());

    // Removed, then Cancel: nothing changes.
    QVERIFY(editor.remove(QStringLiteral("Path")));
    editor.cancel();
    QCOMPARE(savedFileViewLayout(), expected);
    QTRY_VERIFY(first->isVisible() && second->isVisible());

    // Defaults: at its place of the default layout again.
    dialog = configure(m_mainWindow.data(), WhaleToolbarDescriptor::fileViewId());
    QVERIFY(dialog);
    Editor(dialog).defaults(true);
    QCOMPARE(layoutOf(toolBar(m_first)), defaultLayout);
    QCOMPARE(layoutOf(toolBar(m_second)), defaultLayout);
    QCOMPARE(Editor(dialog).current()->findItems(text, Qt::MatchExactly).count(), 1);
    Editor(dialog).ok();
    QTRY_VERIFY(first->isVisible() && second->isVisible());
    QCOMPARE(m_mainWindow->findChildren<WhaleLocationBar *>().count(), 2);
}

QTEST_MAIN(WhaleToolbarConfigTest)

#include "whaletoolbarconfigtest.moc"
