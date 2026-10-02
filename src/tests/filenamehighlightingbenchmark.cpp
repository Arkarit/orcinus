/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "testdir.h"
#include "views/dolphinfileitemlistwidget.h"
#include "views/dolphinitemlistview.h"
#include "views/filenamehighlighter.h"
#include "views/filenamehighlightingsettings.h"

#include <KIO/UDSEntry>

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <sys/stat.h>

using Condition = FileNameHighlightRule::Condition;

namespace
{
constexpr int syntheticItemCount = 50000;
constexpr int directoryFileCount = 20000;

// A mix of names, file types and permissions similar to a large mixed folder.
const QStringList suffixes = {
    QStringLiteral("txt"),
    QStringLiteral("cpp"),
    QStringLiteral("h"),
    QStringLiteral("jpg"),
    QStringLiteral("png"),
    QStringLiteral("mp3"),
    QStringLiteral("mp4"),
    QStringLiteral("pdf"),
    QStringLiteral("tar.gz"),
    QStringLiteral("exe"),
    QString(),
};

QString fileName(int i)
{
    const QString suffix = suffixes.at(i % suffixes.count());
    const QString base = QStringLiteral("file-%1-with-a-moderately-long-name").arg(i, 6, 10, QLatin1Char('0'));
    return suffix.isEmpty() ? base : base + QLatin1Char('.') + suffix;
}

KFileItemList createSyntheticItems()
{
    KFileItemList items;
    items.reserve(syntheticItemCount);
    for (int i = 0; i < syntheticItemCount; ++i) {
        const QString name = fileName(i);
        KIO::UDSEntry entry;
        entry.fastInsert(KIO::UDSEntry::UDS_NAME, name);
        const bool isDir = i % 20 == 0;
        entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, isDir ? S_IFDIR : S_IFREG);
        entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, (isDir || i % 5 == 0) ? 0755 : 0644);
        if (i % 25 == 3) {
            entry.fastInsert(KIO::UDSEntry::UDS_LINK_DEST, QStringLiteral("target"));
        }
        items.append(KFileItem(entry, QUrl::fromLocalFile(QStringLiteral("/benchmark/") + name)));
    }
    return items;
}

FileNameHighlightRule rule(Condition condition, const QStringList &patterns, bool bold)
{
    FileNameHighlightRule result;
    result.condition = condition;
    result.patterns = patterns;
    if (bold) {
        result.style.bold = true;
    } else {
        result.style.color = FileNameColor::custom(QColor(10, 120, 200));
    }
    return result;
}

QList<FileNameHighlightRule> ruleSet(const QString &name)
{
    QList<FileNameHighlightRule> rules = FileNameHighlightingSettings::defaultRules();
    if (name == QLatin1String("flags only")) {
        rules.removeLast();
    } else if (name == QLatin1String("defaults + 20 name patterns")) {
        for (int i = 0; i < 20; ++i) {
            rules.append(rule(Condition::NamePattern, {QStringLiteral("*.ext%1").arg(i), QStringLiteral("name%1*").arg(i)}, false));
        }
        rules.append(rule(Condition::NamePattern, {QStringLiteral("*.cpp"), QStringLiteral("*.h")}, false));
    } else if (name == QLatin1String("defaults + 20 MIME types")) {
        for (int i = 0; i < 20; ++i) {
            rules.append(rule(Condition::MimeType, {QStringLiteral("application/x-benchmark-%1").arg(i)}, false));
        }
        rules.append(rule(Condition::MimeType, {QStringLiteral("text/plain")}, false));
    } else if (name == QLatin1String("bold MIME rule")) {
        rules.append(rule(Condition::MimeType, {QStringLiteral("application/pdf")}, true));
    }
    return rules;
}

std::shared_ptr<const FileNameHighlighter> highlighter(const QString &ruleSetName)
{
    const bool enabled = ruleSetName != QLatin1String("disabled");
    return std::make_shared<const FileNameHighlighter>(enabled, ruleSet(ruleSetName), [](const QString &) {
        return KFileSystemType::Other;
    });
}

void addRuleSetRows(bool withWanted)
{
    QTest::addColumn<QString>("ruleSet");
    if (withWanted) {
        QTest::addColumn<bool>("fontOnly");
    }
    const QStringList ruleSets = {
        QStringLiteral("disabled"),
        QStringLiteral("defaults"),
        QStringLiteral("flags only"),
        QStringLiteral("defaults + 20 name patterns"),
        QStringLiteral("defaults + 20 MIME types"),
        QStringLiteral("bold MIME rule"),
    };
    for (const QString &name : ruleSets) {
        if (withWanted) {
            QTest::addRow("%s, all attributes", qPrintable(name)) << name << false;
            QTest::addRow("%s, font only", qPrintable(name)) << name << true;
        } else {
            QTest::addRow("%s", qPrintable(name)) << name;
        }
    }
}
}

class FileNameHighlightingBenchmark : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void styleForCold_data();
    void styleForCold();
    void styleForWarm_data();
    void styleForWarm();

    void sizeHints_data();
    void sizeHints();

    void iconNameChangesOfAllItems_data();
    void iconNameChangesOfAllItems();

private:
    void createView(KStandardItemListView::ItemLayout layout);
    void destroyView();

    TestDir *m_testDir = nullptr;
    KFileItemModel *m_model = nullptr;
    DolphinItemListView *m_view = nullptr;
    KItemListContainer *m_container = nullptr;
};

