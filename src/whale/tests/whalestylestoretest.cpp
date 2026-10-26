/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalestylestore.h"
#include "whalestyleelement.h"
#include "whalestyleelements.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace
{
class FakeElement : public WhaleStyleElement
{
public:
    explicit FakeElement(const QString &key)
        : m_key(key)
    {
    }
    QString key() const override
    {
        return m_key;
    }
    QString title() const override
    {
        return m_key;
    }
    QString capture(const DolphinMainWindow *) const override
    {
        return QString();
    }
    void apply(DolphinMainWindow *, const QString &) const override
    {
    }
    QString describe(const QString &value) const override
    {
        return value;
    }

private:
    QString m_key;
};
}

/**
 * The model, registry and file of styles (ST1).
 */
class WhaleStyleStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void testIdForName_data();
    void testIdForName();
    void testEmptyFile();
    void testRoundTrip();
    void testUnknownElementSurvives();
    void testUnknownEntrySurvives();
    void testOrder();
    void testBrokenFile();
    void testNewerVersionKept();
    void testAddRenameRemoveMove();
    void testSaveRemovesDeletedStyle();
    void testReplaceStyles();
    void testRegistry();

private:
    KSharedConfigPtr freshConfig(const QByteArray &contents = QByteArray());

    QTemporaryDir m_tmpDir;
    QString m_path;
    int m_count = 0;
};

void WhaleStyleStoreTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_tmpDir.isValid());
}

void WhaleStyleStoreTest::init()
{
    m_path = m_tmpDir.filePath(QStringLiteral("orcinusstylesrc-%1").arg(++m_count));
}

KSharedConfigPtr WhaleStyleStoreTest::freshConfig(const QByteArray &contents)
{
    if (!contents.isNull()) {
        QFile file(m_path);
        if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
            qFatal("cannot write %s", qPrintable(m_path));
        }
    }
    return KSharedConfig::openConfig(m_path, KConfig::SimpleConfig);
}

void WhaleStyleStoreTest::testIdForName_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QStringList>("taken");
    QTest::addColumn<QString>("id");

    QTest::newRow("plain") << QStringLiteral("Schlicht") << QStringList() << QStringLiteral("schlicht");
    QTest::newRow("words") << QStringLiteral("  Big  Icons, 2! ") << QStringList() << QStringLiteral("big-icons-2");
    QTest::newRow("accents") << QStringLiteral("Größe à la carte") << QStringList() << QStringLiteral("gro-e-a-la-carte");
    QTest::newRow("nothing usable") << QStringLiteral("…!") << QStringList() << QStringLiteral("style");
    QTest::newRow("taken") << QStringLiteral("Bilder") << QStringList{QStringLiteral("bilder"), QStringLiteral("bilder-2")} << QStringLiteral("bilder-3");
}

void WhaleStyleStoreTest::testIdForName()
{
    QFETCH(QString, name);
    QFETCH(QStringList, taken);
    QFETCH(QString, id);
    QCOMPARE(WhaleStyleStore::idForName(name, taken), id);
}

void WhaleStyleStoreTest::testEmptyFile()
{
    WhaleStyleStore store(freshConfig());
    QVERIFY(store.styles().isEmpty());
    QCOMPARE(store.fileVersion(), WhaleStyleStore::currentVersion());
}

void WhaleStyleStoreTest::testRoundTrip()
{
    WhaleStyle schlicht;
    schlicht.name = QStringLiteral("Schlicht");
    schlicht.shortcut = QKeySequence(QStringLiteral("Ctrl+Alt+1"));
    schlicht.elements.insert(QStringLiteral("statusBar"), {true, QStringLiteral("FullWidth")});
    schlicht.elements.insert(QStringLiteral("viewStyle.left"), {false, QStringLiteral("a,b=c;[x]\nnext line")});
    WhaleStyle bilder;
    bilder.name = QStringLiteral("Bilder");

    {
        WhaleStyleStore store(freshConfig());
        QCOMPARE(store.addStyle(schlicht), QStringLiteral("schlicht"));
        QCOMPARE(store.addStyle(bilder), QStringLiteral("bilder"));
        store.save();
    }

    WhaleStyleStore store(freshConfig());
    QCOMPARE(store.ids(), (QStringList{QStringLiteral("schlicht"), QStringLiteral("bilder")}));
    schlicht.id = QStringLiteral("schlicht");
    bilder.id = QStringLiteral("bilder");
    QCOMPARE(*store.style(QStringLiteral("schlicht")), schlicht);
    QCOMPARE(*store.style(QStringLiteral("bilder")), bilder);
    QVERIFY(store.style(QStringLiteral("bilder"))->shortcut.isEmpty());

    const KConfigGroup general = freshConfig()->group(QStringLiteral("General"));
    QCOMPARE(general.readEntry("Version", 0), WhaleStyleStore::currentVersion());
}

