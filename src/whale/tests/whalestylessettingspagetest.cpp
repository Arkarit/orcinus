/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalestylessettingspage.h"
#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphinviewcontainer.h"
#include "settings/dolphinsettingsdialog.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalestylemanager.h"
#include "whalestyles.h"

#include <KActionCollection>
#include <KKeySequenceWidget>
#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

/**
 * The page "Styles" of the settings dialog (ST4).
 */
class WhaleStylesSettingsPageTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testListsStyles();
    void testAddFromWindow();
    void testRenameKeepsId();
    void testElementsOnOff();
    void testOrderAndDelete();
    void testCancelDiscards();
    void testShortcut();
    void testCaptureAgain();
    void testApplyToWindowKeepsStatusBarOnOk();
    void testUnavailableInFolderMode();

private:
    void openDialog();
    void apply();
    template<typename T>
    T *child(const char *name) const
    {
        return m_page->findChild<T *>(QLatin1String(name));
    }
    QStringList listedNames() const;
    QTreeWidgetItem *elementItem(const QString &key) const;
    QString saveStyle(const QString &name);

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    QPointer<DolphinSettingsDialog> m_dialog;
    WhaleStylesSettingsPage *m_page = nullptr;
};

void WhaleStylesSettingsPageTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain({"whale"});
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->setGlobalViewProps(true);
    GeneralSettings::self()->save();
}

void WhaleStylesSettingsPageTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"));
    m_mainWindow.reset(new DolphinMainWindow());
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1000, 700);
    m_mainWindow->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_mainWindow.data()));
}

void WhaleStylesSettingsPageTest::cleanup()
{
    delete m_dialog;
    m_page = nullptr;
    m_mainWindow.reset();
    m_dir.reset();
    WhaleStyles::self()->editableStore().replaceStyles({});
    WhaleStyles::self()->save();
    GeneralSettings::self()->setGlobalViewProps(true);
    GeneralSettings::setShowStatusBar(GeneralSettings::EnumShowStatusBar::Small);
    GeneralSettings::self()->save();
}

void WhaleStylesSettingsPageTest::openDialog()
{
    m_dialog = new DolphinSettingsDialog(m_dir->url(), m_mainWindow.data(), m_mainWindow->actionCollection());
    m_dialog->setAttribute(Qt::WA_DeleteOnClose, false);
    m_page = m_dialog->findChild<WhaleStylesSettingsPage *>();
    QVERIFY(m_page);
}

void WhaleStylesSettingsPageTest::apply()
{
    QPushButton *button = m_dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply);
    QVERIFY(button->isEnabled());
    button->click();
}

QStringList WhaleStylesSettingsPageTest::listedNames() const
{
    QStringList names;
    auto list = child<QListWidget>("styleList");
    for (int row = 0; row < list->count(); ++row) {
        names.append(list->item(row)->text());
    }
    return names;
}

QTreeWidgetItem *WhaleStylesSettingsPageTest::elementItem(const QString &key) const
{
    auto tree = child<QTreeWidget>("styleElements");
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->data(0, Qt::UserRole + 1).toString() == key) {
            return tree->topLevelItem(i);
        }
    }
    return nullptr;
}

QString WhaleStylesSettingsPageTest::saveStyle(const QString &name)
{
    return WhaleStyleManager::ensure(m_mainWindow.data())->saveCurrentAsStyle(name);
}

void WhaleStylesSettingsPageTest::testListsStyles()
{
    saveStyle(QStringLiteral("Schlicht"));
    saveStyle(QStringLiteral("Bilder"));
    openDialog();

    QCOMPARE(listedNames(), (QStringList{QStringLiteral("Schlicht"), QStringLiteral("Bilder")}));
    QCOMPARE(child<QListWidget>("styleList")->currentRow(), 0);
    QCOMPARE(child<QLineEdit>("styleName")->text(), QStringLiteral("Schlicht"));
    auto tree = child<QTreeWidget>("styleElements");
    QCOMPARE(tree->topLevelItemCount(), 9);
    QCOMPARE(elementItem(QStringLiteral("split"))->checkState(0), Qt::Checked);
    QVERIFY(!elementItem(QStringLiteral("split"))->text(1).isEmpty());

    child<QListWidget>("styleList")->setCurrentRow(1);
    QCOMPARE(child<QLineEdit>("styleName")->text(), QStringLiteral("Bilder"));
    QVERIFY(!m_dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->isEnabled());
}

