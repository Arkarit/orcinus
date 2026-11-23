/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaitoolapi.h"

#include "views/syntheticexecutablebits.h"
#include "whaleaicanvasmedia.h"

#include <KConfigGroup>
#include <KCoreDirLister>
#include <KIO/CopyJob>
#include <KIO/FileUndoManager>
#include <KIO/JobUiDelegateFactory>
#include <KIO/ListJob>
#include <KIO/MkdirJob>
#include <KIO/StatJob>
#include <KIO/StoredTransferJob>
#include <KJobWidgets>

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaMethod>
#include <QMimeDatabase>
#include <QQmlEngine>
#include <QTimer>

namespace
{
constexpr int DefaultLimit = 10000;
constexpr qint64 DefaultText = 1024 * 1024;

QList<QUrl> urlsOf(const QStringList &texts, bool *ok)
{
    QList<QUrl> urls;
    *ok = !texts.isEmpty();
    for (const QString &text : texts) {
        const QUrl url = WhaleAiToolApi::urlOf(text);
        if (!url.isValid()) {
            *ok = false;
        }
        urls.append(url);
    }
    return urls;
}

/** Whether the MIME type of a file named @p name matches one of @p patterns (a type, or a group ending in a slash and an asterisk). */
bool matches(const QMimeDatabase &db, const QString &name, const QStringList &patterns)
{
    if (patterns.isEmpty()) {
        return true;
    }
    const QMimeType type = db.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
    for (const QString &pattern : patterns) {
        if (pattern.endsWith(QLatin1String("/*")) ? type.name().startsWith(pattern.chopped(1)) : type.inherits(pattern)) {
            return true;
        }
    }
    return false;
}
}

WhaleAiToolApi::WhaleAiToolApi(const WhaleAiTool &tool, const WhaleAiToolStore &store, const WhaleAiToolHost &host, QWidget *dialogParent, QObject *parent)
    : QObject(parent)
    , m_tool(tool)
    , m_store(store)
    , m_host(host)
    , m_dialogParent(dialogParent)
    , m_player(new WhaleAiCanvasPlayer(this))
    , m_backend(new WhaleAiToolBackend(tool, this))
{
    // Another folder: watch that one (if a tool listens).
    connect(this, &WhaleAiToolApi::contextChanged, this, &WhaleAiToolApi::updateWatch);
}

WhaleAiToolApi::~WhaleAiToolApi()
{
    for (const QPointer<KJob> &job : std::as_const(m_lists)) {
        if (job) {
            job->kill();
        }
    }
}

void WhaleAiToolApi::setEngine(QQmlEngine *engine)
{
    m_engine = engine;
}

QString WhaleAiToolApi::toolName() const
{
    return m_tool.name;
}

QString WhaleAiToolApi::toolFolder() const
{
    return QUrl::fromLocalFile(m_tool.folder).toString();
}

WhaleAiCanvasPlayer *WhaleAiToolApi::player() const
{
    return m_player;
}

WhaleAiToolBackend *WhaleAiToolApi::backend() const
{
    return m_backend;
}

QUrl WhaleAiToolApi::urlOf(const QString &text)
{
    if (text.startsWith(QLatin1Char('/'))) {
        return QUrl::fromLocalFile(QDir::cleanPath(text));
    }
    const QUrl url(text);
    if (!url.isValid() || url.scheme().isEmpty() || url.isRelative()) {
        return QUrl();
    }
    return url;
}

void WhaleAiToolApi::call(const QJSValue &callback, const QJSValueList &arguments)
{
    if (!callback.isCallable()) {
        return;
    }
    const QJSValue result = QJSValue(callback).call(arguments);
    if (result.isError()) {
        Q_EMIT logged(QStringLiteral("Error in a callback: %1 (line %2)").arg(result.toString(), result.property(QStringLiteral("lineNumber")).toString()));
    }
}

void WhaleAiToolApi::failLater(const QJSValue &callback, const QString &error, bool change)
{
    // Callbacks are always called later, never from inside the call that takes them.
    QTimer::singleShot(0, this, [this, callback, error, change] {
        call(callback, {change ? QJSValue(false) : QJSValue(QJSValue::NullValue), error});
    });
}

// ---- context ----

