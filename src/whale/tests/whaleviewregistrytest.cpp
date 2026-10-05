/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewregistry.h"
#include "whaleitemlist.h"
#include "whaleview.h"
#include "whaleviewapiversion.h"
#include "whaleviewcontext.h"
#include "whaleviewplugin.h"

#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTest>
#include <QWidget>

namespace
{
const QString helloId = QStringLiteral("whale_hello");
const QString wrongApiId = QStringLiteral("whale_test_wrongapi");

class EmptyItemList : public WhaleItemList
{
public:
    explicit EmptyItemList(QObject *parent)
        : WhaleItemList(parent)
    {
    }
};

class TestContext : public WhaleViewContext
{
public:
    QUrl url() const override
    {
        return m_url;
    }

    WhaleItemList *items() const override
    {
        return m_items;
    }

    bool isLoading() const override
    {
        return false;
    }

    WhaleSelection selection() const override
    {
        return m_selection;
    }

    void setSelection(const WhaleSelection &selection) override
    {
        m_selection = selection;
    }

    void activateItems(const QList<QUrl> &urls) override
    {
        Q_UNUSED(urls)
    }

    void requestContextMenu(const QPoint &globalPos, const QUrl &url) override
    {
        Q_UNUSED(globalPos)
        Q_UNUSED(url)
    }

    void requestItemInfo(const QUrl &url) override
    {
        Q_UNUSED(url)
    }

    void setUrl(const QUrl &url)
    {
        m_url = url;
        Q_EMIT urlChanged(url);
    }

private:
    QUrl m_url = QUrl::fromLocalFile(QStringLiteral("/tmp"));
    EmptyItemList *const m_items = new EmptyItemList(this);
    WhaleSelection m_selection;
};

bool containsPlugin(const QList<KPluginMetaData> &plugins, const QString &pluginId)
{
    return std::any_of(plugins.cbegin(), plugins.cend(), [&pluginId](const KPluginMetaData &metaData) {
        return metaData.pluginId() == pluginId;
    });
}

void ignoreWrongApiWarning()
{
    QTest::ignoreMessage(
        QtWarningMsg,
        QRegularExpression(QStringLiteral("^Ignoring view plugin .*whale_test_wrongapi.* view API version %1, but this Whale provides version %2")
                               .arg(WHALE_VIEW_API_VERSION + 1)
                               .arg(WHALE_VIEW_API_VERSION)));
}
}

class WhaleViewRegistryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void testDiscovery();
    void testWrongApiVersionIsRejectedWithoutLoading();
    void testIsCompatible_data();
    void testIsCompatible();
    void testUnknownPlugin();
    void testInstantiation();
    void testCreateViewAndWidget();
};

void WhaleViewRegistryTest::initTestCase()
{
    // Only search the plugins built in this tree, never installed ones.
    QCoreApplication::setLibraryPaths({QStringLiteral(WHALE_TEST_PLUGIN_DIR), QStringLiteral(WHALE_TEST_EXTRA_PLUGIN_DIR)});
    qunsetenv("WHALE_TEST_WRONGAPI_LOADED");
}

void WhaleViewRegistryTest::testDiscovery()
{
    WhaleViewRegistry registry;
    ignoreWrongApiWarning();
    const QList<KPluginMetaData> plugins = registry.plugins();

    QVERIFY(containsPlugin(plugins, helloId));
    const KPluginMetaData hello = registry.pluginMetaData(helloId);
    QVERIFY(hello.isValid());
    QCOMPARE(hello.name(), QStringLiteral("Hello"));
    QCOMPARE(hello.rawData().value(QLatin1String(WHALE_VIEW_API_VERSION_KEY)).toInt(), WHALE_VIEW_API_VERSION);
    QVERIFY(hello.fileName().contains(QLatin1String("whale/views/")));
}

void WhaleViewRegistryTest::testWrongApiVersionIsRejectedWithoutLoading()
{
    // The test plugin is visible in the namespace…
    QVERIFY(containsPlugin(KPluginMetaData::findPlugins(WhaleViewRegistry::pluginNamespace()), wrongApiId));

    // …but the registry rejects it with a warning and does not offer it.
    WhaleViewRegistry registry;
    ignoreWrongApiWarning();
    QVERIFY(!containsPlugin(registry.plugins(), wrongApiId));
    QVERIFY(!registry.pluginMetaData(wrongApiId).isValid());

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^No compatible view plugin with the ID .*whale_test_wrongapi")));
    QCOMPARE(registry.plugin(wrongApiId), nullptr);

    // Its library has never been loaded.
    QVERIFY(!qEnvironmentVariableIsSet("WHALE_TEST_WRONGAPI_LOADED"));
}