void WhaleStylesSettingsPageTest::testAddFromWindow()
{
    saveStyle(QStringLiteral("New Style"));
    openDialog();
    m_mainWindow->activeViewContainer()->view()->setViewMode(DolphinView::CompactView);

    child<QPushButton>("addStyle")->click();
    QCOMPARE(listedNames(), (QStringList{QStringLiteral("New Style"), QStringLiteral("New Style 2")}));
    QCOMPARE(child<QLineEdit>("styleName")->text(), QStringLiteral("New Style 2"));
    QVERIFY(WhaleStyles::self()->store().ids().size() == 1);

    apply();
    QCOMPARE(WhaleStyles::self()->store().ids(), (QStringList{QStringLiteral("new-style"), QStringLiteral("new-style-2")}));
    QVERIFY(WhaleStyles::self()
                ->store()
                .style(QStringLiteral("new-style-2"))
                ->elements.value(QStringLiteral("viewStyle.left"))
                .value.contains(QLatin1String("ViewMode=2")));
    // The window's menu follows.
    QVERIFY(m_mainWindow->actionCollection()->action(WhaleStyleManager::actionName(QStringLiteral("new-style-2"))));
}

void WhaleStylesSettingsPageTest::testRenameKeepsId()
{
    const QString id = saveStyle(QStringLiteral("Schlicht"));
    openDialog();
    auto name = child<QLineEdit>("styleName");
    name->setText(QStringLiteral("Ganz schlicht"));
    Q_EMIT name->editingFinished();
    QCOMPARE(listedNames(), QStringList{QStringLiteral("Ganz schlicht")});

    // An empty name is refused.
    name->setText(QStringLiteral("  "));
    Q_EMIT name->editingFinished();
    QCOMPARE(name->text(), QStringLiteral("Ganz schlicht"));

    apply();
    QCOMPARE(WhaleStyles::self()->store().ids(), QStringList{id});
    QCOMPARE(WhaleStyles::self()->store().style(id)->name, QStringLiteral("Ganz schlicht"));
    QCOMPARE(m_mainWindow->actionCollection()->action(WhaleStyleManager::actionName(id))->text(), QStringLiteral("Ganz schlicht"));
}

void WhaleStylesSettingsPageTest::testElementsOnOff()
{
    const QString id = saveStyle(QStringLiteral("Bilder"));
    openDialog();
    elementItem(QStringLiteral("panels"))->setCheckState(0, Qt::Unchecked);
    apply();
    QVERIFY(!WhaleStyles::self()->store().style(id)->elements.value(QStringLiteral("panels")).enabled);
    QVERIFY(WhaleStyles::self()->store().style(id)->elements.value(QStringLiteral("split")).enabled);

    elementItem(QStringLiteral("panels"))->setCheckState(0, Qt::Checked);
    apply();
    QVERIFY(WhaleStyles::self()->store().style(id)->elements.value(QStringLiteral("panels")).enabled);
}

void WhaleStylesSettingsPageTest::testOrderAndDelete()
{
    saveStyle(QStringLiteral("A"));
    saveStyle(QStringLiteral("B"));
    saveStyle(QStringLiteral("C"));
    openDialog();
    auto list = child<QListWidget>("styleList");
    QVERIFY(!child<QPushButton>("moveStyleUp")->isEnabled());

    list->setCurrentRow(2);
    child<QPushButton>("moveStyleUp")->click();
    QCOMPARE(listedNames(), (QStringList{QStringLiteral("A"), QStringLiteral("C"), QStringLiteral("B")}));
    QCOMPARE(list->currentItem()->text(), QStringLiteral("C"));
    child<QPushButton>("moveStyleUp")->click();
    QVERIFY(!child<QPushButton>("moveStyleUp")->isEnabled());
    list->setCurrentRow(1);
    child<QPushButton>("removeStyle")->click();
    QCOMPARE(listedNames(), (QStringList{QStringLiteral("C"), QStringLiteral("B")}));
    QCOMPARE(list->currentItem()->text(), QStringLiteral("B"));

    apply();
    QCOMPARE(WhaleStyles::self()->store().ids(), (QStringList{QStringLiteral("c"), QStringLiteral("b")}));
    QVERIFY(!m_mainWindow->actionCollection()->action(WhaleStyleManager::actionName(QStringLiteral("a"))));
}

void WhaleStylesSettingsPageTest::testCancelDiscards()
{
    saveStyle(QStringLiteral("A"));
    openDialog();
    child<QPushButton>("removeStyle")->click();
    QVERIFY(listedNames().isEmpty());
    QVERIFY(!child<QPushButton>("removeStyle")->isEnabled());
    m_dialog->reject();
    QCOMPARE(WhaleStyles::self()->store().ids(), QStringList{QStringLiteral("a")});
}

