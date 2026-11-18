/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleailocalmodels.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>

namespace
{
/** Answers every connection with one fixed reply (or never), in its own thread: the client blocks. */
class FakeHttpServer : public QThread
{
public:
    explicit FakeHttpServer(const QByteArray &reply, bool silent = false)
        : m_reply(reply)
        , m_silent(silent)
    {
        start();
        m_ready.acquire();
    }
    ~FakeHttpServer() override
    {
        m_stop = true;
        wait();
    }
    quint16 port() const
    {
        return m_port;
    }
    QByteArray lastRequest() const
    {
        return m_request;
    }

protected:
    void run() override
    {
        QTcpServer server;
        server.listen(QHostAddress::LocalHost);
        m_port = server.serverPort();
        m_ready.release();
        while (!m_stop) {
            if (!server.waitForNewConnection(50)) {
                continue;
            }
            QTcpSocket *socket = server.nextPendingConnection();
            QByteArray request;
            while (!request.contains("\r\n\r\n") && socket->waitForReadyRead(1000)) {
                request.append(socket->readAll());
            }
            m_request = request;
            if (m_silent) {
                QElapsedTimer timer;
                timer.start();
                while (!m_stop && timer.elapsed() < 3000) {
                    socket->waitForReadyRead(50);
                }
            } else {
                socket->write(m_reply);
                socket->waitForBytesWritten(1000);
            }
            socket->disconnectFromHost();
            if (socket->state() != QAbstractSocket::UnconnectedState) {
                socket->waitForDisconnected(1000);
            }
            delete socket;
        }
    }

private:
    QByteArray m_reply;
    bool m_silent;
    QByteArray m_request;
    QSemaphore m_ready;
    std::atomic<quint16> m_port = 0;
    std::atomic<bool> m_stop = false;
};

QByteArray ok(const QByteArray &body)
{
    return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

void writeExecutable(const QString &path)
{
    write(path, "#!/bin/sh\n");
    QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeOwner);
}

/** @return A /proc/<pid>/cmdline: the arguments, each ended by a NUL. */
QByteArray cmdline(const QList<QByteArray> &arguments)
{
    QByteArray result;
    for (const QByteArray &argument : arguments) {
        result.append(argument).append('\0');
    }
    return result;
}

/** A port nothing listens on (as far as it can be known). */
quint16 closedPort()
{
    QTcpServer server;
    server.listen(QHostAddress::LocalHost);
    return server.serverPort();
}

const char IndexJson[] = R"({"models": [
  {"domain": "llm", "defaultIdentifier": "qwen3.8-27b", "displayName": "Qwen3.8 27B", "params": "27B",
   "quant": {"bits": 6, "name": "Q6_K"}, "sizeBytes": 23362145632, "contextLength": 262144,
   "trainedForToolUse": true, "visionAdapter": {"filename": "mmproj.gguf"}},
  {"domain": "embedding", "defaultIdentifier": "text-embedding-nomic", "displayName": "Nomic Embed"},
  {"domain": "llm", "defaultIdentifier": "psychagent-qwen3-32b-i1", "displayName": "PsychAgent Qwen3 32B",
   "contextLength": 40960, "trainedForToolUse": false, "visionAdapter": null},
  {"domain": "llm", "displayName": "no id, skipped"}
]})";

const char ServerJson[] = R"({"models": [
  {"type": "llm", "key": "qwen3.8-27b", "display_name": "Qwen3.8 27B", "params_string": "27B",
   "quantization": {"name": "Q6_K", "bits_per_weight": 6}, "size_bytes": 23362145632, "max_context_length": 262144,
   "capabilities": {"vision": true, "trained_for_tool_use": true},
   "loaded_instances": [{"id": "qwen3.8-27b", "config": {"context_length": 70144}}]},
  {"type": "embedding", "key": "text-embedding-nomic", "display_name": "Nomic Embed", "loaded_instances": []},
  {"type": "llm", "key": "gemma4-26b", "display_name": "Gemma4 26B", "capabilities": {"vision": false, "trained_for_tool_use": true},
   "loaded_instances": []}
]})";
}

class WhaleAiLocalModelsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void modelsFromIndex();
    void modelsFromServer();
    void idFromFile_data();
    void idFromFile();
    void modelsFromScan();
    void lmStudioRunning();
    void httpGet();
    void discover();
    void describeForAgent();
    void strataConfigs();
    void strataProcess();
    void discoverStrata();
};

