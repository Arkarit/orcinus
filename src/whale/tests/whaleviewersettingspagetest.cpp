/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The "Viewer" page of the settings dialog (V1c, .privatedoc/whale-viewer-plan.md).
 */

#include "whaleviewersettingspage.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "settings/dolphinsettingsdialog.h"
#include "testdir.h"
#include "testhelpers.h"
#include "whaleviewerlist.h"
#include "whaleviewersettings.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QBuffer>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QImage>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

class WhaleViewerSettingsPageTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void testImageMimeTypes();
    void testDefaults();
    void testTurnOffOneType();
    void testTurnOffAllImages();
    void testAllTypesBecomeTheGroup();
    void testOtherPatternsKept();
    void testWrapAround();
    void testUpscale();
    void testSmoothZoom();
    void testRestoreDefaults();
    void testChangedOnlyByTheUser();
    void testPageInDialog();
    void testVideos();

private:
    QTreeWidgetItem *group(WhaleViewerSettingsPage &page, const QString &pattern) const;
    QTreeWidgetItem *images(WhaleViewerSettingsPage &page) const;
    QTreeWidgetItem *videos(WhaleViewerSettingsPage &page) const;
    QCheckBox *checkBox(WhaleViewerSettingsPage &page, const QString &name) const;
    /// What the page writes with all types on: images, and videos when built with libmpv.
    QStringList allTypes() const;
    QTreeWidgetItem *type(WhaleViewerSettingsPage &page, const QString &name) const;
    QCheckBox *wrapAround(WhaleViewerSettingsPage &page) const;
    QCheckBox *upscale(WhaleViewerSettingsPage &page) const;
    QCheckBox *smoothZoom(WhaleViewerSettingsPage &page) const;

    QTemporaryDir m_tmpHome;
};

void WhaleViewerSettingsPageTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhaleViewerSettingsPageTest::init()
{
    KSharedConfig::openConfig()->deleteGroup(QStringLiteral("Viewer"));
}

QTreeWidgetItem *WhaleViewerSettingsPageTest::images(WhaleViewerSettingsPage &page) const
{
    return group(page, QStringLiteral("image/*"));
}

QTreeWidgetItem *WhaleViewerSettingsPageTest::videos(WhaleViewerSettingsPage &page) const
{
    return group(page, QStringLiteral("video/*"));
}

QTreeWidgetItem *WhaleViewerSettingsPageTest::group(WhaleViewerSettingsPage &page, const QString &pattern) const
{
    auto tree = page.findChild<QTreeWidget *>();
    for (int i = 0; tree && i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(1) == pattern) {
            return tree->topLevelItem(i);
        }
    }
    return nullptr;
}

QCheckBox *WhaleViewerSettingsPageTest::checkBox(WhaleViewerSettingsPage &page, const QString &name) const
{
    return page.findChild<QCheckBox *>(name);
}

QStringList WhaleViewerSettingsPageTest::allTypes() const
{
    QStringList result{QStringLiteral("image/*")};
    if (WhaleViewerSettings::videoSupported()) {
        result.append(QStringLiteral("video/*"));
    }
    return result;
}

QTreeWidgetItem *WhaleViewerSettingsPageTest::type(WhaleViewerSettingsPage &page, const QString &name) const
{
    for (QTreeWidgetItem *group : {images(page), videos(page)}) {
        for (int i = 0; group && i < group->childCount(); ++i) {
            if (group->child(i)->text(1) == name) {
                return group->child(i);
            }
        }
    }
    return nullptr;
}

QCheckBox *WhaleViewerSettingsPageTest::wrapAround(WhaleViewerSettingsPage &page) const
{
    return page.findChild<QCheckBox *>(QStringLiteral("wrapAround"));
}

QCheckBox *WhaleViewerSettingsPageTest::upscale(WhaleViewerSettingsPage &page) const
{
    return page.findChild<QCheckBox *>(QStringLiteral("upscale"));
}

QCheckBox *WhaleViewerSettingsPageTest::smoothZoom(WhaleViewerSettingsPage &page) const
{
    return page.findChild<QCheckBox *>(QStringLiteral("smoothZoom"));
}

void WhaleViewerSettingsPageTest::testImageMimeTypes()
{
    const QStringList types = WhaleViewerSettingsPage::imageMimeTypes();
    QVERIFY(types.contains(QStringLiteral("image/png")));
    QVERIFY(types.contains(QStringLiteral("image/jpeg")));
    QCOMPARE(QSet<QString>(types.cbegin(), types.cend()).count(), types.count());
    QStringList sorted = types;
    sorted.sort();
    QCOMPARE(types, sorted);
}

void WhaleViewerSettingsPageTest::testDefaults()
{
    WhaleViewerSettingsPage page;
    QTreeWidgetItem *group = images(page);
    QVERIFY(group);
    QCOMPARE(group->text(1), QStringLiteral("image/*"));
    QCOMPARE(group->childCount(), WhaleViewerSettingsPage::imageMimeTypes().count());
    QCOMPARE(group->checkState(0), Qt::Checked);
    QCOMPARE(type(page, QStringLiteral("image/png"))->checkState(0), Qt::Checked);
    QVERIFY(!type(page, QStringLiteral("image/png"))->text(0).isEmpty());
    QVERIFY(!wrapAround(page)->isChecked());
    QVERIFY(!upscale(page)->isChecked());
    QVERIFY(!smoothZoom(page)->isChecked());
}