void WhaleStylesSettingsPageTest::testShortcut()
{
    const QString a = saveStyle(QStringLiteral("A"));
    WhaleStyle style = *WhaleStyles::self()->store().style(a);
    style.shortcut = QKeySequence(QStringLiteral("Ctrl+Alt+1"));
    WhaleStyles::self()->editableStore().updateStyle(style);
    WhaleStyles::self()->save();
    const QString b = saveStyle(QStringLiteral("B"));
    openDialog();
    child<QListWidget>("styleList")->setCurrentRow(1);
    auto shortcut = child<KKeySequenceWidget>("styleShortcut");

    // Used by another style, by an action of the window: refused with a message.
    int refusals = 0;
    auto closeMessages = [&refusals]() {
        if (QWidget *box = QApplication::activeModalWidget()) {
            ++refusals;
            box->close();
        }
    };
    for (const QString &used : {QStringLiteral("Ctrl+Alt+1"), QStringLiteral("Ctrl+N")}) {
        QTimer::singleShot(0, closeMessages);
        shortcut->setKeySequence(QKeySequence(used));
        QCOMPARE(shortcut->keySequence(), QKeySequence());
    }
    QCOMPARE(refusals, 2);

    shortcut->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+2")));
    apply();
    QCOMPARE(WhaleStyles::self()->store().style(b)->shortcut, QKeySequence(QStringLiteral("Ctrl+Alt+2")));
    QCOMPARE(m_mainWindow->actionCollection()->action(WhaleStyleManager::actionName(b))->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+2")));

    // The style's own shortcut is no conflict; giving A's shortcut to B after removing it from A is fine.
    child<QListWidget>("styleList")->setCurrentRow(0);
    shortcut->setKeySequence(QKeySequence());
    child<QListWidget>("styleList")->setCurrentRow(1);
    shortcut->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+1")));
    QCOMPARE(shortcut->keySequence(), QKeySequence(QStringLiteral("Ctrl+Alt+1")));
    apply();
    QVERIFY(WhaleStyles::self()->store().style(a)->shortcut.isEmpty());
    QCOMPARE(WhaleStyles::self()->store().style(b)->shortcut, QKeySequence(QStringLiteral("Ctrl+Alt+1")));
}

void WhaleStylesSettingsPageTest::testCaptureAgain()
{
    DolphinView *view = m_mainWindow->activeViewContainer()->view();
    view->setViewMode(DolphinView::IconsView);
    const QString id = saveStyle(QStringLiteral("A"));
    openDialog();
    elementItem(QStringLiteral("panels"))->setCheckState(0, Qt::Unchecked);
    const QString before = elementItem(QStringLiteral("viewStyle.left"))->text(1);

    view->setViewMode(DolphinView::DetailsView);
    child<QPushButton>("captureStyle")->click();
    QVERIFY(elementItem(QStringLiteral("viewStyle.left"))->text(1) != before);
    QVERIFY(elementItem(QStringLiteral("viewStyle.left"))->text(1).contains(i18nc("@item:intext view mode", "Details")));
    // Whether an element is set stays.
    QCOMPARE(elementItem(QStringLiteral("panels"))->checkState(0), Qt::Unchecked);

    apply();
    QVERIFY(WhaleStyles::self()->store().style(id)->elements.value(QStringLiteral("viewStyle.left")).value.contains(QLatin1String("ViewMode=1")));
}

void WhaleStylesSettingsPageTest::testApplyToWindowKeepsStatusBarOnOk()
{
    DolphinView *view = m_mainWindow->activeViewContainer()->view();
    view->setViewMode(DolphinView::CompactView);
    GeneralSettings::setShowStatusBar(GeneralSettings::EnumShowStatusBar::Disabled);
    GeneralSettings::self()->save();
    saveStyle(QStringLiteral("Schlicht"));
    view->setViewMode(DolphinView::IconsView);
    GeneralSettings::setShowStatusBar(GeneralSettings::EnumShowStatusBar::Small);
    GeneralSettings::self()->save();

    openDialog();
    // Works at once, also with changes not saved yet.
    elementItem(QStringLiteral("panels"))->setCheckState(0, Qt::Unchecked);
    child<QPushButton>("applyStyle")->click();
    QCOMPARE(view->viewMode(), DolphinView::CompactView);
    QCOMPARE(GeneralSettings::showStatusBar(), GeneralSettings::EnumShowStatusBar::Disabled);

    // OK of the dialog: the page of the status bar does not set its former value again.
    m_dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    QCOMPARE(GeneralSettings::showStatusBar(), GeneralSettings::EnumShowStatusBar::Disabled);
}

void WhaleStylesSettingsPageTest::testUnavailableInFolderMode()
{
    saveStyle(QStringLiteral("A"));
    GeneralSettings::self()->setGlobalViewProps(false);
    openDialog();
    QTreeWidgetItem *item = elementItem(QStringLiteral("viewStyle.left"));
    QVERIFY(item);
    QVERIFY(!(item->flags() & Qt::ItemIsEnabled));
    QVERIFY(!item->toolTip(0).isEmpty());
    QVERIFY(elementItem(QStringLiteral("split"))->flags() & Qt::ItemIsEnabled);
}

QTEST_MAIN(WhaleStylesSettingsPageTest)

#include "whalestylessettingspagetest.moc"