void WhaleAiLocalModelsTest::modelsFromIndex()
{
    const QList<WhaleAiLocalModel> models = WhaleAiLocalModels::modelsFromIndex(IndexJson);
    // Language models only, by name; one without an id is left out.
    QCOMPARE(models.size(), 2);
    QCOMPARE(models.at(0).id, QStringLiteral("psychagent-qwen3-32b-i1"));
    QVERIFY(!models.at(0).toolUse);
    QVERIFY(!models.at(0).vision);
    QCOMPARE(models.at(0).maxContextLength, 40960);
    const WhaleAiLocalModel &qwen = models.at(1);
    QCOMPARE(qwen.id, QStringLiteral("qwen3.8-27b"));
    QCOMPARE(qwen.name, QStringLiteral("Qwen3.8 27B"));
    QCOMPARE(qwen.params, QStringLiteral("27B"));
    QCOMPARE(qwen.quantization, QStringLiteral("Q6_K"));
    QCOMPARE(qwen.sizeBytes, Q_INT64_C(23362145632));
    QCOMPARE(qwen.maxContextLength, 262144);
    QVERIFY(qwen.toolUse);
    QVERIFY(qwen.vision);
    QVERIFY(!qwen.loaded);

    QVERIFY(WhaleAiLocalModels::modelsFromIndex("not json").isEmpty());
    QVERIFY(WhaleAiLocalModels::modelsFromIndex(R"({"models": 3})").isEmpty());
}

void WhaleAiLocalModelsTest::modelsFromServer()
{
    const QList<WhaleAiLocalModel> models = WhaleAiLocalModels::modelsFromServer(ServerJson);
    QCOMPARE(models.size(), 2);
    QCOMPARE(models.at(0).id, QStringLiteral("gemma4-26b"));
    QVERIFY(!models.at(0).loaded);
    QVERIFY(!models.at(0).vision);
    const WhaleAiLocalModel &qwen = models.at(1);
    QCOMPARE(qwen.id, QStringLiteral("qwen3.8-27b"));
    QVERIFY(qwen.loaded);
    QVERIFY(qwen.vision);
    QVERIFY(qwen.toolUse);
    QCOMPARE(qwen.quantization, QStringLiteral("Q6_K"));
    QCOMPARE(qwen.maxContextLength, 262144);
    QCOMPARE(qwen.sizeBytes, Q_INT64_C(23362145632));
}

void WhaleAiLocalModelsTest::idFromFile_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<int>("files");
    QTest::addColumn<QString>("id");
    // The examples are what LM Studio 0.4.28 showed for these files.
    QTest::newRow("one file, -GGUF") << "lmstudio-community/Qwen3.8-27B-GGUF/Qwen3.8-27B-Q6_K.gguf" << 1 << "qwen3.8-27b";
    QTest::newRow("one file") << "HauhauCS/Gemma4-26B-A4B-Uncensored-HauhauCS-Balanced/Gemma4-x-Q8_K_P.gguf" << 1
                              << "gemma4-26b-a4b-uncensored-hauhaucs-balanced";
    QTest::newRow("two files") << "HauhauCS/Qwen3.8-27B-MTP-GGUF/Qwen3.8-27B-Q3_K_P.gguf" << 2 << "hauhaucs/qwen3.8-27b-mtp-gguf/qwen3.8-27b-q3_k_p.gguf";
}

void WhaleAiLocalModelsTest::idFromFile()
{
    QFETCH(QString, path);
    QFETCH(int, files);
    QFETCH(QString, id);
    QCOMPARE(WhaleAiLocalModels::idFromFile(path, files), id);
}