QString WhaleAiToolApi::currentFolder() const
{
    return m_host.currentFolder ? m_host.currentFolder().toString() : QString();
}

QStringList WhaleAiToolApi::selection() const
{
    QStringList result;
    if (m_host.selection) {
        const QList<QUrl> urls = m_host.selection();
        for (const QUrl &url : urls) {
            result.append(url.toString());
        }
    }
    return result;
}

QString WhaleAiToolApi::otherFolder() const
{
    return m_host.otherFolder ? m_host.otherFolder().toString() : QString();
}

// ---- reading ----

QJSValue WhaleAiToolApi::itemValue(const QUrl &url, const QString &name, bool isDir, qint64 size, qint64 modified, const QString &mime) const
{
    QJSValue item = m_engine->newObject();
    item.setProperty(QStringLiteral("url"), url.toString());
    item.setProperty(QStringLiteral("path"), url.isLocalFile() ? url.toLocalFile() : QString());
    item.setProperty(QStringLiteral("name"), name);
    const QUrl folder = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    item.setProperty(QStringLiteral("folder"), folder.toString());
    item.setProperty(QStringLiteral("folderName"), folder.fileName());
    item.setProperty(QStringLiteral("isDir"), isDir);
    item.setProperty(QStringLiteral("size"), double(size));
    item.setProperty(QStringLiteral("modified"), modified > 0 ? QDateTime::fromSecsSinceEpoch(modified).toString(Qt::ISODate) : QString());
    item.setProperty(QStringLiteral("mime"), mime);
    return item;
}

int WhaleAiToolApi::list(const QString &urlText, const QJSValue &options, const QJSValue &callback)
{
    const int id = m_nextId++;
    const QUrl url = urlOf(urlText);
    if (!url.isValid() || !m_engine) {
        failLater(callback, QStringLiteral("Not a folder: %1").arg(urlText));
        return id;
    }
    const bool recursive = options.property(QStringLiteral("recursive")).toBool();
    const bool hidden = options.property(QStringLiteral("hidden")).toBool();
    int limit = options.property(QStringLiteral("limit")).isNumber() ? options.property(QStringLiteral("limit")).toInt() : DefaultLimit;
    limit = std::clamp(limit, 1, MaxListed);
    QStringList mimes;
    const QJSValue mime = options.property(QStringLiteral("mime"));
    if (mime.isString()) {
        mimes.append(mime.toString());
    } else if (mime.isArray()) {
        const int length = mime.property(QStringLiteral("length")).toInt();
        for (int i = 0; i < length; ++i) {
            mimes.append(mime.property(i).toString());
        }
    }
    const QJSValue progress = options.property(QStringLiteral("progress"));

    const KIO::ListJob::ListFlags flags = hidden ? KIO::ListJob::ListFlags(KIO::ListJob::ListFlag::IncludeHidden) : KIO::ListJob::ListFlags();
    KIO::ListJob *job = recursive ? KIO::listRecursive(url, KIO::HideProgressInfo, flags) : KIO::listDir(url, KIO::HideProgressInfo, flags);
    m_lists.insert(id, job);
    auto items = std::make_shared<QJSValue>(m_engine->newArray());
    auto count = std::make_shared<int>(0);
    auto truncated = std::make_shared<bool>(false);
    connect(job, &KIO::ListJob::entries, this, [this, url, items, count, truncated, limit, mimes, progress, job](KIO::Job *, const KIO::UDSEntryList &entries) {
        const QMimeDatabase db;
        for (const KIO::UDSEntry &entry : entries) {
            const QString relative = entry.stringValue(KIO::UDSEntry::UDS_NAME);
            if (relative == QLatin1String(".") || relative == QLatin1String("..")) {
                continue;
            }
            const bool isDir = entry.isDir();
            const QString name = relative.section(QLatin1Char('/'), -1);
            if (!mimes.isEmpty() && (isDir || !matches(db, name, mimes))) {
                continue;
            }
            if (*count >= limit) {
                *truncated = true;
                job->kill(KJob::EmitResult);
                return;
            }
            QUrl itemUrl = url;
            itemUrl.setPath(QDir::cleanPath(url.path() + QLatin1Char('/') + relative));
            const QString mimeName = isDir ? QStringLiteral("inode/directory") : db.mimeTypeForFile(name, QMimeDatabase::MatchExtension).name();
            items->setProperty(*count,
                               itemValue(itemUrl,
                                         name,
                                         isDir,
                                         entry.numberValue(KIO::UDSEntry::UDS_SIZE, 0),
                                         entry.numberValue(KIO::UDSEntry::UDS_MODIFICATION_TIME, 0),
                                         mimeName));
            ++*count;
        }
        call(progress, {*count});
    });
    connect(job, &KJob::result, this, [this, id, items, truncated, callback](KJob *job) {
        m_lists.remove(id);
        // Stopped at the limit: what came so far is the answer.
        if (job->error() && !*truncated) {
            call(callback, {QJSValue(QJSValue::NullValue), job->errorString()});
            return;
        }
        call(callback, {*items, QJSValue(QJSValue::NullValue)});
    });
    return id;
}

