/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewstate.h"

#include "whaleviewdebug.h"

#include <QDataStream>
#include <QIODevice>

#include <algorithm>

namespace
{
// Eight bytes, so that Dolphin's own state (a QDataStream starting with its version
// number) cannot be mistaken for them.
const QByteArray locationMagic = QByteArrayLiteral("WHALEVS\x01");
const QByteArray sessionMagic = QByteArrayLiteral("WHALESS\x01");
// No more views per area than this are read; anything beyond is damage.
constexpr quint32 MaxViews = 64;
constexpr quint32 MaxPanes = 2;

void prepare(QDataStream &stream)
{
    stream.setVersion(QDataStream::Qt_6_0);
}

void writeViews(QDataStream &stream, const WhaleViewsState &views)
{
    stream << views.activeViewId;
    // Sorted, so that equal states give equal bytes.
    QStringList ids = views.states.keys();
    std::sort(ids.begin(), ids.end());
    stream << quint32(ids.count());
    for (const QString &id : std::as_const(ids)) {
        stream << id << views.states.value(id);
    }
}

bool readViews(QDataStream &stream, WhaleViewsState *views)
{
    WhaleViewsState result;
    quint32 count = 0;
    stream >> result.activeViewId >> count;
    if (stream.status() != QDataStream::Ok || count > MaxViews) {
        return false;
    }
    for (quint32 i = 0; i < count; ++i) {
        QString id;
        QByteArray state;
        stream >> id >> state;
        if (stream.status() != QDataStream::Ok || id.isEmpty()) {
            return false;
        }
        result.states.insert(id, state);
    }
    *views = result;
    return true;
}

// Reads the magic and the version; false (with a warning) for damaged or newer data.
bool readHeader(QDataStream &stream, const QByteArray &data, const QByteArray &magic, const char *what)
{
    if (!data.startsWith(magic)) {
        return false;
    }
    stream.skipRawData(magic.size());
    quint32 version = 0;
    stream >> version;
    if (stream.status() != QDataStream::Ok || version == 0 || version > WhalePersistence::FormatVersion) {
        qCWarning(WhaleViewDebug) << "Ignoring Whale" << what << "data of format version" << version << "(this Whale reads up to"
                                  << WhalePersistence::FormatVersion << ")";
        return false;
    }
    return true;
}
}

QByteArray WhalePersistence::wrapLocationState(const QByteArray &dolphinState, const WhaleViewsState &views)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    prepare(stream);
    stream.writeRawData(locationMagic.constData(), locationMagic.size());
    stream << FormatVersion << dolphinState;
    writeViews(stream, views);
    return data;
}

bool WhalePersistence::unwrapLocationState(const QByteArray &data, QByteArray *dolphinState, WhaleViewsState *views)
{
    if (!data.startsWith(locationMagic)) {
        // Dolphin's own state of an entry written before Whale's envelope existed.
        *dolphinState = data;
        return false;
    }
    QDataStream stream(data);
    prepare(stream);
    QByteArray dolphin;
    WhaleViewsState whale;
    if (!readHeader(stream, data, locationMagic, "location")) {
        *dolphinState = QByteArray();
        return false;
    }
    stream >> dolphin;
    if (stream.status() != QDataStream::Ok || !readViews(stream, &whale)) {
        qCWarning(WhaleViewDebug) << "Ignoring damaged Whale location state";
        *dolphinState = QByteArray();
        return false;
    }
    *dolphinState = dolphin;
    *views = whale;
    return true;
}

QByteArray WhalePersistence::writeSessionState(const QList<WhaleViewsState> &panes)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    prepare(stream);
    stream.writeRawData(sessionMagic.constData(), sessionMagic.size());
    stream << FormatVersion << quint32(panes.count());
    for (const WhaleViewsState &views : panes) {
        writeViews(stream, views);
    }
    return data;
}

QList<WhaleViewsState> WhalePersistence::readSessionState(const QByteArray &data)
{
    if (data.isEmpty()) {
        return {}; // a session written without Whale's data
    }
    QDataStream stream(data);
    prepare(stream);
    if (!readHeader(stream, data, sessionMagic, "session")) {
        if (!data.startsWith(sessionMagic)) {
            qCWarning(WhaleViewDebug) << "Ignoring unknown Whale session data";
        }
        return {};
    }
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > MaxPanes) {
        qCWarning(WhaleViewDebug) << "Ignoring damaged Whale session data";
        return {};
    }
    QList<WhaleViewsState> panes;
    for (quint32 i = 0; i < count; ++i) {
        WhaleViewsState views;
        if (!readViews(stream, &views)) {
            qCWarning(WhaleViewDebug) << "Ignoring damaged Whale session data";
            return {};
        }
        panes.append(views);
    }
    return panes;
}