void WhaleAiLocalModelsTest::modelsFromScan()
{
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("lmstudio-community/Qwen3.8-27B-GGUF/Qwen3.8-27B-Q6_K.gguf")), "12345");
    write(dir.filePath(QStringLiteral("lmstudio-community/Qwen3.8-27B-GGUF/mmproj-Qwen3.8-27B-BF16.gguf")), "1");
    write(dir.filePath(QStringLiteral("HauhauCS/Two-GGUF/Two-Q3.gguf")), "1");
    write(dir.filePath(QStringLiteral("HauhauCS/Two-GGUF/Two-Q6.gguf")), "1");
    write(dir.filePath(QStringLiteral("HauhauCS/Two-GGUF/README.md")), "1");
    write(dir.filePath(QStringLiteral("loose.gguf")), "1");

    const QList<WhaleAiLocalModel> models = WhaleAiLocalModels::modelsFromScan(dir.path());
    QCOMPARE(models.size(), 3);
    QCOMPARE(models.at(0).name, QStringLiteral("Qwen3.8-27B-Q6_K"));
    QCOMPARE(models.at(0).id, QStringLiteral("qwen3.8-27b"));
    QCOMPARE(models.at(0).sizeBytes, 5);
    QVERIFY(models.at(0).vision); // an mmproj file next to it
    QCOMPARE(models.at(1).id, QStringLiteral("hauhaucs/two-gguf/two-q3.gguf"));
    QVERIFY(!models.at(1).vision);
    QCOMPARE(models.at(2).id, QStringLiteral("hauhaucs/two-gguf/two-q6.gguf"));

    QVERIFY(WhaleAiLocalModels::modelsFromScan(dir.filePath(QStringLiteral("missing"))).isEmpty());
}

void WhaleAiLocalModelsTest::lmStudioRunning()
{
    QTemporaryDir proc;
    write(proc.filePath(QStringLiteral("1/cmdline")), cmdline({"/bin/bash", "-c", "lm-studio"}));
    write(proc.filePath(QStringLiteral("self/cmdline")), cmdline({"lm-studio"}));
    QVERIFY(!WhaleAiLocalModels::lmStudioRunning(proc.path()));

    // The service lms starts; the AppImage around it; a headless llmster.
    for (const QByteArray &line : {cmdline({"/tmp/.mount_LM-StujIaiAk/lm-studio", "--run-as-service"}),
                                   cmdline({"/home/u/.local/opt/LM Studio/LM-Studio-0.4.28-2-linux-x64.AppImage", "--run-as-service"}),
                                   cmdline({"/home/u/.lmstudio/llmster/llmster"})}) {
        QTemporaryDir running;
        write(running.filePath(QStringLiteral("4711/cmdline")), line);
        QVERIFY2(WhaleAiLocalModels::lmStudioRunning(running.path()), line.constData());
    }
    QVERIFY(!WhaleAiLocalModels::lmStudioRunning(proc.filePath(QStringLiteral("missing"))));
}

void WhaleAiLocalModelsTest::httpGet()
{
    {
        FakeHttpServer server(ok(R"({"a":1})"));
        const std::optional<QByteArray> body = WhaleAiLocalModels::httpGet(QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("/api/v1/models"), 2000);
        QVERIFY(body);
        QCOMPARE(*body, QByteArray(R"({"a":1})"));
        QVERIFY(server.lastRequest().startsWith("GET /api/v1/models HTTP/1.0\r\n"));
    }
    {
        FakeHttpServer server("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\n{\"a\"\r\n3\r\n:1}\r\n0\r\n\r\n");
        const std::optional<QByteArray> body = WhaleAiLocalModels::httpGet(QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("/"), 2000);
        QVERIFY(body);
        QCOMPARE(*body, QByteArray(R"({"a":1})"));
    }
    {
        FakeHttpServer server("HTTP/1.1 404 Not Found\r\nContent-Length: 2\r\n\r\n{}");
        QVERIFY(!WhaleAiLocalModels::httpGet(QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("/"), 2000));
    }
    {
        FakeHttpServer server("garbage");
        QVERIFY(!WhaleAiLocalModels::httpGet(QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("/"), 2000));
    }
    // Nothing listens: at once. A server that never answers: after the timeout.
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!WhaleAiLocalModels::httpGet(QStringLiteral("127.0.0.1"), closedPort(), QStringLiteral("/"), 2000));
    QVERIFY(timer.elapsed() < 1000);
    FakeHttpServer silent(QByteArray(), true);
    timer.restart();
    QVERIFY(!WhaleAiLocalModels::httpGet(QStringLiteral("127.0.0.1"), silent.port(), QStringLiteral("/"), 300));
    QVERIFY(timer.elapsed() >= 290);
    QVERIFY(timer.elapsed() < 2000);
}