void WhaleAiToolApi::cancel(int id)
{
    if (QPointer<KJob> job = m_lists.take(id); job) {
        // No callback for a cancelled list.
        job->kill(KJob::Quietly);
    }
}

void WhaleAiToolApi::item(const QString &urlText, const QJSValue &callback)
{
    const QUrl url = urlOf(urlText);
    if (!url.isValid() || !m_engine) {
        failLater(callback, QStringLiteral("Not a file: %1").arg(urlText));
        return;
    }
    auto job = KIO::stat(url, KIO::StatJob::SourceSide, KIO::StatDefaultDetails, KIO::HideProgressInfo);
    connect(job, &KJob::result, this, [this, url, callback](KJob *job) {
        if (job->error()) {
            call(callback, {QJSValue(QJSValue::NullValue), job->errorString()});
            return;
        }
        const KIO::UDSEntry entry = static_cast<KIO::StatJob *>(job)->statResult();
        const bool isDir = entry.isDir();
        const QString mime = isDir ? QStringLiteral("inode/directory") : QMimeDatabase().mimeTypeForFile(url.fileName(), QMimeDatabase::MatchExtension).name();
        call(callback,
             {itemValue(url,
                        url.fileName(),
                        isDir,
                        entry.numberValue(KIO::UDSEntry::UDS_SIZE, 0),
                        entry.numberValue(KIO::UDSEntry::UDS_MODIFICATION_TIME, 0),
                        mime),
              QJSValue(QJSValue::NullValue)});
    });
}

void WhaleAiToolApi::readText(const QString &urlText, const QJSValue &callback, qint64 maxBytes)
{
    const QUrl url = urlOf(urlText);
    if (!url.isValid()) {
        failLater(callback, QStringLiteral("Not a file: %1").arg(urlText));
        return;
    }
    const qint64 limit = maxBytes > 0 ? std::min(maxBytes, MaxText) : DefaultText;
    auto job = KIO::storedGet(url, KIO::NoReload, KIO::HideProgressInfo);
    auto tooLarge = std::make_shared<bool>(false);
    connect(job, &KJob::totalSize, this, [job, limit, tooLarge](KJob *, qulonglong size) {
        if (qint64(size) > limit) {
            *tooLarge = true;
            job->kill(KJob::EmitResult);
        }
    });
    connect(job, &KJob::result, this, [this, callback, limit, tooLarge](KJob *job) {
        auto stored = static_cast<KIO::StoredTransferJob *>(job);
        if (job->error() && !*tooLarge) {
            call(callback, {QJSValue(QJSValue::NullValue), job->errorString()});
            return;
        }
        if (*tooLarge || stored->data().size() > limit) {
            call(callback, {QJSValue(QJSValue::NullValue), QStringLiteral("The file is larger than %1 bytes.").arg(limit)});
            return;
        }
        call(callback, {QString::fromUtf8(stored->data()), QJSValue(QJSValue::NullValue)});
    });
}

// ---- changing ----

void WhaleAiToolApi::finish(KJob *job, const QJSValue &callback)
{
    // Conflicts (a file of that name exists) are asked as Orcinus asks them.
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingDisabled, m_dialogParent));
    if (m_dialogParent) {
        KJobWidgets::setWindow(job, m_dialogParent->window());
    }
    connect(job, &KJob::result, this, [this, callback](KJob *job) {
        call(callback, {job->error() == 0, job->error() ? QJSValue(job->errorString()) : QJSValue(QJSValue::NullValue)});
    });
}

