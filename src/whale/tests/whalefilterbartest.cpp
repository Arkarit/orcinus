/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphinviewcontainer.h"
#include "filterbar/filterbar.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QLineEdit>
#include <QMenu>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>

/**
 * Whale's changes of the filter bar (FB, .privatedoc/whale-filter-bar-plan.md).
 */
class WhaleFilterBarTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testLockOnByDefault();
    void testLockKeepsFilterWhenChangingFolders();
    void testUnlockedClearsFilter();
    void testClosingKeepsLock();
    void testLockRemembered();

    void testScope_data();
    void testScope();
    void testScopeFromMenuRemembered();

private:
    DolphinViewContainer *showMainWindow();
    static FilterBar *filterBar(DolphinViewContainer *container);
    static QToolButton *lockButton(DolphinViewContainer *container);
    static QLineEdit *filterInput(DolphinViewContainer *container);
    static QStringList shownNames(DolphinViewContainer *container);

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
};

void WhaleFilterBarTest::initTestCase()
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
    GeneralSettings::self()->save();
}

void WhaleFilterBarTest::init()
{
    KSharedConfig::openStateConfig()->deleteGroup(QStringLiteral("FilterBar"));
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"));
    m_dir->createFile(QStringLiteral("b.jpg"));
    m_dir->createFile(QStringLiteral("sub/c.txt"));
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleFilterBarTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
}

DolphinViewContainer *WhaleFilterBarTest::showMainWindow()
{
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1000, 700);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowExposed(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 3;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

FilterBar *WhaleFilterBarTest::filterBar(DolphinViewContainer *container)
{
    return container->findChild<FilterBar *>();
}

QToolButton *WhaleFilterBarTest::lockButton(DolphinViewContainer *container)
{
    const auto buttons = filterBar(container)->findChildren<QToolButton *>();
    for (QToolButton *button : buttons) {
        if (button->toolTip() == i18nc("@info:tooltip", "Keep Filter When Changing Folders")) {
            return button;
        }
    }
    return nullptr;
}

QLineEdit *WhaleFilterBarTest::filterInput(DolphinViewContainer *container)
{
    return filterBar(container)->findChild<QLineEdit *>();
}

void WhaleFilterBarTest::testLockOnByDefault()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QVERIFY(lockButton(container));
    QVERIFY(lockButton(container)->isChecked());
}

void WhaleFilterBarTest::testLockKeepsFilterWhenChangingFolders()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->setFilterBarVisible(true);
    filterInput(container)->setText(QStringLiteral("txt"));

    container->setUrl(QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    QTRY_COMPARE(container->view()->url(), QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    QCOMPARE(filterInput(container)->text(), QStringLiteral("txt"));
}

void WhaleFilterBarTest::testUnlockedClearsFilter()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->setFilterBarVisible(true);
    filterInput(container)->setText(QStringLiteral("txt"));

    // Releasing the lock clears the filter, as in Dolphin; so does changing folders then.
    lockButton(container)->click();
    QVERIFY(!lockButton(container)->isChecked());
    QVERIFY(filterInput(container)->text().isEmpty());
    filterInput(container)->setText(QStringLiteral("txt"));
    container->setUrl(QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    QTRY_VERIFY(filterInput(container)->text().isEmpty());
}

void WhaleFilterBarTest::testClosingKeepsLock()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->setFilterBarVisible(true);
    filterInput(container)->setText(QStringLiteral("txt"));

    container->setFilterBarVisible(false);
    QVERIFY(filterInput(container)->text().isEmpty());
    QVERIFY(lockButton(container)->isChecked());

    container->setFilterBarVisible(true);
    lockButton(container)->click();
    container->setFilterBarVisible(false);
    QVERIFY(!lockButton(container)->isChecked());
}

void WhaleFilterBarTest::testLockRemembered()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    // The buttons of a hidden filter bar are disabled.
    container->setFilterBarVisible(true);
    lockButton(container)->click();
    QVERIFY(!lockButton(container)->isChecked());
    // Written at once, not only when the bar goes away.
    QVERIFY(!KSharedConfig::openStateConfig()->group(QStringLiteral("FilterBar")).readEntry("keepFilter", true));

    m_mainWindow.reset(new DolphinMainWindow());
    container = showMainWindow();
    QVERIFY(container);
    QVERIFY(!lockButton(container)->isChecked());
    container->setFilterBarVisible(true);
    lockButton(container)->click();
    QVERIFY(KSharedConfig::openStateConfig()->group(QStringLiteral("FilterBar")).readEntry("keepFilter", false));
}