void WhaleAiLocalModelsTest::discover()
{
    QTemporaryDir dir;
    const QString home = dir.filePath(QStringLiteral("home"));
    const QString lmStudio = home + QStringLiteral("/.lmstudio");
    const QString bin = dir.filePath(QStringLiteral("bin"));
    const QString proc = dir.filePath(QStringLiteral("proc"));
    QDir().mkpath(bin);
    QDir().mkpath(proc);
    WhaleAiLocalModels::Options options;
    options.homePath = home;
    options.searchPaths = {bin};
    options.procPath = proc;
    options.probeTimeoutMs = 2000;

    // Nothing installed.
    WhaleAiLocalSetup setup = WhaleAiLocalModels::discover(options);
    QVERIFY(setup.providers.isEmpty());
    QCOMPARE(setup.clients.size(), 1);
    QCOMPARE(setup.clients.at(0).id, QStringLiteral("opencode"));
    QVERIFY(setup.clients.at(0).commandLine.isEmpty());
    QVERIFY(setup.clients.at(0).note.contains(QLatin1String("npm i -g opencode-ai")));

    // LM Studio with its index, server not running; models in a folder of its own.
    const quint16 closed = closedPort();
    write(lmStudio + QStringLiteral("/.internal/http-server-config.json"),
          QStringLiteral(R"({"port": %1, "networkInterface": "127.0.0.1", "justInTimeModelLoading": true})").arg(closed).toUtf8());
    write(lmStudio + QStringLiteral("/.internal/model-index-cache.json"), IndexJson);
    write(lmStudio + QStringLiteral("/settings.json"), R"({"downloadsFolder": "/mnt/models"})");
    writeExecutable(lmStudio + QStringLiteral("/bin/lms"));
    setup = WhaleAiLocalModels::discover(options);
    QCOMPARE(setup.providers.size(), 1);
    WhaleAiLocalProvider provider = setup.providers.at(0);
    QCOMPARE(provider.id, QStringLiteral("lmstudio"));
    QVERIFY(provider.installed);
    QCOMPARE(provider.cli, lmStudio + QStringLiteral("/bin/lms"));
    QCOMPARE(provider.port, int(closed));
    QCOMPARE(provider.apiBase(), QStringLiteral("http://127.0.0.1:%1/v1").arg(closed));
    QVERIFY(provider.jitLoading);
    QVERIFY(!provider.serverRunning);
    QVERIFY(!provider.appRunning);
    QCOMPARE(provider.modelsFolder, QStringLiteral("/mnt/models"));
    QCOMPARE(provider.source, WhaleAiLocalProvider::Source::Index);
    QCOMPARE(provider.models.size(), 2);

    // The server runs: its list wins, with the loaded state; the app process is seen.
    FakeHttpServer server(ok(ServerJson));
    write(lmStudio + QStringLiteral("/.internal/http-server-config.json"),
          QStringLiteral(R"({"port": %1, "networkInterface": "0.0.0.0"})").arg(server.port()).toUtf8());
    write(proc + QStringLiteral("/99/cmdline"), cmdline({"/tmp/.mount_x/lm-studio", "--run-as-service"}));
    setup = WhaleAiLocalModels::discover(options);
    provider = setup.providers.at(0);
    QCOMPARE(provider.host, QStringLiteral("127.0.0.1"));
    QVERIFY(provider.serverRunning);
    QVERIFY(provider.appRunning);
    QVERIFY(!provider.jitLoading);
    QCOMPARE(provider.source, WhaleAiLocalProvider::Source::Server);
    QCOMPARE(provider.models.size(), 2);
    QVERIFY(provider.models.at(1).loaded);

    // No index, server gone: the files in the default models folder.
    write(lmStudio + QStringLiteral("/.internal/http-server-config.json"), QStringLiteral(R"({"port": %1})").arg(closed).toUtf8());
    QFile::remove(lmStudio + QStringLiteral("/.internal/model-index-cache.json"));
    QFile::remove(lmStudio + QStringLiteral("/settings.json"));
    write(lmStudio + QStringLiteral("/models/pub/Model-GGUF/Model-Q4.gguf"), "1");
    setup = WhaleAiLocalModels::discover(options);
    provider = setup.providers.at(0);
    QCOMPARE(provider.modelsFolder, lmStudio + QStringLiteral("/models"));
    QCOMPARE(provider.source, WhaleAiLocalProvider::Source::Scan);
    QCOMPARE(provider.models.size(), 1);
    QCOMPARE(provider.models.at(0).id, QStringLiteral("model"));

    // OpenCode only in npx' cache: npx is offered, the cache named as unstable.
    writeExecutable(home + QStringLiteral("/.npm/_npx/e2094862b59aac7b/node_modules/.bin/opencode"));
    writeExecutable(bin + QStringLiteral("/npx"));
    setup = WhaleAiLocalModels::discover(options);
    WhaleAiLocalClient client = setup.clients.at(0);
    QCOMPARE(client.commandLine, QStringLiteral("npx -y opencode-ai acp"));
    QVERIFY(!client.stable);
    QVERIFY(client.note.contains(QLatin1String("e2094862b59aac7b")));
    QVERIFY(client.note.contains(QLatin1String("cleaned up")));

    // Installed by its own script, and in PATH.
    writeExecutable(home + QStringLiteral("/.opencode/bin/opencode"));
    client = WhaleAiLocalModels::discover(options).clients.at(0);
    QCOMPARE(client.commandLine, home + QStringLiteral("/.opencode/bin/opencode acp"));
    QVERIFY(client.stable);
    writeExecutable(bin + QStringLiteral("/opencode"));
    client = WhaleAiLocalModels::discover(options).clients.at(0);
    QCOMPARE(client.commandLine, QStringLiteral("opencode acp"));
    QVERIFY(client.stable);
    QVERIFY(client.note.isEmpty());
}