void WhaleAiToolApi::copy(const QStringList &urls, const QString &destination, const QJSValue &callback)
{
    bool ok = false;
    const QList<QUrl> sources = urlsOf(urls, &ok);
    const QUrl target = urlOf(destination);
    if (!ok || !target.isValid()) {
        failLater(callback, QStringLiteral("copy needs files and a destination folder."), true);
        return;
    }
    KIO::CopyJob *job = KIO::copy(sources, target, KIO::HideProgressInfo);
    SyntheticExecutableBits::cleanUpAfter(job);
    KIO::FileUndoManager::self()->recordCopyJob(job);
    finish(job, callback);
}

void WhaleAiToolApi::move(const QStringList &urls, const QString &destination, const QJSValue &callback)
{
    bool ok = false;
    const QList<QUrl> sources = urlsOf(urls, &ok);
    const QUrl target = urlOf(destination);
    if (!ok || !target.isValid()) {
        failLater(callback, QStringLiteral("move needs files and a destination folder."), true);
        return;
    }
    KIO::CopyJob *job = KIO::move(sources, target, KIO::HideProgressInfo);
    SyntheticExecutableBits::cleanUpAfter(job);
    KIO::FileUndoManager::self()->recordCopyJob(job);
    finish(job, callback);
}

void WhaleAiToolApi::rename(const QString &urlText, const QString &newName, const QJSValue &callback)
{
    const QUrl url = urlOf(urlText);
    if (!url.isValid() || newName.isEmpty() || newName.contains(QLatin1Char('/')) || newName == QLatin1String(".") || newName == QLatin1String("..")) {
        failLater(callback, QStringLiteral("rename needs a file and a new name without '/'."), true);
        return;
    }
    QUrl target = url.adjusted(QUrl::RemoveFilename);
    target.setPath(target.path() + newName);
    KIO::CopyJob *job = KIO::moveAs(url, target, KIO::HideProgressInfo);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Rename, {url}, target, job);
    finish(job, callback);
}

void WhaleAiToolApi::trash(const QStringList &urls, const QJSValue &callback)
{
    bool ok = false;
    const QList<QUrl> sources = urlsOf(urls, &ok);
    if (!ok) {
        failLater(callback, QStringLiteral("trash needs files."), true);
        return;
    }
    KIO::Job *job = KIO::trash(sources, KIO::HideProgressInfo);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, sources, QUrl(QStringLiteral("trash:/")), job);
    finish(job, callback);
}

void WhaleAiToolApi::mkdir(const QString &urlText, const QJSValue &callback)
{
    const QUrl url = urlOf(urlText);
    if (!url.isValid()) {
        failLater(callback, QStringLiteral("mkdir needs a folder."), true);
        return;
    }
    KIO::MkdirJob *job = KIO::mkdir(url);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkdir, {}, url, job);
    finish(job, callback);
}

void WhaleAiToolApi::writeText(const QString &urlText, const QString &text, bool overwrite, const QJSValue &callback)
{
    const QUrl url = urlOf(urlText);
    if (!url.isValid()) {
        failLater(callback, QStringLiteral("writeText needs a file."), true);
        return;
    }
    auto put = [this, url, text, callback] {
        KIO::StoredTransferJob *job = KIO::storedPut(text.toUtf8(), url, -1, KIO::HideProgressInfo);
        KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Put, {}, url, job);
        finish(job, callback);
    };
    auto stat = KIO::stat(url, KIO::StatJob::DestinationSide, KIO::StatNoDetails, KIO::HideProgressInfo);
    connect(stat, &KJob::result, this, [this, url, overwrite, put, callback](KJob *stat) {
        if (stat->error() == KIO::ERR_DOES_NOT_EXIST) {
            put();
            return;
        }
        if (stat->error()) {
            call(callback, {false, stat->errorString()});
            return;
        }
        if (!overwrite) {
            call(callback, {false, QStringLiteral("%1 exists; pass overwrite to replace it (the old one goes to the trash).").arg(url.toDisplayString())});
            return;
        }
        // The old one stays in the trash, and undo gets it back.
        KIO::Job *trashJob = KIO::trash({url}, KIO::HideProgressInfo);
        KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, {url}, QUrl(QStringLiteral("trash:/")), trashJob);
        connect(trashJob, &KJob::result, this, [this, put, callback](KJob *trashJob) {
            if (trashJob->error()) {
                call(callback, {false, trashJob->errorString()});
                return;
            }
            put();
        });
    });
}

