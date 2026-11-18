/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAILOCALMODELS_H
#define WHALEAILOCALMODELS_H

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/**
 * @brief A model a local inference server (LM Studio) can serve.
 */
struct WhaleAiLocalModel {
    QString id; ///< what the API and the server's load call take, e.g. "qwen3.8-27b"
    QString name; ///< shown to the user, e.g. "Qwen3.8 27B"
    QString params; ///< e.g. "27B", "35B-A3B"; empty if unknown
    QString quantization; ///< e.g. "Q6_K"; empty if unknown
    qint64 sizeBytes = 0;
    int maxContextLength = 0; ///< 0 if unknown
    bool toolUse = false; ///< trained for tool calls (an agent needs them)
    bool vision = false; ///< takes images
    bool loaded = false; ///< loaded right now (only known while the server runs)

    bool operator==(const WhaleAiLocalModel &other) const = default;
};

/**
 * @brief A local inference server found on this computer. AI12: LM Studio only.
 */
struct WhaleAiLocalProvider {
    /** Where the list of models came from; the ids of a scan are guesses from the file names. */
    enum class Source {
        None,
        Server, ///< the running server's /api/v1/models
        Index, ///< LM Studio's own model index
        Scan, ///< the *.gguf files in the models folder
    };

    QString id; ///< "lmstudio"
    QString name; ///< "LM Studio"
    bool installed = false;
    QString cli; ///< the lms command line tool; empty if not found
    QString host = QStringLiteral("127.0.0.1");
    int port = 1234;
    bool jitLoading = false; ///< the server loads a requested model by itself
    bool serverRunning = false; ///< the server answers on host:port
    bool appRunning = false; ///< an LM Studio process runs (with or without the server)
    QString modelsFolder;
    Source source = Source::None;
    QList<WhaleAiLocalModel> models; ///< language models only (no embedding models)

    /** @return The OpenAI-compatible base URL, e.g. "http://127.0.0.1:1234/v1". */
    QString apiBase() const;
};

/**
 * @brief A program that talks ACP to Orcinus and to a local model. AI12: OpenCode.
 */
struct WhaleAiLocalClient {
    QString id; ///< "opencode"
    QString name; ///< "OpenCode"
    QString commandLine; ///< what to run for ACP; empty if it is not available
    /** False if the program was found only where it can disappear (npx' cache). */
    bool stable = false;
    QString note; ///< English, for the agent: how to get it, why it is not stable, …
};

/**
 * @brief What there is for a local AI on this computer.
 */
struct WhaleAiLocalSetup {
    QList<WhaleAiLocalProvider> providers;
    QList<WhaleAiLocalClient> clients;
};

/**
 * @brief Finds local inference servers, their models and the clients for them (AI12b).
 *
 * Reading only: it never starts anything. LM Studio's `lms` is not called, since any call
 * of it starts LM Studio's service. See .privatedoc/ai-support-plan.md (AI12).
 */
namespace WhaleAiLocalModels
{
struct Options {
    QString homePath; ///< empty: the user's home
    QString lmStudioHome; ///< empty: <home>/.lmstudio
    QStringList searchPaths; ///< for programs; empty: PATH
    QString procPath = QStringLiteral("/proc"); ///< where to look for running processes
    int probeTimeoutMs = 1500; ///< for the HTTP request to a server that accepted the connection
};

/** @return What there is; asks a running server over HTTP (blocking, see Options::probeTimeoutMs). */
WhaleAiLocalSetup discover(const Options &options = {});

/** @return @p setup for the agent (list_local_models). */
QJsonObject describeForAgent(const WhaleAiLocalSetup &setup);

// ---- building blocks, public for the tests ----

/** @return The language models in LM Studio's model index (.internal/model-index-cache.json). */
QList<WhaleAiLocalModel> modelsFromIndex(const QByteArray &json);
/** @return The language models in a reply of LM Studio's /api/v1/models, with their loaded state. */
QList<WhaleAiLocalModel> modelsFromServer(const QByteArray &json);
/** @return The language models found as *.gguf files below @p folder; ids guessed from the names. */
QList<WhaleAiLocalModel> modelsFromScan(const QString &folder);
/**
 * @return The id LM Studio gives the model file at @p relativePath ("publisher/folder/file.gguf")
 *         in the models folder, guessed: the folder name without "-GGUF" if it holds one model
 *         file ("qwen3.8-27b"), else the whole path, both lower case.
 */
QString idFromFile(const QString &relativePath, int modelFilesInFolder);
/** @return Whether an LM Studio process (app, its service or llmster) runs, found below @p procPath. */
bool lmStudioRunning(const QString &procPath);
/** @return Whether the process @p pid (below @p procPath) is LM Studio's app, its service or llmster. */
bool isLmStudioProcess(const QString &procPath, qint64 pid);

/** A reply to httpRequest(). */
struct HttpReply {
    int status = 0;
    QByteArray body;
};
/**
 * @return The reply to "@p method @p path" (with @p body as JSON if not empty) at @p host:@p port;
 *         nothing if nothing listens, the reply is not HTTP, or after @p timeoutMs. Blocks.
 */
std::optional<HttpReply> httpRequest(const QByteArray &method, const QString &host, int port, const QString &path, const QByteArray &body, int timeoutMs);
/**
 * @return The body of a successful (2xx) reply to "GET @p path" at @p host:@p port; nothing if
 *         nothing listens, on any other answer, or after @p timeoutMs. Blocks; plain HTTP only.
 */
std::optional<QByteArray> httpGet(const QString &host, int port, const QString &path, int timeoutMs);
}

#endif