void WhaleAiLocalModelsTest::describeForAgent()
{
    WhaleAiLocalSetup setup;
    QJsonObject described = WhaleAiLocalModels::describeForAgent(setup);
    QVERIFY(described.value(QLatin1String("note")).toString().contains(QLatin1String("LM Studio")));

    WhaleAiLocalProvider provider;
    provider.id = QStringLiteral("lmstudio");
    provider.name = QStringLiteral("LM Studio");
    provider.installed = true;
    provider.source = WhaleAiLocalProvider::Source::Index;
    provider.models = WhaleAiLocalModels::modelsFromIndex(IndexJson);
    setup.providers = {provider};
    setup.clients = {{QStringLiteral("opencode"), QStringLiteral("OpenCode"), QStringLiteral("opencode acp"), true, QString()}};
    described = WhaleAiLocalModels::describeForAgent(setup);
    QVERIFY(!described.contains(QLatin1String("note")));
    const QJsonObject lmStudio = described.value(QLatin1String("providers")).toArray().at(0).toObject();
    QCOMPARE(lmStudio.value(QLatin1String("apiBase")).toString(), QStringLiteral("http://127.0.0.1:1234/v1"));
    QCOMPARE(lmStudio.value(QLatin1String("serverRunning")).toBool(), false);
    QCOMPARE(lmStudio.value(QLatin1String("modelsFrom")).toString(), QStringLiteral("index"));
    QVERIFY(QJsonDocument(lmStudio.value(QLatin1String("notes")).toArray()).toJson().contains("not running"));
    const QJsonObject qwen = lmStudio.value(QLatin1String("models")).toArray().at(1).toObject();
    QCOMPARE(qwen.value(QLatin1String("id")).toString(), QStringLiteral("qwen3.8-27b"));
    QCOMPARE(qwen.value(QLatin1String("sizeGB")).toDouble(), 23.4);
    QCOMPARE(qwen.value(QLatin1String("toolUse")).toBool(), true);
    QVERIFY(!qwen.contains(QLatin1String("loaded"))); // unknown while the server is not running
    const QJsonObject openCode = described.value(QLatin1String("clients")).toArray().at(0).toObject();
    QCOMPARE(openCode.value(QLatin1String("acpCommand")).toString(), QStringLiteral("opencode acp"));
    QCOMPARE(openCode.value(QLatin1String("stable")).toBool(), true);

    // Running: the loaded state is told, the "not running" note is gone.
    setup.providers[0].serverRunning = true;
    setup.providers[0].models[1].loaded = true;
    const QJsonObject running = WhaleAiLocalModels::describeForAgent(setup).value(QLatin1String("providers")).toArray().at(0).toObject();
    QVERIFY(running.value(QLatin1String("models")).toArray().at(1).toObject().value(QLatin1String("loaded")).toBool());
    QVERIFY(!QJsonDocument(running.value(QLatin1String("notes")).toArray()).toJson().contains("not running"));
}

