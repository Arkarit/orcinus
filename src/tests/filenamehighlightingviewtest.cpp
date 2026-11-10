/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
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

#include <KConfigGroup>

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStyleOptionGraphicsItem>
#include <QTest>

#include <sys/stat.h>

using Condition = FileNameHighlightRule::Condition;

namespace
{
class TestWidget : public DolphinFileItemListWidget
{
public:
    TestWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent)
        : DolphinFileItemListWidget(informant, parent)
    {
    }

    using DolphinFileItemListWidget::customizedFont;
    using DolphinFileItemListWidget::refreshCache;
    using KStandardItemListWidget::nameTextColor;
    using KStandardItemListWidget::textColor;
};

class TestView : public DolphinItemListView
{
public:
    using DolphinItemListView::itemSizeHintUpdateRequired;
    using KItemListView::visibleItemListWidgets;
};

FileNameHighlightRule rule(Condition condition, const FileNameTextStyle &style, const QStringList &patterns = {})
{
    FileNameHighlightRule result;
    result.condition = condition;
    result.patterns = patterns;
    result.style = style;
    return result;
}

const QColor executableColor(0, 0, 180);
const QColor mediaColor(150, 0, 150);

QList<FileNameHighlightRule> testRules()
{
    FileNameTextStyle executable;
    executable.bold = true;
    executable.color = FileNameColor::custom(executableColor);

    FileNameTextStyle symlink;
    symlink.italic = true;

    FileNameTextStyle media;
    media.color = FileNameColor::custom(mediaColor);

    return {
        rule(Condition::Executable, executable),
        rule(Condition::Symlink, symlink),
        rule(Condition::MimeType, media, {QStringLiteral("image/*")}),
    };
}

std::shared_ptr<const FileNameHighlighter> highlighter(bool enabled, const QList<FileNameHighlightRule> &rules = testRules())
{
    return std::make_shared<const FileNameHighlighter>(enabled, rules, [](const QString &) {
        return KFileSystemType::Other;
    });
}

QPalette paletteWithBase(const QColor &base, const QColor &text)
{
    QPalette palette;
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::Text, text);
    return palette;
}
}

class FileNameHighlightingViewTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testDisabledKeepsDolphinBehavior();
    void testEnabledWithoutRules();
    void testNameColorAndFont();
    void testHiddenItem();
    void testSelectedItem();
    void testVersionControlState();
    void testPaletteChange();
    void testSizeHintsUseHighlightedFont_data();
    void testSizeHintsUseHighlightedFont();
    void testPermissionChange();
    void testReadSettings();

private:
    void loadDirectory();
    TestWidget *widgetForName(const QString &name);

    TestDir *m_testDir = nullptr;
    KFileItemModel *m_model = nullptr;
    TestView *m_view = nullptr;
    KItemListContainer *m_container = nullptr;
};

void FileNameHighlightingViewTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void FileNameHighlightingViewTest::init()
{
    // Start from the defaults, which have highlighting disabled.
    KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("FileNameHighlighting")).deleteGroup();

    m_testDir = new TestDir();
    m_testDir->createFiles({QStringLiteral("plain.txt"), QStringLiteral("script.sh"), QStringLiteral("photo.png"), QStringLiteral(".hidden.sh")});
    const QFile::Permissions executable = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;
    QVERIFY(QFile::setPermissions(m_testDir->filePath(QStringLiteral("script.sh")), executable));
    QVERIFY(QFile::setPermissions(m_testDir->filePath(QStringLiteral(".hidden.sh")), executable));
    QVERIFY(QFile::link(m_testDir->filePath(QStringLiteral("plain.txt")), m_testDir->filePath(QStringLiteral("link.txt"))));

    m_model = new KFileItemModel();
    m_model->setShowHiddenFiles(true);
    m_view = new TestView();
    m_view->setWidgetCreator(new KItemListWidgetCreator<TestWidget>());
    m_view->setItemLayout(KStandardItemListView::DetailsLayout);
    m_view->setVisibleRoles({"text", "size"});

    // The container owns the controller, which owns the model and the view.
    auto controller = new KItemListController(m_model, m_view, nullptr);
    m_container = new KItemListContainer(controller);
    // Requires a model: palette changes reset the folder previews of the model.
    m_view->setPalette(paletteWithBase(Qt::white, Qt::black));
    m_container->resize(800, 600);
    m_container->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_container));

    loadDirectory();
}

void FileNameHighlightingViewTest::cleanup()
{
    delete m_container;
    m_container = nullptr;
    m_view = nullptr;
    m_model = nullptr;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    delete m_testDir;
    m_testDir = nullptr;
}

void FileNameHighlightingViewTest::loadDirectory()
{
    QSignalSpy loadingCompleted(m_model, &KFileItemModel::directoryLoadingCompleted);
    m_model->loadDirectory(m_testDir->url());
    QVERIFY(loadingCompleted.wait());
    QCOMPARE(m_model->count(), 5);
    QCoreApplication::processEvents();
}

