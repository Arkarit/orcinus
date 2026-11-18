/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleailocalmodels.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>

#include <algorithm>

namespace
{
const QString LmStudioId = QStringLiteral("lmstudio");
const QString StrataId = QStringLiteral("strata");

/** @return The arguments of the process @p pid below @p procPath; empty if it is gone. */
QStringList processArguments(const QString &procPath, qint64 pid)
{
    QFile cmdline(QStringLiteral("%1/%2/cmdline").arg(procPath).arg(pid));
    if (pid <= 0 || !cmdline.open(QIODevice::ReadOnly)) {
        return {};
    }
    QStringList arguments;
    const QList<QByteArray> parts = cmdline.readAll().split('\0');
    for (const QByteArray &part : parts) {
        if (!part.isEmpty()) {
            arguments.append(QString::fromLocal8Bit(part));
        }
    }
    return arguments;
}

/** @return Whether @p arguments are those of a Strata server: python …/serve/server.py --engine strata … */
bool strataServerArguments(const QStringList &arguments)
{
    const qsizetype script = arguments.indexOf(QRegularExpression(QStringLiteral(".*(^|/)serve/server\\.py$")));
    const qsizetype engine = arguments.indexOf(QStringLiteral("--engine"));
    return script >= 0 && engine > script && arguments.value(engine + 1) == StrataId;
}

QJsonObject readJsonObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool isModelFile(const QString &fileName)
{
    return fileName.endsWith(QLatin1String(".gguf"), Qt::CaseInsensitive) && !fileName.startsWith(QLatin1String("mmproj"), Qt::CaseInsensitive);
}

void sortByName(QList<WhaleAiLocalModel> &models)
{
    std::sort(models.begin(), models.end(), [](const WhaleAiLocalModel &a, const WhaleAiLocalModel &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
}

/** @return @p body of a chunked HTTP reply joined; nothing if it is malformed. */
std::optional<QByteArray> unchunk(const QByteArray &body)
{
    QByteArray result;
    qsizetype position = 0;
    while (true) {
        const qsizetype lineEnd = body.indexOf("\r\n", position);
        if (lineEnd < 0) {
            return std::nullopt;
        }
        bool ok = false;
        const qsizetype size = body.mid(position, lineEnd - position).split(';').constFirst().trimmed().toLongLong(&ok, 16);
        if (!ok) {
            return std::nullopt;
        }
        if (size == 0) {
            return result;
        }
        position = lineEnd + 2;
        if (position + size > body.size()) {
            return std::nullopt;
        }
        result.append(body.mid(position, size));
        position += size + 2;
    }
}

QString findLms(const WhaleAiLocalModels::Options &options, const QString &home, const QString &lmStudioHome)
{
    const QStringList candidates{lmStudioHome + QStringLiteral("/bin/lms"), home + QStringLiteral("/.local/bin/lms")};
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }
    return QStandardPaths::findExecutable(QStringLiteral("lms"), options.searchPaths);
}

WhaleAiLocalProvider discoverLmStudio(const WhaleAiLocalModels::Options &options, const QString &home)
{
    WhaleAiLocalProvider provider;
    provider.id = LmStudioId;
    provider.name = QStringLiteral("LM Studio");

    const QString lmStudioHome = options.lmStudioHome.isEmpty() ? home + QStringLiteral("/.lmstudio") : options.lmStudioHome;
    const QString internal = lmStudioHome + QStringLiteral("/.internal");
    provider.cli = findLms(options, home, lmStudioHome);
    provider.installed = QFileInfo::exists(internal) || !provider.cli.isEmpty();
    if (!provider.installed) {
        return provider;
    }

    const QJsonObject server = readJsonObject(internal + QStringLiteral("/http-server-config.json"));
    provider.port = server.value(QStringLiteral("port")).toInt(provider.port);
    const QString address = server.value(QStringLiteral("networkInterface")).toString();
    // A server bound to every interface still answers on the loopback one.
    if (!address.isEmpty() && address != QLatin1String("0.0.0.0")) {
        provider.host = address;
    }
    provider.jitLoading = server.value(QStringLiteral("justInTimeModelLoading")).toBool();

    const QString downloads = readJsonObject(lmStudioHome + QStringLiteral("/settings.json")).value(QStringLiteral("downloadsFolder")).toString();
    provider.modelsFolder = downloads.isEmpty() ? lmStudioHome + QStringLiteral("/models") : downloads;

    provider.appRunning = WhaleAiLocalModels::lmStudioRunning(options.procPath);

    const std::optional<QByteArray> reply = options.probeServer
        ? WhaleAiLocalModels::httpGet(provider.host, provider.port, QStringLiteral("/api/v1/models"), options.probeTimeoutMs)
        : std::nullopt;
    if (reply) {
        provider.serverRunning = true;
        provider.models = WhaleAiLocalModels::modelsFromServer(*reply);
        provider.source = WhaleAiLocalProvider::Source::Server;
    }
    if (provider.models.isEmpty()) {
        QFile index(internal + QStringLiteral("/model-index-cache.json"));
        if (index.open(QIODevice::ReadOnly)) {
            provider.models = WhaleAiLocalModels::modelsFromIndex(index.readAll());
            provider.source = WhaleAiLocalProvider::Source::Index;
        }
    }
    if (provider.models.isEmpty()) {
        provider.models = WhaleAiLocalModels::modelsFromScan(provider.modelsFolder);
        provider.source = provider.models.isEmpty() ? WhaleAiLocalProvider::Source::None : WhaleAiLocalProvider::Source::Scan;
    }
    return provider;
}

WhaleAiLocalClient discoverOpenCode(const WhaleAiLocalModels::Options &options, const QString &home)
{
    WhaleAiLocalClient client;
    client.id = QStringLiteral("opencode");
    client.name = QStringLiteral("OpenCode");

    // In PATH the plain name is enough, like the presets of the settings page.
    if (!QStandardPaths::findExecutable(QStringLiteral("opencode"), options.searchPaths).isEmpty()) {
        client.commandLine = QStringLiteral("opencode acp");
        client.stable = true;
        return client;
    }
    // Where OpenCode's own install script puts it.
    const QFileInfo own(home + QStringLiteral("/.opencode/bin/opencode"));
    if (own.isFile() && own.isExecutable()) {
        client.commandLine = own.absoluteFilePath() + QStringLiteral(" acp");
        client.stable = true;
        return client;
    }

    const bool npx = !QStandardPaths::findExecutable(QStringLiteral("npx"), options.searchPaths).isEmpty();
    QString cached;
    QDirIterator it(home + QStringLiteral("/.npm/_npx"), QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext() && cached.isEmpty()) {
        const QFileInfo candidate(it.next() + QStringLiteral("/node_modules/.bin/opencode"));
        if (candidate.isExecutable()) {
            cached = candidate.absoluteFilePath();
        }
    }
    if (npx) {
        client.commandLine = QStringLiteral("npx -y opencode-ai acp");
        client.note = QStringLiteral("Not installed: npx downloads it on the first start (and when it is updated), which takes a while.");
    } else {
        client.note = QStringLiteral("Not installed. The user can install it with “npm i -g opencode-ai” or OpenCode's own install script (opencode.ai).");
    }
    if (!cached.isEmpty()) {
        client.note += QStringLiteral(" A copy in npx' cache (%1) is not meant to be run directly: the cache can be cleaned up at any time.").arg(cached);
    }
    return client;
}

WhaleAiLocalProvider discoverStrata(const WhaleAiLocalModels::Options &options, const QString &home)
{
    WhaleAiLocalProvider provider;
    provider.id = StrataId;
    provider.name = QStringLiteral("Strata");
    provider.port = 8080;
    provider.folder = options.strataFolder.isEmpty() ? home + QStringLiteral("/Strata") : options.strataFolder;
    if (!QFileInfo::exists(provider.folder + QStringLiteral("/serve/server.py"))) {
        return provider;
    }
    provider.installed = true;
    const QFileInfo python(provider.folder + QStringLiteral("/.venv/bin/python"));
    if (python.isExecutable()) {
        provider.cli = python.absoluteFilePath();
    }
    provider.modelsFolder = provider.folder;
    provider.models = WhaleAiLocalModels::modelsFromStrataConfigs(provider.folder);
    provider.source = provider.models.isEmpty() ? WhaleAiLocalProvider::Source::None : WhaleAiLocalProvider::Source::Configs;
    // All sizes are set up for one port (setup asks once); the first config that names one says which.
    for (const WhaleAiLocalModel &model : std::as_const(provider.models)) {
        const int port = readJsonObject(model.file).value(QStringLiteral("port")).toInt();
        if (port > 0) {
            provider.port = port;
            break;
        }
    }

    // While it loads, its port is still closed (AI13a): the process tells.
    if (const std::optional<QString> config = WhaleAiLocalModels::runningStrataConfig(options.procPath)) {
        provider.appRunning = true;
        provider.runningConfig = config->isEmpty() || QDir::isAbsolutePath(*config) ? *config : QDir(provider.folder).filePath(*config);
    }
    const std::optional<QByteArray> reply =
        options.probeServer ? WhaleAiLocalModels::httpGet(provider.host, provider.port, QStringLiteral("/health"), options.probeTimeoutMs) : std::nullopt;
    const QJsonObject health = reply ? QJsonDocument::fromJson(*reply).object() : QJsonObject();
    if (health.value(QStringLiteral("service")).toString() == StrataId) {
        provider.serverRunning = true;
        const QString running = health.value(QStringLiteral("model")).toString();
        for (WhaleAiLocalModel &model : provider.models) {
            model.loaded = model.id == running && health.value(QStringLiteral("loaded")).toBool();
        }
    }
    return provider;
}

QString sourceName(WhaleAiLocalProvider::Source source)
{
    switch (source) {
    case WhaleAiLocalProvider::Source::Server:
        return QStringLiteral("server");
    case WhaleAiLocalProvider::Source::Index:
        return QStringLiteral("index");
    case WhaleAiLocalProvider::Source::Scan:
        return QStringLiteral("files");
    case WhaleAiLocalProvider::Source::Configs:
        return QStringLiteral("configs");
    case WhaleAiLocalProvider::Source::None:
        break;
    }
    return QStringLiteral("none");
}
}

QString WhaleAiLocalProvider::apiBase() const
{
    return QStringLiteral("http://%1:%2/v1").arg(host).arg(port);
}

namespace WhaleAiLocalModels
{
WhaleAiLocalSetup discover(const Options &options)
{
    const QString home = options.homePath.isEmpty() ? QDir::homePath() : options.homePath;
    WhaleAiLocalSetup setup;
    const WhaleAiLocalProvider lmStudio = discoverLmStudio(options, home);
    if (lmStudio.installed) {
        setup.providers.append(lmStudio);
    }
    const WhaleAiLocalProvider strata = discoverStrata(options, home);
    if (strata.installed) {
        setup.providers.append(strata);
    }
    setup.clients.append(discoverOpenCode(options, home));
    return setup;
}

QJsonObject describeForAgent(const WhaleAiLocalSetup &setup)
{
    QJsonArray providers;
    for (const WhaleAiLocalProvider &provider : setup.providers) {
        QJsonArray models;
        for (const WhaleAiLocalModel &model : provider.models) {
            QJsonObject entry{{QStringLiteral("id"), model.id},
                              {QStringLiteral("name"), model.name},
                              {QStringLiteral("toolUse"), model.toolUse},
                              {QStringLiteral("vision"), model.vision}};
            if (!model.params.isEmpty()) {
                entry.insert(QStringLiteral("parameters"), model.params);
            }
            if (!model.quantization.isEmpty()) {
                entry.insert(QStringLiteral("quantization"), model.quantization);
            }
            if (model.sizeBytes > 0) {
                entry.insert(QStringLiteral("sizeGB"), qRound(model.sizeBytes / 1e8) / 10.0);
            }
            if (model.maxContextLength > 0) {
                entry.insert(QStringLiteral("maxContextLength"), model.maxContextLength);
            }
            if (provider.serverRunning) {
                entry.insert(QStringLiteral("loaded"), model.loaded);
            }
            models.append(entry);
        }
        QJsonObject entry{{QStringLiteral("id"), provider.id},
                          {QStringLiteral("name"), provider.name},
                          {QStringLiteral("apiBase"), provider.apiBase()},
                          {QStringLiteral("serverRunning"), provider.serverRunning},
                          {QStringLiteral("appRunning"), provider.appRunning},
                          {QStringLiteral("justInTimeLoading"), provider.jitLoading},
                          {QStringLiteral("modelsFolder"), provider.modelsFolder},
                          {QStringLiteral("modelsFrom"), sourceName(provider.source)},
                          {QStringLiteral("models"), models}};
        if (!provider.cli.isEmpty()) {
            entry.insert(QStringLiteral("cli"), provider.cli);
        }
        if (!provider.runningConfig.isEmpty()) {
            entry.insert(QStringLiteral("runningConfig"), provider.runningConfig);
        }
        QStringList notes;
        if (provider.id == StrataId) {
            notes.append(
                QStringLiteral("Strata serves one model at a time, each from a config of its own; loading takes 1-3 minutes "
                               "and 35-55 GB of RAM, the computer can be slow meanwhile."));
            if (provider.appRunning && !provider.serverRunning) {
                notes.append(QStringLiteral("Its server is starting (the port opens when the model is loaded)."));
            }
        } else if (!provider.serverRunning) {
            notes.append(
                QStringLiteral("The server is not running, so the agent cannot reach a model; do not start it yourself (any “lms” "
                               "call starts LM Studio, and “lms server stop” leaves the model in memory). The user starts it."));
        }
        if (provider.source == WhaleAiLocalProvider::Source::Scan) {
            notes.append(QStringLiteral("The model ids are guessed from the file names; the running server's list is authoritative."));
        }
        if (provider.id != StrataId) {
            notes.append(QStringLiteral("Prefer a model with toolUse: an agent works through tool calls."));
        }
        entry.insert(QStringLiteral("notes"), QJsonArray::fromStringList(notes));
        providers.append(entry);
    }

    QJsonArray clients;
    for (const WhaleAiLocalClient &client : setup.clients) {
        QJsonObject entry{{QStringLiteral("id"), client.id},
                          {QStringLiteral("name"), client.name},
                          {QStringLiteral("available"), !client.commandLine.isEmpty()},
                          {QStringLiteral("stable"), client.stable}};
        if (!client.commandLine.isEmpty()) {
            entry.insert(QStringLiteral("acpCommand"), client.commandLine);
        }
        if (!client.note.isEmpty()) {
            entry.insert(QStringLiteral("note"), client.note);
        }
        clients.append(entry);
    }

    QJsonObject result{{QStringLiteral("providers"), providers}, {QStringLiteral("clients"), clients}};
    if (providers.isEmpty()) {
        result.insert(QStringLiteral("note"), QStringLiteral("No local inference server found (Orcinus knows LM Studio and Strata)."));
    }
    return result;
}

QList<WhaleAiLocalModel> modelsFromIndex(const QByteArray &json)
{
    QList<WhaleAiLocalModel> models;
    const QJsonArray entries = QJsonDocument::fromJson(json).object().value(QStringLiteral("models")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("domain")).toString() != QLatin1String("llm")) {
            continue;
        }
        WhaleAiLocalModel model;
        model.id = entry.value(QStringLiteral("defaultIdentifier")).toString();
        if (model.id.isEmpty()) {
            continue;
        }
        model.name = entry.value(QStringLiteral("displayName")).toString(model.id);
        model.params = entry.value(QStringLiteral("params")).toString();
        model.quantization = entry.value(QStringLiteral("quant")).toObject().value(QStringLiteral("name")).toString();
        model.sizeBytes = entry.value(QStringLiteral("sizeBytes")).toInteger();
        model.maxContextLength = entry.value(QStringLiteral("contextLength")).toInt();
        model.toolUse = entry.value(QStringLiteral("trainedForToolUse")).toBool();
        model.vision = entry.value(QStringLiteral("visionAdapter")).isObject();
        models.append(model);
    }
    sortByName(models);
    return models;
}