namespace
{
/** A Strata folder as setup leaves it: serve/server.py, the venv's Python, configs, GGUF files. */
void makeStrata(const QString &folder, int port = 8080)
{
    write(folder + QStringLiteral("/serve/server.py"), "");
    writeExecutable(folder + QStringLiteral("/.venv/bin/python"));
    write(folder + QStringLiteral("/data/a.gguf"), "12345");
    write(folder + QStringLiteral("/data/b.gguf"), "123");
    write(folder + QStringLiteral("/strata-iq2_xs.json"),
          QStringLiteral(R"({"model_name": "qwen3.8-flash-next-iq2_xs", "port": %1, "vision": {"exe": "x"},
             "args": ["--native", "%2/data/a.gguf", "--ple-gguf", "%2/data/b.gguf", "--max-context", "262144", "--vision"]})")
              .arg(port)
              .arg(folder)
              .toUtf8());
    write(folder + QStringLiteral("/strata-iq2_xs-uncensored.json"),
          QStringLiteral(R"({"model_name": "qwen3.8-flash-next-iq2_xs-uncensored", "port": %1, "args": ["--max-context", "131072"]})").arg(port).toUtf8());
    write(folder + QStringLiteral("/strata-coder-iq1_m.json"), R"({"model_name": "coder", "args": []})");
    // Copies and other files are not models.
    write(folder + QStringLiteral("/strata-iq3_s.before-memory-fix.json"), R"({"model_name": "old"})");
    write(folder + QStringLiteral("/strata-iq3_s.json.bak"), R"({"model_name": "bak"})");
    write(folder + QStringLiteral("/strata-broken.json"), "{");
}
}

void WhaleAiLocalModelsTest::strataConfigs()
{
    QTemporaryDir dir;
    makeStrata(dir.path());
    const QList<WhaleAiLocalModel> models = WhaleAiLocalModels::modelsFromStrataConfigs(dir.path());
    QCOMPARE(models.size(), 3);
    // By file name.
    QCOMPARE(models.at(0).name, QStringLiteral("Strata Coder IQ1_M"));
    QCOMPARE(models.at(0).id, QStringLiteral("coder"));
    QCOMPARE(models.at(1).name, QStringLiteral("Strata IQ2_XS Uncensored"));
    QCOMPARE(models.at(1).maxContextLength, 131072);
    QVERIFY(!models.at(1).vision);
    const WhaleAiLocalModel &iq2 = models.at(2);
    QCOMPARE(iq2.id, QStringLiteral("qwen3.8-flash-next-iq2_xs"));
    QCOMPARE(iq2.name, QStringLiteral("Strata IQ2_XS"));
    QCOMPARE(iq2.quantization, QStringLiteral("IQ2_XS"));
    QCOMPARE(iq2.maxContextLength, 262144);
    QCOMPARE(iq2.sizeBytes, 8);
    QVERIFY(iq2.vision);
    QVERIFY(iq2.toolUse);
    QCOMPARE(iq2.file, dir.filePath(QStringLiteral("strata-iq2_xs.json")));
    QVERIFY(WhaleAiLocalModels::modelsFromStrataConfigs(dir.filePath(QStringLiteral("missing"))).isEmpty());
}

void WhaleAiLocalModelsTest::strataProcess()
{
    QTemporaryDir proc;
    write(proc.filePath(QStringLiteral("10/cmdline")), cmdline({"python3", "/home/u/other/server.py", "--engine", "strata"}));
    write(proc.filePath(QStringLiteral("11/cmdline")), cmdline({"/home/u/Strata/.venv/bin/python", "serve/server.py", "--engine", "llama"}));
    QVERIFY(!WhaleAiLocalModels::runningStrataConfig(proc.path()));
    QVERIFY(!WhaleAiLocalModels::isStrataServer(proc.path(), 10));
    QVERIFY(!WhaleAiLocalModels::isStrataServer(proc.path(), 11));

    // As setup's run scripts and Strata's MCP server start it.
    write(proc.filePath(QStringLiteral("12/cmdline")),
          cmdline({"/home/u/Strata/.venv/bin/python",
                   "/home/u/Strata/serve/server.py",
                   "--engine",
                   "strata",
                   "--config",
                   "/home/u/Strata/strata-iq2_xs.json",
                   "--port",
                   "8080"}));
    QCOMPARE(WhaleAiLocalModels::runningStrataConfig(proc.path()), std::optional<QString>(QStringLiteral("/home/u/Strata/strata-iq2_xs.json")));
    QVERIFY(WhaleAiLocalModels::isStrataServer(proc.path(), 12));
    QVERIFY(!WhaleAiLocalModels::isStrataServer(proc.path(), 13));
}