TestWidget *FileNameHighlightingViewTest::widgetForName(const QString &name)
{
    const auto widgets = m_view->visibleItemListWidgets();
    for (KItemListWidget *widget : widgets) {
        if (widget->data().value("text").toString() == name) {
            // Painting refreshes the caches of dirty widgets.
            QImage image(widget->size().toSize(), QImage::Format_ARGB32);
            QPainter painter(&image);
            const QStyleOptionGraphicsItem option;
            widget->paint(&painter, &option, m_container->viewport());
            return static_cast<TestWidget *>(widget);
        }
    }
    return nullptr;
}

void FileNameHighlightingViewTest::testDisabledKeepsDolphinBehavior()
{
    m_view->setFileNameHighlighter(highlighter(false));
    const QFont baseFont = m_view->styleOption().font;

    TestWidget *script = widgetForName(QStringLiteral("script.sh"));
    QVERIFY(script);
    QVERIFY(!script->nameTextColor().isValid());
    QCOMPARE(script->customizedFont(baseFont), baseFont);

    // Dolphin's built-in italic font for symbolic links.
    TestWidget *link = widgetForName(QStringLiteral("link.txt"));
    QVERIFY(link);
    QVERIFY(link->customizedFont(baseFont).italic());
}

void FileNameHighlightingViewTest::testEnabledWithoutRules()
{
    m_view->setFileNameHighlighter(highlighter(true, {}));
    const QFont baseFont = m_view->styleOption().font;

    // The rules replace the built-in italic font for links.
    TestWidget *link = widgetForName(QStringLiteral("link.txt"));
    QVERIFY(link);
    QVERIFY(!link->customizedFont(baseFont).italic());
    QVERIFY(!link->nameTextColor().isValid());
}

void FileNameHighlightingViewTest::testNameColorAndFont()
{
    m_view->setFileNameHighlighter(highlighter(true));
    const QFont baseFont = m_view->styleOption().font;

    TestWidget *script = widgetForName(QStringLiteral("script.sh"));
    QVERIFY(script);
    QCOMPARE(script->nameTextColor(), executableColor);
    QCOMPARE(script->textColor(*m_container), executableColor);
    QVERIFY(script->customizedFont(baseFont).bold());

    TestWidget *photo = widgetForName(QStringLiteral("photo.png"));
    QVERIFY(photo);
    QCOMPARE(photo->nameTextColor(), mediaColor);
    QVERIFY(!photo->customizedFont(baseFont).bold());

    TestWidget *link = widgetForName(QStringLiteral("link.txt"));
    QVERIFY(link);
    QVERIFY(link->customizedFont(baseFont).italic());
    QVERIFY(!link->nameTextColor().isValid());

    TestWidget *plain = widgetForName(QStringLiteral("plain.txt"));
    QVERIFY(plain);
    QVERIFY(!plain->nameTextColor().isValid());
    QCOMPARE(plain->customizedFont(baseFont), baseFont);
}

void FileNameHighlightingViewTest::testHiddenItem()
{
    m_view->setFileNameHighlighter(highlighter(true));

    TestWidget *hidden = widgetForName(QStringLiteral(".hidden.sh"));
    QVERIFY(hidden);
    QCOMPARE(hidden->nameTextColor(), executableColor);
    // Hidden items show a faded variant of the name color.
    QVERIFY(hidden->textColor(*m_container) != executableColor);
}

void FileNameHighlightingViewTest::testSelectedItem()
{
    m_view->setFileNameHighlighter(highlighter(true));
    const QFont baseFont = m_view->styleOption().font;

    TestWidget *script = widgetForName(QStringLiteral("script.sh"));
    QVERIFY(script);
    script->setSelected(true);
    QVERIFY(script->textColor(*m_container) != executableColor);
    QVERIFY(script->customizedFont(baseFont).bold());
}

void FileNameHighlightingViewTest::testVersionControlState()
{
    m_view->setFileNameHighlighter(highlighter(true));
    const QFont baseFont = m_view->styleOption().font;

    TestWidget *script = widgetForName(QStringLiteral("script.sh"));
    QVERIFY(script);

    // A version control state with a meaning keeps its color.
    SmallHash data = script->data();
    data.insert("version", KVersionControlPlugin::LocallyModifiedVersion);
    script->setData(data, {"version"});
    script->refreshCache();
    QVERIFY(!script->nameTextColor().isValid());
    QVERIFY(script->textColor(*m_container) != executableColor);
    QVERIFY(script->customizedFont(baseFont).bold());

    // Unchanged files show the rule color.
    data.insert("version", KVersionControlPlugin::NormalVersion);
    script->setData(data, {"version"});
    script->refreshCache();
    QCOMPARE(script->nameTextColor(), executableColor);
}