QList<WhaleAiLocalModel> modelsFromServer(const QByteArray &json)
{
    QList<WhaleAiLocalModel> models;
    const QJsonArray entries = QJsonDocument::fromJson(json).object().value(QStringLiteral("models")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("type")).toString() != QLatin1String("llm")) {
            continue;
        }
        WhaleAiLocalModel model;
        model.id = entry.value(QStringLiteral("key")).toString();
        if (model.id.isEmpty()) {
            continue;
        }
        model.name = entry.value(QStringLiteral("display_name")).toString(model.id);
        model.params = entry.value(QStringLiteral("params_string")).toString();
        model.quantization = entry.value(QStringLiteral("quantization")).toObject().value(QStringLiteral("name")).toString();
        model.sizeBytes = entry.value(QStringLiteral("size_bytes")).toInteger();
        model.maxContextLength = entry.value(QStringLiteral("max_context_length")).toInt();
        const QJsonObject capabilities = entry.value(QStringLiteral("capabilities")).toObject();
        model.toolUse = capabilities.value(QStringLiteral("trained_for_tool_use")).toBool();
        model.vision = capabilities.value(QStringLiteral("vision")).toBool();
        model.loaded = !entry.value(QStringLiteral("loaded_instances")).toArray().isEmpty();
        models.append(model);
    }
    sortByName(models);
    return models;
}