void WhaleViewerSettingsPageTest::testTurnOffOneType()
{
    {
        WhaleViewerSettingsPage page;
        type(page, QStringLiteral("image/png"))->setCheckState(0, Qt::Unchecked);
        QCOMPARE(images(page)->checkState(0), Qt::PartiallyChecked);
        page.applySettings();
    }
    const QStringList mimeTypes = WhaleViewerSettings::mimeTypes();
    QVERIFY(!mimeTypes.contains(QStringLiteral("image/*")));
    QVERIFY(!mimeTypes.contains(QStringLiteral("image/png")));
    QVERIFY(mimeTypes.contains(QStringLiteral("image/jpeg")));
    // And the videos as their group, built with libmpv.
    QCOMPARE(mimeTypes.count(), WhaleViewerSettingsPage::imageMimeTypes().count() - 1 + allTypes().count() - 1);

    // The viewer follows: PNG files open with their application again.
    TestDir dir;
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "png");
    dir.createFile(QStringLiteral("a.png"), png);
    QVERIFY(!WhaleViewerList::isDisplayable(KFileItem(QUrl::fromLocalFile(dir.path() + QStringLiteral("/a.png"))), mimeTypes));

    // Shown as stored.
    WhaleViewerSettingsPage again;
    QCOMPARE(type(again, QStringLiteral("image/png"))->checkState(0), Qt::Unchecked);
    QCOMPARE(type(again, QStringLiteral("image/jpeg"))->checkState(0), Qt::Checked);
    QCOMPARE(images(again)->checkState(0), Qt::PartiallyChecked);
}

void WhaleViewerSettingsPageTest::testTurnOffAllImages()
{
    {
        WhaleViewerSettingsPage page;
        images(page)->setCheckState(0, Qt::Unchecked);
        QCOMPARE(type(page, QStringLiteral("image/jpeg"))->checkState(0), Qt::Unchecked);
        page.applySettings();
    }
    // Videos stay on, built with libmpv.
    QCOMPARE(WhaleViewerSettings::mimeTypes(), allTypes().mid(1));
    WhaleViewerSettingsPage again;
    QCOMPARE(images(again)->checkState(0), Qt::Unchecked);
}

void WhaleViewerSettingsPageTest::testAllTypesBecomeTheGroup()
{
    WhaleViewerSettings::setMimeTypes({QStringLiteral("image/jpeg")});
    {
        WhaleViewerSettingsPage page;
        QCOMPARE(type(page, QStringLiteral("image/jpeg"))->checkState(0), Qt::Checked);
        QCOMPARE(type(page, QStringLiteral("image/png"))->checkState(0), Qt::Unchecked);
        images(page)->setCheckState(0, Qt::Checked);
        page.applySettings();
    }
    // All of them on is the group, so types that come later are on too.
    QCOMPARE(WhaleViewerSettings::mimeTypes(), QStringList{QStringLiteral("image/*")});
}

void WhaleViewerSettingsPageTest::testOtherPatternsKept()
{
    WhaleViewerSettings::setMimeTypes({QStringLiteral("audio/*"), QStringLiteral("image/png")});
    {
        WhaleViewerSettingsPage page;
        page.applySettings();
    }
    QCOMPARE(WhaleViewerSettings::mimeTypes(), (QStringList{QStringLiteral("audio/*"), QStringLiteral("image/png")}));
}

void WhaleViewerSettingsPageTest::testWrapAround()
{
    {
        WhaleViewerSettingsPage page;
        wrapAround(page)->setChecked(true);
        page.applySettings();
    }
    QVERIFY(WhaleViewerSettings::wrapAround());
    WhaleViewerSettingsPage again;
    QVERIFY(wrapAround(again)->isChecked());
}

void WhaleViewerSettingsPageTest::testUpscale()
{
    {
        WhaleViewerSettingsPage page;
        QSignalSpy changed(&page, &SettingsPageBase::changed);
        upscale(page)->setChecked(true);
        QCOMPARE(changed.count(), 1);
        page.applySettings();
    }
    QVERIFY(WhaleViewerSettings::upscale());
    WhaleViewerSettingsPage again;
    QVERIFY(upscale(again)->isChecked());
    again.restoreDefaults();
    QVERIFY(!upscale(again)->isChecked());
}

void WhaleViewerSettingsPageTest::testSmoothZoom()
{
    {
        WhaleViewerSettingsPage page;
        QSignalSpy changed(&page, &SettingsPageBase::changed);
        smoothZoom(page)->setChecked(true);
        QCOMPARE(changed.count(), 1);
        page.applySettings();
    }
    QVERIFY(WhaleViewerSettings::smoothZoom());
    WhaleViewerSettingsPage again;
    QVERIFY(smoothZoom(again)->isChecked());
    again.restoreDefaults();
    QVERIFY(!smoothZoom(again)->isChecked());
}