void FileNameHighlightingViewTest::testPaletteChange()
{
    m_view->setFileNameHighlighter(highlighter(true));

    TestWidget *script = widgetForName(QStringLiteral("script.sh"));
    QVERIFY(script);
    QCOMPARE(script->nameTextColor(), executableColor);

    // The dark blue color gets lightened on a dark background.
    m_view->setPalette(paletteWithBase(QColor(20, 20, 20), Qt::white));
    QCoreApplication::processEvents();
    script = widgetForName(QStringLiteral("script.sh"));
    QVERIFY(script);
    QVERIFY(script->nameTextColor() != executableColor);
    QVERIFY(script->nameTextColor().lightness() > executableColor.lightness());
}

void FileNameHighlightingViewTest::testSizeHintsUseHighlightedFont_data()
{
    QTest::addColumn<int>("layout");
    QTest::newRow("compact") << int(KStandardItemListView::CompactLayout);
    QTest::newRow("details") << int(KStandardItemListView::DetailsLayout);
}

void FileNameHighlightingViewTest::testSizeHintsUseHighlightedFont()
{
    QFETCH(int, layout);
    m_view->setItemLayout(static_cast<KStandardItemListView::ItemLayout>(layout));

    int scriptIndex = -1;
    for (int i = 0; i < m_model->count(); ++i) {
        if (m_model->fileItem(i).name() == QLatin1String("script.sh")) {
            scriptIndex = i;
        }
    }
    QVERIFY(scriptIndex >= 0);

    std::unique_ptr<KItemListWidgetInformant> informant(DolphinFileItemListWidget::createInformant());
    auto width = [&]() {
        if (layout == KStandardItemListView::DetailsLayout) {
            return informant->preferredRoleColumnWidth("text", scriptIndex, m_view);
        }
        QVector<std::pair<qreal, bool>> hints(m_model->count(), {0.0, false});
        qreal logicalWidth = 0.0;
        informant->calculateItemSizeHints(hints, logicalWidth, m_view);
        return hints.at(scriptIndex).first;
    };

    m_view->setFileNameHighlighter(highlighter(false));
    const qreal normalWidth = width();
    m_view->setFileNameHighlighter(highlighter(true));
    QVERIFY(width() > normalWidth);
}

void FileNameHighlightingViewTest::testPermissionChange()
{
    m_view->setFileNameHighlighter(highlighter(true));

    TestWidget *plain = widgetForName(QStringLiteral("plain.txt"));
    QVERIFY(plain);
    QVERIFY(!plain->nameTextColor().isValid());

    const QString path = m_testDir->filePath(QStringLiteral("plain.txt"));
    auto modelKnowsExecuteBit = [&]() {
        return (m_model->fileItem(m_model->index(QUrl::fromLocalFile(path))).permissions() & S_IXUSR) != 0;
    };

    // Changed roles of the item refresh that follows the permission change.
    QSet<QByteArray> refreshedRoles;
    connect(m_model, &KFileItemModel::itemsChanged, this, [&](const KItemRangeList &, const QSet<QByteArray> &roles) {
        refreshedRoles = roles;
    });

    QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QTRY_VERIFY_WITH_TIMEOUT(modelKnowsExecuteBit(), 10000);

    // Only the permissions changed, so at most the icon name changed. The item size
    // must be recalculated anyway because the name is bold now.
    QVERIFY(m_view->itemSizeHintUpdateRequired(refreshedRoles));

    plain = widgetForName(QStringLiteral("plain.txt"));
    QVERIFY(plain);
    QCOMPARE(plain->nameTextColor(), executableColor);

    QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
    QTRY_VERIFY_WITH_TIMEOUT(!modelKnowsExecuteBit(), 10000);
    plain = widgetForName(QStringLiteral("plain.txt"));
    QVERIFY(plain);
    QVERIFY(!plain->nameTextColor().isValid());
}

void FileNameHighlightingViewTest::testReadSettings()
{
    FileNameHighlightingSettings disabled = FileNameHighlightingSettings::defaults();
    disabled.enabled = false;
    disabled.save();

    m_view->readSettings();
    TestWidget *script = widgetForName(QStringLiteral("script.sh"));
    QVERIFY(script);
    QVERIFY(!m_view->fileNameHighlighter()->isEnabled());
    QVERIFY(!script->nameTextColor().isValid());

    FileNameHighlightingSettings settings;
    settings.enabled = true;
    settings.rules = testRules();
    settings.save();

    m_view->readSettings();
    QVERIFY(m_view->fileNameHighlighter()->isEnabled());
    script = widgetForName(QStringLiteral("script.sh"));
    QVERIFY(script);
    QCOMPARE(script->nameTextColor(), executableColor);
}

QTEST_MAIN(FileNameHighlightingViewTest)

#include "filenamehighlightingviewtest.moc"
