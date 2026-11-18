/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIFAKESTRATA_H
#define WHALEAIFAKESTRATA_H

#include "whaleailocalruntime.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>

#include <signal.h>

/**
 * @brief Strata for the tests (AI13c): a folder as Strata's setup leaves it, whose serve/server.py
 *        is a small Python server that behaves as Strata did in the AI13a spike.
 *
 * It prints the phases of a start, opens its port only when the model is "loaded", answers
 * /health, /unload (JSON only) and /load, ends on SIGTERM, and enters itself into a fake /proc
 * (removed when it ends). Its requests go to "requests.log". Two models: "small" (vision) and
 * "big"; a config's "fake_load_s" is how long loading takes, "fake_fail" makes it end instead.
 */
class FakeStrata
{
public:
    FakeStrata()
    {
        QDir().mkpath(procPath());
        m_port = freePort();
        const QString folder = this->folder();
        writeFile(folder + QStringLiteral("/serve/server.py"), serverScript());
        QDir().mkpath(folder + QStringLiteral("/.venv/bin"));
        QFile::link(QStandardPaths::findExecutable(QStringLiteral("python3")), folder + QStringLiteral("/.venv/bin/python"));
        setModel(QStringLiteral("small"), 0.3);
        setModel(QStringLiteral("big"), 0.3);
        // The servers Orcinus starts get the test's environment.
        qputenv("FAKE_STRATA_DIR", m_dir.path().toLocal8Bit());
    }

    ~FakeStrata()
    {
        // Whatever still runs (a test that failed half-way).
        const QDir proc(procPath());
        for (const QString &pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (QFile::exists(proc.filePath(pid + QStringLiteral("/fake-strata")))) {
                ::kill(pid_t(pid.toLongLong()), SIGKILL);
            }
        }
    }

    QString path() const
    {
        return m_dir.path();
    }
    QString home() const
    {
        return m_dir.filePath(QStringLiteral("home"));
    }
    QString folder() const
    {
        return home() + QStringLiteral("/Strata");
    }
    QString procPath() const
    {
        return m_dir.filePath(QStringLiteral("proc"));
    }
    int port() const
    {
        return m_port;
    }
    QString config(const QString &size) const
    {
        return folder() + QStringLiteral("/strata-") + size + QStringLiteral(".json");
    }

    /** (Re)writes the config of @p size: loading takes @p loadSeconds, or it ends with @p fail. */
    void setModel(const QString &size, double loadSeconds, bool fail = false)
    {
        writeFile(
            config(size),
            QStringLiteral(R"({"model_name": "%1", "port": %2, "fake_load_s": %3, "fake_fail": %4, "args": ["--max-context", "65536"%5]})")
                .arg(size)
                .arg(m_port)
                .arg(loadSeconds)
                .arg(fail ? QStringLiteral("true") : QStringLiteral("false"), size == QLatin1String("small") ? QStringLiteral(", \"--vision\"") : QString())
                .toUtf8());
    }

    WhaleAiLocalRuntime::Options options(int graceMs = 200) const
    {
        WhaleAiLocalRuntime::Options options;
        options.discovery.homePath = home(); // no LM Studio there
        options.discovery.searchPaths = {m_dir.filePath(QStringLiteral("nobin"))};
        options.discovery.procPath = procPath();
        options.discovery.probeTimeoutMs = 2000;
        options.statePath = m_dir.filePath(QStringLiteral("state/local-models.json"));
        options.pid = 4000000;
        options.graceMs = graceMs;
        options.commandTimeoutMs = 10000;
        options.requestTimeoutMs = 3000;
        return options;
    }