void WhaleStyleStoreTest::testUnknownElementSurvives()
{
    WhaleStyleStore store(
        freshConfig("[General]\nVersion=1\nOrder=a\n\n"
                    "[Style][a]\nName=A\n\n"
                    "[Style][a][Element][fromTheFuture]\nEnabled=true\nValue=42\n"));
    QVERIFY(!WhaleStyleElements::self().element(QStringLiteral("fromTheFuture")));
    QCOMPARE(store.style(QStringLiteral("a"))->elements.value(QStringLiteral("fromTheFuture")), (WhaleStyleElementState{true, QStringLiteral("42")}));

    QVERIFY(store.renameStyle(QStringLiteral("a"), QStringLiteral("A2")));
    store.save();

    WhaleStyleStore reread(freshConfig());
    QCOMPARE(reread.style(QStringLiteral("a"))->name, QStringLiteral("A2"));
    QCOMPARE(reread.style(QStringLiteral("a"))->elements.value(QStringLiteral("fromTheFuture")), (WhaleStyleElementState{true, QStringLiteral("42")}));
}

void WhaleStyleStoreTest::testUnknownEntrySurvives()
{
    WhaleStyleStore store(freshConfig("[Style][a]\nName=A\nIcon=view-preview\n"));
    store.save();

    WhaleStyleStore reread(freshConfig());
    QCOMPARE(reread.style(QStringLiteral("a"))->unknownEntries, (QMap<QString, QString>{{QStringLiteral("Icon"), QStringLiteral("view-preview")}}));
    QCOMPARE(freshConfig()->group(QStringLiteral("Style")).group(QStringLiteral("a")).readEntry("Icon"), QStringLiteral("view-preview"));
}

void WhaleStyleStoreTest::testOrder()
{
    // "gone" is in the order but has no group; "b" and "a" have groups but are not in it.
    WhaleStyleStore store(
        freshConfig("[General]\nOrder=c,gone,c,d\n\n"
                    "[Style][a]\nName=A\n\n[Style][b]\nName=B\n\n[Style][c]\nName=C\n\n[Style][d]\nName=D\n"));
    QCOMPARE(store.ids(), (QStringList{QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("a"), QStringLiteral("b")}));

    store.save();
    QCOMPARE(freshConfig()->group(QStringLiteral("General")).readEntry("Order", QStringList()), store.ids());
}

void WhaleStyleStoreTest::testBrokenFile()
{
    WhaleStyleStore store(
        freshConfig("garbage without group\n"
                    "[General]\nVersion=not a number\n\n"
                    "[Style][noname]\nShortcut=no such key+++\n\n"
                    "[Style][noname][Element][split]\nValue=x\n\n"
                    "[Style][broken\nName=Half\n"));
    QCOMPARE(store.fileVersion(), WhaleStyleStore::currentVersion());
    const WhaleStyle *style = store.style(QStringLiteral("noname"));
    QVERIFY(style);
    // Without a name the ID stands in; an element without "Enabled" is not enabled.
    QCOMPARE(style->name, QStringLiteral("noname"));
    QCOMPARE(style->elements.value(QStringLiteral("split")), (WhaleStyleElementState{false, QStringLiteral("x")}));
}

void WhaleStyleStoreTest::testNewerVersionKept()
{
    const int newer = WhaleStyleStore::currentVersion() + 1;
    WhaleStyleStore store(freshConfig(QByteArray("[General]\nVersion=") + QByteArray::number(newer) + "\n\n[Style][a]\nName=A\n"));
    QCOMPARE(store.fileVersion(), newer);
    QCOMPARE(store.ids(), QStringList{QStringLiteral("a")});

    store.save();
    QCOMPARE(freshConfig()->group(QStringLiteral("General")).readEntry("Version", 0), newer);
}