void WhaleAiLocalModelsTest::discoverStrata()
{
    QTemporaryDir dir;
    const QString home = dir.filePath(QStringLiteral("home"));
    const QString proc = dir.filePath(QStringLiteral("proc"));
    QDir().mkpath(proc);
    WhaleAiLocalModels::Options options;
    options.homePath = home;
    options.searchPaths = {dir.filePath(QStringLiteral("bin"))};
    options.procPath = proc;
    options.probeTimeoutMs = 2000;

    // Not there: no provider.
    QVERIFY(WhaleAiLocalModels::discover(options).providers.isEmpty());

    // There, not running; its port from the configs.
    const quint16 closed = closedPort();
    makeStrata(home + QStringLiteral("/Strata"), closed);
    WhaleAiLocalSetup setup = WhaleAiLocalModels::discover(options);
    QCOMPARE(setup.providers.size(), 1);
    WhaleAiLocalProvider strata = setup.providers.first();
    QCOMPARE(strata.id, QStringLiteral("strata"));
    QCOMPARE(strata.folder, home + QStringLiteral("/Strata"));
    QCOMPARE(strata.cli, home + QStringLiteral("/Strata/.venv/bin/python"));
    QCOMPARE(strata.port, int(closed));
    QCOMPARE(strata.source, WhaleAiLocalProvider::Source::Configs);
    QCOMPARE(strata.models.size(), 3);
    QVERIFY(!strata.serverRunning);
    QVERIFY(!strata.appRunning);
    QVERIFY(!strata.jitLoading);

    // Loading: the process is there, the port still closed.
    write(proc + QStringLiteral("/50/cmdline"),
          cmdline({"python", "serve/server.py", "--engine", "strata", "--config", "strata-iq2_xs.json", "--port", "8080"}));
    strata = WhaleAiLocalModels::discover(options).providers.first();
    QVERIFY(strata.appRunning);
    QVERIFY(!strata.serverRunning);
    QCOMPARE(strata.runningConfig, home + QStringLiteral("/Strata/strata-iq2_xs.json"));
    QVERIFY(QJsonDocument(WhaleAiLocalModels::describeForAgent(WhaleAiLocalModels::discover(options))).toJson().contains("is starting"));

    // Running and loaded: /health says which model; a server of another kind on the port is not Strata.
    FakeHttpServer server(ok(R"({"status": "ok", "max_context": 262144, "model": "qwen3.8-flash-next-iq2_xs", "loaded": true, "service": "strata"})"));
    makeStrata(home + QStringLiteral("/Strata"), server.port());
    strata = WhaleAiLocalModels::discover(options).providers.first();
    QVERIFY(strata.serverRunning);
    QCOMPARE(server.lastRequest().left(20), QByteArray("GET /health HTTP/1.0"));
    QVERIFY(strata.models.at(2).loaded);
    QVERIFY(!strata.models.at(1).loaded);
    FakeHttpServer other(ok(R"({"status": "ok"})"));
    makeStrata(home + QStringLiteral("/Strata"), other.port());
    QVERIFY(!WhaleAiLocalModels::discover(options).providers.first().serverRunning);

    // A folder of its own.
    options.strataFolder = dir.filePath(QStringLiteral("elsewhere"));
    QVERIFY(WhaleAiLocalModels::discover(options).providers.isEmpty());
    makeStrata(options.strataFolder);
    QCOMPARE(WhaleAiLocalModels::discover(options).providers.first().folder, options.strataFolder);
}

QTEST_GUILESS_MAIN(WhaleAiLocalModelsTest)

#include "whaleailocalmodelstest.moc"