// ---- changes in the current folder ----

namespace
{
enum ChangeKind {
    Added,
    Changed,
    Removed
};
}

void WhaleAiToolApi::connectNotify(const QMetaMethod &signal)
{
    QObject::connectNotify(signal);
    if (signal == QMetaMethod::fromSignal(&WhaleAiToolApi::folderContentsChanged)) {
        // Not while connecting.
        QMetaObject::invokeMethod(this, &WhaleAiToolApi::updateWatch, Qt::QueuedConnection);
    }
}

void WhaleAiToolApi::disconnectNotify(const QMetaMethod &signal)
{
    QObject::disconnectNotify(signal);
    if (signal == QMetaMethod::fromSignal(&WhaleAiToolApi::folderContentsChanged)) {
        QMetaObject::invokeMethod(this, &WhaleAiToolApi::updateWatch, Qt::QueuedConnection);
    }
}

void WhaleAiToolApi::updateWatch()
{
    const QUrl folder = m_host.currentFolder ? m_host.currentFolder() : QUrl();
    if (!isSignalConnected(QMetaMethod::fromSignal(&WhaleAiToolApi::folderContentsChanged)) || !folder.isValid()) {
        if (m_watcher) {
            m_watcher->stop();
        }
        m_watched.clear();
        m_changes.clear();
        return;
    }
    if (folder.matches(m_watched, QUrl::StripTrailingSlash)) {
        return;
    }
    if (!m_watcher) {
        // Shares KIO's directory cache and watching with the views.
        m_watcher = new KCoreDirLister(this);
        m_watcher->setAutoErrorHandlingEnabled(false);
        m_changeTimer = new QTimer(this);
        m_changeTimer->setSingleShot(true);
        m_changeTimer->setInterval(100);
        connect(m_changeTimer, &QTimer::timeout, this, &WhaleAiToolApi::reportChanges);
        connect(m_watcher, &KCoreDirLister::completed, this, [this] {
            m_watchReady = true;
        });
        connect(m_watcher, &KCoreDirLister::itemsAdded, this, [this](const QUrl &, const KFileItemList &items) {
            for (const KFileItem &item : items) {
                addChange(item, Added);
            }
        });
        connect(m_watcher, &KCoreDirLister::itemsDeleted, this, [this](const KFileItemList &items) {
            for (const KFileItem &item : items) {
                addChange(item, Removed);
            }
        });
        connect(m_watcher, &KCoreDirLister::refreshItems, this, [this](const QList<QPair<KFileItem, KFileItem>> &items) {
            for (const auto &[before, after] : items) {
                if (before.url() == after.url()) {
                    addChange(after, Changed);
                } else {
                    // Renamed: gone under its old name, there under the new one.
                    addChange(before, Removed);
                    addChange(after, Added);
                }
            }
        });
    }
    m_watched = folder;
    m_watchReady = false;
    m_changes.clear();
    m_watcher->openUrl(folder);
}

void WhaleAiToolApi::addChange(const KFileItem &item, int kind)
{
    // The first listing of the folder is no change.
    if (!m_watchReady || item.isHidden()) {
        return;
    }
    const auto it = m_changes.find(item.url());
    if (it == m_changes.end()) {
        m_changes.insert(item.url(), {kind, item});
    } else if (kind == Removed) {
        if (it->kind == Added) {
            m_changes.erase(it); // came and went
        } else {
            *it = {Removed, item};
        }
    } else if (kind == Added && it->kind == Removed) {
        *it = {Changed, item}; // replaced
    } else {
        it->item = item; // still added or changed, as it is now
    }
    m_changeTimer->start();
}

void WhaleAiToolApi::reportChanges()
{
    if (m_changes.isEmpty() || !m_engine) {
        return;
    }
    QJSValue lists[3] = {m_engine->newArray(), m_engine->newArray(), m_engine->newArray()};
    int counts[3] = {0, 0, 0};
    for (const Change &change : std::as_const(m_changes)) {
        lists[change.kind].setProperty(counts[change.kind]++, itemValue(change.item));
    }
    m_changes.clear();
    QJSValue changes = m_engine->newObject();
    changes.setProperty(QStringLiteral("folder"), m_watched.toString());
    changes.setProperty(QStringLiteral("added"), lists[Added]);
    changes.setProperty(QStringLiteral("removed"), lists[Removed]);
    changes.setProperty(QStringLiteral("changed"), lists[Changed]);
    Q_EMIT folderContentsChanged(changes);
}