void FileNameHighlightingBenchmark::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);

    m_testDir = new TestDir();
    for (int i = 0; i < directoryFileCount; ++i) {
        const QString path = m_testDir->filePath(fileName(i));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        if (i % 5 == 0) {
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
    }
}

void FileNameHighlightingBenchmark::cleanupTestCase()
{
    delete m_testDir;
    m_testDir = nullptr;
}

void FileNameHighlightingBenchmark::createView(KStandardItemListView::ItemLayout layout)
{
    m_model = new KFileItemModel();
    m_view = new DolphinItemListView();
    m_view->setItemLayout(layout);
    auto controller = new KItemListController(m_model, m_view, nullptr);
    m_container = new KItemListContainer(controller);
    m_container->resize(1200, 800);
    m_container->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_container));

    QSignalSpy loadingCompleted(m_model, &KFileItemModel::directoryLoadingCompleted);
    m_model->loadDirectory(m_testDir->url());
    QVERIFY(loadingCompleted.wait(60000));
    QCOMPARE(m_model->count(), directoryFileCount);
}

void FileNameHighlightingBenchmark::destroyView()
{
    delete m_container;
    m_container = nullptr;
    m_view = nullptr;
    m_model = nullptr;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void FileNameHighlightingBenchmark::styleForCold_data()
{
    addRuleSetRows(true);
}

void FileNameHighlightingBenchmark::styleForCold()
{
    QFETCH(QString, ruleSet);
    QFETCH(bool, fontOnly);

    // Fresh items and a fresh highlighter: no cached MIME types, matches or colors.
    const KFileItemList items = createSyntheticItems();
    const auto h = highlighter(ruleSet);
    const auto wanted = fontOnly ? FileNameHighlighter::Font : FileNameHighlighter::AllAttributes;

    int styled = 0;
    QBENCHMARK_ONCE {
        for (const KFileItem &item : items) {
            styled += h->styleFor(item, wanted).isEmpty() ? 0 : 1;
        }
    }
    qDebug() << "styled items:" << styled << "of" << items.count();
}

void FileNameHighlightingBenchmark::styleForWarm_data()
{
    addRuleSetRows(true);
}

void FileNameHighlightingBenchmark::styleForWarm()
{
    QFETCH(QString, ruleSet);
    QFETCH(bool, fontOnly);

    const KFileItemList items = createSyntheticItems();
    const auto h = highlighter(ruleSet);
    const auto wanted = fontOnly ? FileNameHighlighter::Font : FileNameHighlighter::AllAttributes;
    for (const KFileItem &item : items) {
        h->styleFor(item, wanted);
    }

    // Repeated evaluation, e.g. while scrolling.
    QBENCHMARK {
        for (const KFileItem &item : items) {
            h->styleFor(item, wanted);
        }
    }
}

void FileNameHighlightingBenchmark::sizeHints_data()
{
    QTest::addColumn<QString>("ruleSet");
    QTest::addColumn<int>("layout");
    for (const QString &name : {QStringLiteral("disabled"), QStringLiteral("defaults"), QStringLiteral("bold MIME rule")}) {
        QTest::addRow("compact, %s", qPrintable(name)) << name << int(KStandardItemListView::CompactLayout);
        QTest::addRow("icons, %s", qPrintable(name)) << name << int(KStandardItemListView::IconsLayout);
        QTest::addRow("details column width, %s", qPrintable(name)) << name << int(KStandardItemListView::DetailsLayout);
    }
}

void FileNameHighlightingBenchmark::sizeHints()
{
    QFETCH(QString, ruleSet);
    QFETCH(int, layout);

    createView(static_cast<KStandardItemListView::ItemLayout>(layout));
    m_view->setFileNameHighlighter(highlighter(ruleSet));
    std::unique_ptr<KItemListWidgetInformant> informant(DolphinFileItemListWidget::createInformant());

    // Size hints of all items, as calculated for a newly loaded folder.
    QBENCHMARK {
        if (layout == KStandardItemListView::DetailsLayout) {
            qreal width = 0;
            for (int i = 0; i < m_model->count(); ++i) {
                width = qMax(width, informant->preferredRoleColumnWidth("text", i, m_view));
            }
        } else {
            QVector<std::pair<qreal, bool>> hints(m_model->count(), {0.0, false});
            qreal logicalWidth = 0.0;
            informant->calculateItemSizeHints(hints, logicalWidth, m_view);
        }
    }

    destroyView();
}

void FileNameHighlightingBenchmark::iconNameChangesOfAllItems_data()
{
    QTest::addColumn<QString>("ruleSet");
    QTest::addRow("disabled") << QStringLiteral("disabled");
    QTest::addRow("defaults") << QStringLiteral("defaults");
}

void FileNameHighlightingBenchmark::iconNameChangesOfAllItems()
{
    QFETCH(QString, ruleSet);

    createView(KStandardItemListView::CompactLayout);
    m_view->setFileNameHighlighter(highlighter(ruleSet));
    QCoreApplication::processEvents();

    // While a folder loads, the icons of all items get resolved. With rules that
    // change the font, every changed icon name recalculates the size of its item.
    QBENCHMARK {
        for (int i = 0; i < m_model->count(); i += 200) {
            Q_EMIT m_model->itemsChanged(KItemRangeList() << KItemRange(i, qMin(200, m_model->count() - i)), {"iconName"});
        }
        QCoreApplication::processEvents();
    }

    destroyView();
}

QTEST_MAIN(FileNameHighlightingBenchmark)

#include "filenamehighlightingbenchmark.moc"