void WhaleStyleStoreTest::testAddRenameRemoveMove()
{
    WhaleStyleStore store(freshConfig());

    WhaleStyle style;
    style.id = QStringLiteral("ignored");
    style.name = QStringLiteral("   ");
    QVERIFY(store.addStyle(style).isEmpty());

    style.name = QStringLiteral(" One ");
    QCOMPARE(store.addStyle(style), QStringLiteral("one"));
    QCOMPARE(store.style(QStringLiteral("one"))->name, QStringLiteral("One"));
    QCOMPARE(store.addStyle(style), QStringLiteral("one-2"));
    style.name = QStringLiteral("Two");
    QCOMPARE(store.addStyle(style), QStringLiteral("two"));

    // Renaming keeps the ID, so a later style of the old name gets a new one.
    QVERIFY(store.renameStyle(QStringLiteral("one"), QStringLiteral("First")));
    QVERIFY(!store.renameStyle(QStringLiteral("one"), QStringLiteral(" ")));
    QVERIFY(!store.renameStyle(QStringLiteral("nope"), QStringLiteral("X")));
    QCOMPARE(store.style(QStringLiteral("one"))->name, QStringLiteral("First"));

    WhaleStyle changed = *store.style(QStringLiteral("two"));
    changed.shortcut = QKeySequence(QStringLiteral("Meta+S"));
    QVERIFY(store.updateStyle(changed));
    changed.id = QStringLiteral("nope");
    QVERIFY(!store.updateStyle(changed));
    QCOMPARE(store.style(QStringLiteral("two"))->shortcut, QKeySequence(QStringLiteral("Meta+S")));

    QVERIFY(store.moveStyle(QStringLiteral("two"), 0));
    QCOMPARE(store.ids(), (QStringList{QStringLiteral("two"), QStringLiteral("one"), QStringLiteral("one-2")}));
    QVERIFY(store.moveStyle(QStringLiteral("two"), 99));
    QCOMPARE(store.ids(), (QStringList{QStringLiteral("one"), QStringLiteral("one-2"), QStringLiteral("two")}));
    QVERIFY(!store.moveStyle(QStringLiteral("nope"), 0));

    QVERIFY(store.removeStyle(QStringLiteral("one-2")));
    QVERIFY(!store.removeStyle(QStringLiteral("one-2")));
    QCOMPARE(store.ids(), (QStringList{QStringLiteral("one"), QStringLiteral("two")}));

    // Nothing is written before save().
    QVERIFY(WhaleStyleStore(freshConfig()).styles().isEmpty());
}

void WhaleStyleStoreTest::testSaveRemovesDeletedStyle()
{
    WhaleStyleStore store(
        freshConfig("[General]\nOrder=a,b\n\n[Style][a]\nName=A\n\n[Style][a][Element][split]\nEnabled=true\nValue=on\n\n"
                    "[Style][b]\nName=B\n"));
    QVERIFY(store.removeStyle(QStringLiteral("a")));
    store.save();

    KSharedConfigPtr config = freshConfig();
    QCOMPARE(config->group(QStringLiteral("Style")).groupList(), QStringList{QStringLiteral("b")});
    QVERIFY(!config->group(QStringLiteral("Style")).group(QStringLiteral("a")).group(QStringLiteral("Element")).exists());
    QCOMPARE(WhaleStyleStore(config).ids(), QStringList{QStringLiteral("b")});
}

void WhaleStyleStoreTest::testReplaceStyles()
{
    WhaleStyleStore store(freshConfig("[General]\nOrder=a\n\n[Style][a]\nName=A\n"));
    WhaleStyle b;
    b.id = QStringLiteral("b");
    b.name = QStringLiteral(" B ");
    WhaleStyle c;
    c.id = QStringLiteral("c");
    c.name = QStringLiteral("C");

    QVERIFY(store.replaceStyles({c, b}));
    QCOMPARE(store.ids(), (QStringList{QStringLiteral("c"), QStringLiteral("b")}));
    QCOMPARE(store.style(QStringLiteral("b"))->name, QStringLiteral("B"));

    // Refused as a whole: a second "b", an empty ID, an empty name.
    WhaleStyle noId = c;
    noId.id.clear();
    WhaleStyle noName = c;
    noName.id = QStringLiteral("d");
    noName.name = QStringLiteral(" ");
    QVERIFY(!store.replaceStyles({b, b}));
    QVERIFY(!store.replaceStyles({noId}));
    QVERIFY(!store.replaceStyles({noName}));
    QCOMPARE(store.ids(), (QStringList{QStringLiteral("c"), QStringLiteral("b")}));

    QVERIFY(store.replaceStyles({}));
    store.save();
    QVERIFY(WhaleStyleStore(freshConfig()).styles().isEmpty());
}

void WhaleStyleStoreTest::testRegistry()
{
    WhaleStyleElements registry;
    registry.add(std::make_unique<FakeElement>(QStringLiteral("split")));
    registry.add(std::make_unique<FakeElement>(QStringLiteral("statusBar")));
    registry.add(std::make_unique<FakeElement>(QStringLiteral("split")));
    registry.add(nullptr);

    const QList<const WhaleStyleElement *> elements = registry.elements();
    QCOMPARE(elements.size(), 2);
    QCOMPARE(elements.at(0)->key(), QStringLiteral("split"));
    QCOMPARE(elements.at(1)->key(), QStringLiteral("statusBar"));
    QCOMPARE(registry.element(QStringLiteral("statusBar")), elements.at(1));
    QVERIFY(!registry.element(QStringLiteral("nope")));
    QVERIFY(elements.at(0)->isAvailable(nullptr));
}

QTEST_GUILESS_MAIN(WhaleStyleStoreTest)

#include "whalestylestoretest.moc"