void WhaleViewerSettingsPageTest::testRestoreDefaults()
{
    WhaleViewerSettings::setMimeTypes({QStringLiteral("audio/*"), QStringLiteral("image/png")});
    WhaleViewerSettings::setWrapAround(true);
    WhaleViewerSettings::setMultiSelection(WhaleViewerSettings::MultiSelection::MoveSelection);
    WhaleViewerSettingsPage page;
    QSignalSpy changed(&page, &SettingsPageBase::changed);
    page.restoreDefaults();
    QVERIFY(changed.count() > 0);
    QCOMPARE(images(page)->checkState(0), Qt::Checked);
    QVERIFY(!wrapAround(page)->isChecked());
    page.applySettings();
    QCOMPARE(WhaleViewerSettings::mimeTypes(), QStringList{QStringLiteral("audio/*")} + allTypes());
    QVERIFY(!WhaleViewerSettings::wrapAround());
    // The hidden experiment switch is not the page's.
    QCOMPARE(WhaleViewerSettings::multiSelection(), WhaleViewerSettings::MultiSelection::MoveSelection);
}

void WhaleViewerSettingsPageTest::testChangedOnlyByTheUser()
{
    WhaleViewerSettings::setMimeTypes({QStringLiteral("image/png")});
    WhaleViewerSettings::setWrapAround(true);
    WhaleViewerSettingsPage page;
    QSignalSpy changed(&page, &SettingsPageBase::changed);
    page.applySettings();
    QCOMPARE(changed.count(), 0);
    type(page, QStringLiteral("image/jpeg"))->setCheckState(0, Qt::Checked);
    QVERIFY(changed.count() > 0);
    changed.clear();
    wrapAround(page)->setChecked(false);
    QCOMPARE(changed.count(), 1);
}

void WhaleViewerSettingsPageTest::testPageInDialog()
{
    TestDir dir;
    dir.createFile(QStringLiteral("a.txt"));
    DolphinMainWindow mainWindow;
    mainWindow.openDirectories({dir.url()}, false);
    DolphinSettingsDialog dialog(dir.url(), &mainWindow, mainWindow.actionCollection());
    auto page = dialog.findChild<WhaleViewerSettingsPage *>();
    QVERIFY(page);
    // The icon of the page comes from the resources of the static library.
    QVERIFY(!WhaleViewerSettingsPage::icon().pixmap(32, 32).isNull());

    QPushButton *apply = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply);
    QVERIFY(!apply->isEnabled());
    wrapAround(*page)->setChecked(true);
    QVERIFY(apply->isEnabled());
    apply->click();
    QVERIFY(WhaleViewerSettings::wrapAround());
}

void WhaleViewerSettingsPageTest::testVideos()
{
    if (!WhaleViewerSettings::videoSupported()) {
        WhaleViewerSettingsPage page;
        QVERIFY(!videos(page));
        QVERIFY(!checkBox(page, QStringLiteral("videoAutoplay")));
        QSKIP("built without libmpv");
    }
    QVERIFY(WhaleViewerSettingsPage::videoMimeTypes().contains(QStringLiteral("video/mp4")));
    QVERIFY(!WhaleViewerSettingsPage::videoMimeTypes().contains(QStringLiteral("image/png")));
    {
        WhaleViewerSettingsPage page;
        QVERIFY(videos(page));
        QCOMPARE(videos(page)->checkState(0), Qt::Checked);
        QCOMPARE(type(page, QStringLiteral("video/mp4"))->checkState(0), Qt::Checked);
        for (const QString &name : {QStringLiteral("videoAutoplay"), QStringLiteral("videoSound"), QStringLiteral("videoLoop")}) {
            QVERIFY(checkBox(page, name)->isChecked());
        }
        // Only MP4 of the videos, no sound, once.
        videos(page)->setCheckState(0, Qt::Unchecked);
        type(page, QStringLiteral("video/mp4"))->setCheckState(0, Qt::Checked);
        checkBox(page, QStringLiteral("videoSound"))->setChecked(false);
        checkBox(page, QStringLiteral("videoLoop"))->setChecked(false);
        page.applySettings();
    }
    QCOMPARE(WhaleViewerSettings::mimeTypes(), (QStringList{QStringLiteral("image/*"), QStringLiteral("video/mp4")}));
    QVERIFY(WhaleViewerSettings::videoAutoplay());
    QVERIFY(!WhaleViewerSettings::videoSound());
    QVERIFY(!WhaleViewerSettings::videoLoop());

    WhaleViewerSettingsPage again;
    QCOMPARE(videos(again)->checkState(0), Qt::PartiallyChecked);
    QVERIFY(!checkBox(again, QStringLiteral("videoSound"))->isChecked());
    again.restoreDefaults();
    QCOMPARE(videos(again)->checkState(0), Qt::Checked);
    QVERIFY(checkBox(again, QStringLiteral("videoSound"))->isChecked());
    QVERIFY(checkBox(again, QStringLiteral("videoLoop"))->isChecked());
}

QTEST_MAIN(WhaleViewerSettingsPageTest)

#include "whaleviewersettingspagetest.moc"