QStringList WhaleFilterBarTest::shownNames(DolphinViewContainer *container)
{
    QStringList names;
    const KFileItemList items = container->view()->items();
    for (const KFileItem &item : items) {
        names.append(item.text());
    }
    names.sort();
    return names;
}

void WhaleFilterBarTest::testScope_data()
{
    using Scope = KFileItemModelFilter::Scope;
    QTest::addColumn<Scope>("scope");
    QTest::addColumn<int>("mode");
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QStringList>("shown");

    const auto glob = int(KFileItemModelFilter::Glob);
    const auto plain = int(KFileItemModelFilter::PlainText);
    // Files a.txt, b.jpg and the folder sub.
    QTest::newRow("files, glob") << Scope::Files << glob << QStringLiteral("*.jpg") << QStringList{QStringLiteral("b.jpg"), QStringLiteral("sub")};
    QTest::newRow("files, plain") << Scope::Files << plain << QStringLiteral("txt") << QStringList{QStringLiteral("a.txt"), QStringLiteral("sub")};
    QTest::newRow("folders, matching") << Scope::Folders << glob << QStringLiteral("su*")
                                       << QStringList{QStringLiteral("a.txt"), QStringLiteral("b.jpg"), QStringLiteral("sub")};
    QTest::newRow("folders, none") << Scope::Folders << glob << QStringLiteral("x*") << QStringList{QStringLiteral("a.txt"), QStringLiteral("b.jpg")};
    QTest::newRow("both, as Dolphin") << Scope::FilesAndFolders << glob << QStringLiteral("*.jpg") << QStringList{QStringLiteral("b.jpg")};
}

void WhaleFilterBarTest::testScope()
{
    QFETCH(KFileItemModelFilter::Scope, scope);
    QFETCH(int, mode);
    QFETCH(QString, pattern);
    QFETCH(QStringList, shown);

    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->setFilterBarVisible(true);
    filterBar(container)->setFilterScope(scope);
    container->view()->setFilterMode(static_cast<KFileItemModelFilter::FilterMode>(mode));
    QCOMPARE(container->view()->filterScope(), scope);
    filterInput(container)->setText(pattern);
    QTRY_COMPARE(shownNames(container), shown);
}

void WhaleFilterBarTest::testScopeFromMenuRemembered()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    // Files by default: the folders stay shown.
    QCOMPARE(filterBar(container)->filterScope(), KFileItemModelFilter::Scope::Files);
    QCOMPARE(container->view()->filterScope(), KFileItemModelFilter::Scope::Files);

    container->setFilterBarVisible(true);
    QToolButton *scopeButton = nullptr;
    const auto buttons = filterBar(container)->findChildren<QToolButton *>();
    for (QToolButton *button : buttons) {
        if (button->menu()) {
            scopeButton = button;
        }
    }
    QVERIFY(scopeButton);
    const auto entries = scopeButton->menu()->actions();
    QCOMPARE(entries.size(), 3);
    QVERIFY(entries.at(0)->isChecked());
    entries.at(2)->trigger();
    QCOMPARE(container->view()->filterScope(), KFileItemModelFilter::Scope::FilesAndFolders);
    QVERIFY(entries.at(2)->isChecked() && !entries.at(0)->isChecked());
    QVERIFY(scopeButton->toolTip().contains(entries.at(2)->text()));

    filterInput(container)->setText(QStringLiteral("*.jpg"));
    QTRY_COMPARE(shownNames(container), QStringList{QStringLiteral("b.jpg")});

    m_mainWindow.reset(new DolphinMainWindow());
    container = showMainWindow();
    QVERIFY(container);
    QCOMPARE(container->view()->filterScope(), KFileItemModelFilter::Scope::FilesAndFolders);
}

QTEST_MAIN(WhaleFilterBarTest)

#include "whalefilterbartest.moc"