    /** @return The requests the fake server got, e.g. "POST /unload?wait=1 application/json". */
    QStringList requests() const
    {
        QFile log(m_dir.filePath(QStringLiteral("requests.log")));
        if (!log.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    /** @return The fake servers that run (pids). */
    QList<qint64> servers() const
    {
        QList<qint64> result;
        const QDir proc(procPath());
        for (const QString &pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (QFile::exists(proc.filePath(pid + QStringLiteral("/fake-strata")))) {
                result.append(pid.toLongLong());
            }
        }
        return result;
    }
    /** Starts a server of @p size as the user would (not through Orcinus); returns its pid. */
    qint64 startByUser(const QString &size)
    {
        qint64 pid = 0;
        QProcess process;
        process.setProgram(folder() + QStringLiteral("/.venv/bin/python"));
        process.setArguments({folder() + QStringLiteral("/serve/server.py"),
                              QStringLiteral("--engine"),
                              QStringLiteral("strata"),
                              QStringLiteral("--config"),
                              config(size),
                              QStringLiteral("--port"),
                              QString::number(m_port)});
        process.setWorkingDirectory(folder());
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
        process.setEnvironment(QProcess::systemEnvironment() << QStringLiteral("FAKE_STRATA_DIR=") + m_dir.path());
        process.startDetached(&pid);
        return pid;
    }

    static void writeFile(const QString &path, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(content);
        }
    }

private:
    static int freePort()
    {
        QTcpServer server;
        server.listen(QHostAddress::LocalHost);
        return server.serverPort();
    }

    static QByteArray serverScript()
    {
        return R"PY(
import http.server, json, os, signal, sys, threading, time
args = sys.argv[1:]
config_path = args[args.index("--config") + 1]
port = int(args[args.index("--port") + 1])
base = os.environ["FAKE_STRATA_DIR"]
config = json.load(open(config_path))
proc = os.path.join(base, "proc", str(os.getpid()))
os.makedirs(proc, exist_ok=True)
open(os.path.join(proc, "cmdline"), "wb").write(b"\0".join(a.encode() for a in [sys.executable, sys.argv[0]] + args) + b"\0")
open(os.path.join(proc, "fake-strata"), "w").close()
def leave(*_):
    import shutil
    shutil.rmtree(proc, ignore_errors=True)
    print("[strata] stopped", flush=True)
    os._exit(0)
signal.signal(signal.SIGTERM, leave)
state = {"loaded": False}
def load():
    print("[strata] starting the engine: reading the model's weights ...", flush=True)
    time.sleep(config["fake_load_s"] / 3)
    print("[strata] loading the experts into RAM (about 39 GB) and locking part of them for the GPU.", flush=True)
    time.sleep(config["fake_load_s"] / 3)
    if config.get("fake_fail"):
        print("[strata] the engine stopped unexpectedly (exit code 9)", flush=True)
        leave()
    print("[strata] filling the GPU's expert cache (14049 experts, 18.83 GiB of VRAM) ...", flush=True)
    time.sleep(config["fake_load_s"] / 3)
    print("[strata] almost ready ...", flush=True)
    state["loaded"] = True
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass
    def note(self):
        with open(os.path.join(base, "requests.log"), "a") as f:
            f.write(f"{self.command} {self.path} {self.headers.get('Content-Type', '')}\n")
    def reply(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def do_GET(self):
        self.note()
        if self.path == "/health":
            self.reply(200, {"status": "ok", "model": config["model_name"], "loaded": state["loaded"], "service": "strata"})
        else:
            self.reply(404, {})
    def do_POST(self):
        self.note()
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if not self.headers.get("Content-Type", "").startswith("application/json"):
            self.reply(415, {"error": {"message": "send application/json"}})
        elif self.path.startswith("/unload"):
            state["loaded"] = False
            self.reply(200, {"status": "unloaded"})
        elif self.path == "/load":
            load()
            self.reply(200, {"status": "loaded"})
        else:
            self.reply(404, {})
try:
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.server_close()
except OSError:
    print(f"server.py: error: port {port} is already in use", flush=True)
    leave()
load()
server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
print(f"ready: http://127.0.0.1:{port}/v1", flush=True)
server.serve_forever()
)PY";
    }

    QTemporaryDir m_dir;
    int m_port = 0;
};

#endif
