/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletreewatch.h"

#include "whaleviewdebug.h"

#include <QSocketNotifier>

#include <sys/inotify.h>
#include <unistd.h>

namespace
{
// Changes of entry names, of content and of attributes (permissions decide what a scan
// can read), and of the folder itself.
constexpr uint32_t Mask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;
}

WhaleTreeWatch::Core::~Core()
{
    if (fd >= 0) {
        ::close(fd); // removes all watches
    }
}

bool WhaleTreeWatch::Core::add(quint64 owner, const QByteArray &path)
{
    QMutexLocker locker(&mutex);
    if (fd < 0 || closed || released.contains(owner)) {
        return false;
    }
    QList<int> &own = watches[owner];
    if (own.count() >= budget || owners.count() >= 2 * budget) {
        return false;
    }
    // The same folder for several owners has one descriptor.
    const int wd = ::inotify_add_watch(fd, path.constData(), Mask);
    if (wd < 0) {
        return false;
    }
    QSet<quint64> &folderOwners = owners[wd];
    if (!folderOwners.contains(owner)) {
        folderOwners.insert(owner);
        own.append(wd);
    }
    return true;
}

WhaleTreeWatch::WhaleTreeWatch(int budget, QObject *parent)
    : QObject(parent)
    , m_core(std::make_shared<Core>())
{
    m_core->budget = budget;
    m_core->fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_core->fd < 0) {
        qCWarning(WhaleViewDebug) << "Cannot watch scanned folders (inotify):" << strerror(errno) << "- tree scans are not cached";
        return;
    }
    m_notifier = new QSocketNotifier(m_core->fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &WhaleTreeWatch::readEvents);
}

WhaleTreeWatch::~WhaleTreeWatch()
{
    // Workers may still hold the core; from now on they watch nothing.
    QMutexLocker locker(&m_core->mutex);
    m_core->closed = true;
}

bool WhaleTreeWatch::isValid() const
{
    return m_core->fd >= 0;
}

int WhaleTreeWatch::budget() const
{
    return m_core->budget;
}

std::function<bool(const QByteArray &)> WhaleTreeWatch::adder(quint64 owner) const
{
    return [core = m_core, owner](const QByteArray &path) {
        return core->add(owner, path);
    };
}

void WhaleTreeWatch::release(quint64 owner)
{
    QMutexLocker locker(&m_core->mutex);
    m_core->released.insert(owner);
    const QList<int> watches = m_core->watches.take(owner);
    for (int wd : watches) {
        auto owners = m_core->owners.find(wd);
        if (owners == m_core->owners.end()) {
            continue;
        }
        owners->remove(owner);
        if (owners->isEmpty()) {
            m_core->owners.erase(owners);
            ::inotify_rm_watch(m_core->fd, wd);
        }
    }
}

int WhaleTreeWatch::watchCount() const
{
    QMutexLocker locker(&m_core->mutex);
    return m_core->owners.count();
}

int WhaleTreeWatch::watchCount(quint64 owner) const
{
    QMutexLocker locker(&m_core->mutex);
    return m_core->watches.value(owner).count();
}

void WhaleTreeWatch::processPendingEvents()
{
    if (isValid()) {
        readEvents();
    }
}

void WhaleTreeWatch::readEvents()
{
    alignas(inotify_event) char buffer[16 * 1024];
    QSet<quint64> changedOwners;
    bool overflow = false;
    for (;;) {
        const ssize_t length = ::read(m_core->fd, buffer, sizeof(buffer));
        if (length <= 0) {
            break;
        }
        QMutexLocker locker(&m_core->mutex);
        for (ssize_t offset = 0; offset < length;) {
            const auto event = reinterpret_cast<const inotify_event *>(buffer + offset);
            offset += sizeof(inotify_event) + event->len;
            if (event->mask & IN_Q_OVERFLOW) {
                overflow = true;
                continue;
            }
            const auto owners = m_core->owners.constFind(event->wd);
            if (owners == m_core->owners.constEnd()) {
                continue; // a released watch (IN_IGNORED after inotify_rm_watch)
            }
            changedOwners.unite(*owners);
            if (event->mask & IN_IGNORED) {
                // The folder is gone; the kernel removed the watch.
                m_core->owners.remove(event->wd);
            }
        }
    }
    if (overflow) {
        Q_EMIT overflowed();
    }
    for (quint64 owner : std::as_const(changedOwners)) {
        Q_EMIT changed(owner);
    }
}

#include "moc_whaletreewatch.cpp"