QString idFromFile(const QString &relativePath, int modelFilesInFolder)
{
    const QStringList parts = relativePath.split(QLatin1Char('/'));
    if (modelFilesInFolder == 1 && parts.size() >= 2) {
        QString folder = parts.at(parts.size() - 2);
        if (folder.endsWith(QLatin1String("-GGUF"), Qt::CaseInsensitive)) {
            folder.chop(5);
        }
        return folder.toLower();
    }
    return relativePath.toLower();
}

QList<WhaleAiLocalModel> modelsFromStrataConfigs(const QString &folder)
{
    QList<WhaleAiLocalModel> models;
    static const QRegularExpression quant(QStringLiteral("^(i?q\\d|ud$|bf16|f16|mxfp)"), QRegularExpression::CaseInsensitiveOption);
    const QFileInfoList configs = QDir(folder).entryInfoList({QStringLiteral("strata-*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &config : configs) {
        const QString size = config.completeBaseName().mid(7); // after "strata-"
        if (size.isEmpty() || size.contains(QLatin1Char('.'))) {
            continue; // a copy, e.g. strata-iq3_s.before-memory-fix.json
        }
        const QJsonObject object = readJsonObject(config.absoluteFilePath());
        WhaleAiLocalModel model;
        model.id = object.value(QStringLiteral("model_name")).toString();
        if (model.id.isEmpty()) {
            continue;
        }
        model.file = config.absoluteFilePath();
        // "iq2_xs-uncensored" → "IQ2_XS Uncensored", "coder-iq1_m" → "Coder IQ1_M"
        QStringList words;
        for (const QString &part : size.split(QLatin1Char('-'), Qt::SkipEmptyParts)) {
            if (quant.match(part).hasMatch()) {
                words.append(part.toUpper());
                if (model.quantization.isEmpty()) {
                    model.quantization = part.toUpper();
                }
            } else {
                words.append(part.at(0).toUpper() + part.mid(1));
            }
        }
        model.name = QStringLiteral("Strata ") + words.join(QLatin1Char(' '));
        // Strata's model (Qwen3.8-Flash-Next) is made for agents and tool calls.
        model.toolUse = true;
        const QJsonArray arguments = object.value(QStringLiteral("args")).toArray();
        for (qsizetype i = 0; i < arguments.size(); ++i) {
            const QString argument = arguments.at(i).toString();
            if (argument == QLatin1String("--max-context")) {
                model.maxContextLength = arguments.at(i + 1).toString().toInt();
            } else if (argument == QLatin1String("--vision")) {
                model.vision = true;
            } else if (argument.endsWith(QLatin1String(".gguf"), Qt::CaseInsensitive)) {
                model.sizeBytes += QFileInfo(argument).size();
            }
        }
        const QJsonValue vision = object.value(QStringLiteral("vision"));
        if (vision.isObject() || vision.toBool()) {
            model.vision = true;
        }
        models.append(model);
    }
    return models;
}

std::optional<QString> runningStrataConfig(const QString &procPath)
{
    const QDir proc(procPath);
    for (const QString &pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool number = false;
        const qint64 value = pid.toLongLong(&number);
        if (!number) {
            continue;
        }
        const QStringList arguments = processArguments(procPath, value);
        if (strataServerArguments(arguments)) {
            const qsizetype config = arguments.indexOf(QStringLiteral("--config"));
            return config >= 0 ? arguments.value(config + 1) : QString();
        }
    }
    return std::nullopt;
}

bool isStrataServer(const QString &procPath, qint64 pid)
{
    return strataServerArguments(processArguments(procPath, pid));
}

QList<WhaleAiLocalModel> modelsFromScan(const QString &folder)
{
    // LM Studio's layout: <models folder>/<publisher>/<model folder>/<file>.gguf
    QList<WhaleAiLocalModel> models;
    const QDir root(folder);
    for (const QFileInfo &publisher : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        for (const QFileInfo &modelFolder : QDir(publisher.absoluteFilePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QFileInfoList files = QDir(modelFolder.absoluteFilePath()).entryInfoList({QStringLiteral("*.gguf")}, QDir::Files);
            QFileInfoList modelFiles;
            bool vision = false;
            for (const QFileInfo &file : files) {
                if (isModelFile(file.fileName())) {
                    modelFiles.append(file);
                } else {
                    vision = true;
                }
            }
            for (const QFileInfo &file : std::as_const(modelFiles)) {
                WhaleAiLocalModel model;
                const QString relative = root.relativeFilePath(file.absoluteFilePath());
                model.id = idFromFile(relative, modelFiles.size());
                model.name = file.completeBaseName();
                model.sizeBytes = file.size();
                model.vision = vision;
                models.append(model);
            }
        }
    }
    sortByName(models);
    return models;
}

bool lmStudioRunning(const QString &procPath)
{
    const QDir proc(procPath);
    for (const QString &pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool number = false;
        const qint64 value = pid.toLongLong(&number);
        if (number && isLmStudioProcess(procPath, value)) {
            return true;
        }
    }
    return false;
}

bool isLmStudioProcess(const QString &procPath, qint64 pid)
{
    QFile cmdline(QStringLiteral("%1/%2/cmdline").arg(procPath).arg(pid));
    if (pid <= 0 || !cmdline.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QString program = QFileInfo(QString::fromLocal8Bit(cmdline.readAll().split('\0').constFirst())).fileName();
    return program == QLatin1String("lm-studio") || program == QLatin1String("llmster")
        || (program.startsWith(QLatin1String("LM-Studio")) && program.endsWith(QLatin1String(".AppImage")));
}

std::optional<QByteArray> httpGet(const QString &host, int port, const QString &path, int timeoutMs)
{
    const std::optional<HttpReply> reply = httpRequest("GET", host, port, path, QByteArray(), timeoutMs);
    if (!reply || reply->status < 200 || reply->status > 299) {
        return std::nullopt;
    }
    return reply->body;
}

std::optional<HttpReply> httpRequest(const QByteArray &method, const QString &host, int port, const QString &path, const QByteArray &body, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    auto left = [&timer, timeoutMs]() {
        return std::max<qint64>(1, timeoutMs - timer.elapsed());
    };

    QTcpSocket socket;
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(left())) {
        return std::nullopt;
    }
    // HTTP/1.0: the server closes the connection after the reply, so its end is the reply's end.
    QByteArray request =
        method + ' ' + path.toLatin1() + " HTTP/1.0\r\nHost: " + host.toLatin1() + ':' + QByteArray::number(port) + "\r\nAccept: application/json\r\n";
    if (!body.isEmpty()) {
        request += "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
    }
    request += "\r\n" + body;
    socket.write(request);
    QByteArray reply;
    while (timer.elapsed() < timeoutMs) {
        if (!socket.waitForReadyRead(left())) {
            break;
        }
        reply.append(socket.readAll());
    }
    reply.append(socket.readAll());
    if (socket.state() != QAbstractSocket::UnconnectedState && timer.elapsed() >= timeoutMs) {
        return std::nullopt;
    }

    const qsizetype headerEnd = reply.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return std::nullopt;
    }
    const QList<QByteArray> headers = reply.left(headerEnd).split('\n');
    const QList<QByteArray> status = headers.constFirst().trimmed().split(' ');
    bool ok = false;
    HttpReply result;
    result.status = status.value(1).toInt(&ok);
    if (status.size() < 2 || !status.at(0).startsWith("HTTP/") || !ok) {
        return std::nullopt;
    }
    result.body = reply.mid(headerEnd + 4);
    for (const QByteArray &header : headers) {
        const QByteArray line = header.trimmed().toLower();
        if (line.startsWith("transfer-encoding:") && line.contains("chunked")) {
            const std::optional<QByteArray> joined = unchunk(result.body);
            if (!joined) {
                return std::nullopt;
            }
            result.body = *joined;
        }
    }
    return result;
}
}