QJSValue WhaleAiToolApi::itemValue(const KFileItem &item) const
{
    // As list() gives them: the type by the name.
    const QString mime = item.isDir() ? QStringLiteral("inode/directory") : QMimeDatabase().mimeTypeForFile(item.name(), QMimeDatabase::MatchExtension).name();
    const QDateTime modified = item.time(KFileItem::ModificationTime);
    return itemValue(item.url(), item.name(), item.isDir(), item.size(), modified.isValid() ? modified.toSecsSinceEpoch() : 0, mime);
}

// ---- the view ----

void WhaleAiToolApi::reveal(const QString &urlText)
{
    const QUrl url = urlOf(urlText);
    if (url.isValid() && m_host.reveal) {
        m_host.reveal(url);
    }
}

// ---- user interface ----

void WhaleAiToolApi::pickFolder(const QJSValue &callback, const QString &start)
{
    auto dialog = new QFileDialog(m_dialogParent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setFileMode(QFileDialog::Directory);
    dialog->setOption(QFileDialog::ShowDirsOnly);
    dialog->setWindowTitle(m_tool.name);
    const QUrl startUrl = urlOf(start);
    if (startUrl.isValid()) {
        dialog->setDirectoryUrl(startUrl);
    }
    connect(dialog, &QFileDialog::finished, this, [this, dialog, callback](int result) {
        const QList<QUrl> urls = dialog->selectedUrls();
        call(callback, {result == QDialog::Accepted && !urls.isEmpty() ? urls.constFirst().toString() : QString()});
    });
    dialog->open();
}

void WhaleAiToolApi::pickFiles(const QJSValue &callback, const QJSValue &options)
{
    auto dialog = new QFileDialog(m_dialogParent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const QJSValue multiple = options.property(QStringLiteral("multiple"));
    dialog->setFileMode(multiple.isBool() && !multiple.toBool() ? QFileDialog::ExistingFile : QFileDialog::ExistingFiles);
    dialog->setWindowTitle(m_tool.name);
    const QString mime = options.property(QStringLiteral("mime")).toString();
    if (!mime.isEmpty() && options.property(QStringLiteral("mime")).isString()) {
        dialog->setMimeTypeFilters({mime});
    }
    connect(dialog, &QFileDialog::finished, this, [this, dialog, callback](int result) {
        QStringList urls;
        if (result == QDialog::Accepted) {
            for (const QUrl &url : dialog->selectedUrls()) {
                urls.append(url.toString());
            }
        }
        call(callback, {m_engine ? m_engine->toScriptValue(urls) : QJSValue()});
    });
    dialog->open();
}

void WhaleAiToolApi::notify(const QString &text)
{
    Q_EMIT notified(text.left(500));
}

// ---- settings ----

QVariant WhaleAiToolApi::setting(const QString &key, const QVariant &fallback) const
{
    const KConfigGroup group = m_store.settingsGroup(m_tool.id);
    if (group.hasKey(key)) {
        // Kept as JSON, so numbers, lists and objects come back as they were.
        const QJsonDocument document = QJsonDocument::fromJson("[" + group.readEntry(key, QByteArray()) + "]");
        if (document.isArray() && !document.array().isEmpty()) {
            return document.array().first().toVariant();
        }
    }
    if (m_tool.settings.contains(key)) {
        return m_tool.settings.value(key).toVariant();
    }
    return fallback;
}

void WhaleAiToolApi::setSetting(const QString &key, const QVariant &value)
{
    KConfigGroup group = m_store.settingsGroup(m_tool.id);
    const QJsonValue json = QJsonValue::fromVariant(value);
    const QByteArray text = QJsonDocument(QJsonArray{json}).toJson(QJsonDocument::Compact);
    // "[...]" around it: the value alone.
    group.writeEntry(key, text.mid(1, text.size() - 2));
    group.sync();
}

#include "moc_whaleaitoolapi.cpp"
