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

private:
    DolphinViewContainer *showMainWindow();
    static FilterBar *filterBar(DolphinViewContainer *container);
    static QToolButton *lockButton(DolphinViewContainer *container);
    static QLineEdit *filterInput(DolphinViewContainer *container);

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

QTEST_MAIN(WhaleFilterBarTest)

#include "whalefilterbartest.moc"