void WhaleViewRegistryTest::testIsCompatible_data()
{
    QTest::addColumn<QJsonObject>("json");
    QTest::addColumn<bool>("compatible");
    QTest::addColumn<QString>("reason");

    const QJsonObject kplugin{{QStringLiteral("Id"), QStringLiteral("whale_test")}};
    const QString key = QStringLiteral(WHALE_VIEW_API_VERSION_KEY);

    QTest::newRow("matching version") << QJsonObject{{QStringLiteral("KPlugin"), kplugin}, {key, WHALE_VIEW_API_VERSION}} << true << QString();
    QTest::newRow("older version") << QJsonObject{{QStringLiteral("KPlugin"), kplugin}, {key, WHALE_VIEW_API_VERSION - 1}} << false
                                   << QStringLiteral("built for view API version %1").arg(WHALE_VIEW_API_VERSION - 1);
    QTest::newRow("newer version") << QJsonObject{{QStringLiteral("KPlugin"), kplugin}, {key, WHALE_VIEW_API_VERSION + 1}} << false
                                   << QStringLiteral("built for view API version %1").arg(WHALE_VIEW_API_VERSION + 1);
    QTest::newRow("missing version") << QJsonObject{{QStringLiteral("KPlugin"), kplugin}} << false << QStringLiteral("has no \"%1\" entry").arg(key);
    QTest::newRow("version as string") << QJsonObject{{QStringLiteral("KPlugin"), kplugin}, {key, QString::number(WHALE_VIEW_API_VERSION)}} << false
                                       << QStringLiteral("is not an integer");
    QTest::newRow("fractional version") << QJsonObject{{QStringLiteral("KPlugin"), kplugin}, {key, WHALE_VIEW_API_VERSION + 0.5}} << false
                                        << QStringLiteral("is not an integer");
}

void WhaleViewRegistryTest::testIsCompatible()
{
    QFETCH(QJsonObject, json);
    QFETCH(bool, compatible);
    QFETCH(QString, reason);

    const KPluginMetaData metaData(json, QStringLiteral("/plugins/whale/views/whale_test.so"));
    QString actualReason;
    QCOMPARE(WhaleViewRegistry::isCompatible(metaData, &actualReason), compatible);
    if (compatible) {
        QVERIFY(actualReason.isEmpty());
    } else {
        QVERIFY2(actualReason.contains(reason), qPrintable(actualReason));
    }
}

void WhaleViewRegistryTest::testUnknownPlugin()
{
    WhaleViewRegistry registry;
    ignoreWrongApiWarning();
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^No compatible view plugin with the ID .*does_not_exist")));
    QCOMPARE(registry.plugin(QStringLiteral("does_not_exist")), nullptr);
}

void WhaleViewRegistryTest::testInstantiation()
{
    WhaleViewRegistry registry;
    ignoreWrongApiWarning();
    WhaleViewPlugin *plugin = registry.plugin(helloId);
    QVERIFY(plugin);
    QCOMPARE(plugin->parent(), &registry);
    QVERIFY(plugin->supportsUrl(QUrl::fromLocalFile(QStringLiteral("/"))));

    // The instance is created once and reused.
    QCOMPARE(registry.plugin(helloId), plugin);
}

void WhaleViewRegistryTest::testCreateViewAndWidget()
{
    WhaleViewRegistry registry;
    ignoreWrongApiWarning();
    WhaleViewPlugin *plugin = registry.plugin(helloId);
    QVERIFY(plugin);

    TestContext context;
    QObject owner;
    WhaleView *view = plugin->createView(&context, &owner);
    QVERIFY(view);
    QCOMPARE(view->parent(), &owner);
    QCOMPARE(view->capabilities(), WhaleViewCapabilities());
    QVERIFY(view->saveState().isEmpty());

    QPointer<QWidget> widget = view->widget();
    QVERIFY(widget);
    QCOMPARE(view->widget(), widget.data()); // stable for the lifetime of the view
    QVERIFY(widget->property("text").toString().contains(QLatin1String("/tmp")));

    // The view reaches the host only through its context.
    const QUrl url = QUrl::fromLocalFile(QStringLiteral("/somewhere/else"));
    context.setUrl(url);
    QVERIFY(widget->property("text").toString().contains(QLatin1String("/somewhere/else")));

    view->activate();
    view->deactivate();

    // The view owns its widget.
    delete view;
    QVERIFY(!widget);
}

QTEST_MAIN(WhaleViewRegistryTest)

#include "whaleviewregistrytest.moc"
