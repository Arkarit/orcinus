/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewpreferences.h"

#include "whaleviewdebug.h"

#include <KConfigGroup>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace
{
const QString groupName = QStringLiteral("PreferredViews");
const QString scopeGroupName = QStringLiteral("ContentScopes");
}

WhaleViewPreferences *WhaleViewPreferences::instance()
{
    static WhaleViewPreferences preferences(KSharedConfig::openStateConfig(QStringLiteral("whaleviewpreferencesrc")));
    return &preferences;
}

WhaleViewPreferences::WhaleViewPreferences(KSharedConfigPtr config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
    const QString name = m_config->name();
    m_filePath = QDir::isAbsolutePath(name) ? name : QStandardPaths::writableLocation(m_config->locationType()) + QLatin1Char('/') + name;
}

WhaleViewPreferences::~WhaleViewPreferences() = default;

QString WhaleViewPreferences::locationKey(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty()) {
        return {};
    }
    QUrl location;
    if (url.isLocalFile()) {
        const QString path = url.toLocalFile();
        if (path.isEmpty()) {
            return {};
        }
        location = QUrl::fromLocalFile(QDir::cleanPath(path));
    } else {
        location = url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash | QUrl::RemoveFragment | QUrl::RemovePassword);
        if (location.path().isEmpty()) {
            location.setPath(QStringLiteral("/"));
        }
    }
    return location.toString(QUrl::FullyEncoded);
}

QString WhaleViewPreferences::entryKey(const QString &locationKey)
{
    // A hash: KConfig keys cannot hold any text (e.g. "[" starts a locale), URLs can be long.
    return QString::fromLatin1(QCryptographicHash::hash(locationKey.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

QString WhaleViewPreferences::preferredView(const QUrl &url)
{
    const QString key = locationKey(url);
    if (key.isEmpty()) {
        return {};
    }
    reloadIfChanged();
    return m_views.value(key);
}

void WhaleViewPreferences::setPreferredView(const QUrl &url, const QString &viewId)
{
    const QString key = locationKey(url);
    if (key.isEmpty() || viewId.isEmpty()) {
        return;
    }
    reloadIfChanged();
    if (m_views.value(key) == viewId) {
        return;
    }
    m_views.insert(key, viewId);
    m_config->group(groupName).writeEntry(entryKey(key), QStringList{viewId, key});
    write();
    qCDebug(WhaleViewDebug) << "Preferred view of" << key << "is now" << viewId;
    Q_EMIT changed(key);
}

bool WhaleViewPreferences::clearPreferredView(const QUrl &url)
{
    const QString key = locationKey(url);
    if (key.isEmpty()) {
        return false;
    }
    reloadIfChanged();
    if (!m_views.remove(key)) {
        return false;
    }
    m_config->group(groupName).deleteEntry(entryKey(key));
    write();
    Q_EMIT changed(key);
    return true;
}

int WhaleViewPreferences::count()
{
    reloadIfChanged();
    return m_views.count();
}

WhaleContentScope WhaleViewPreferences::contentScope(const QUrl &url)
{
    const QString key = locationKey(url);
    if (key.isEmpty()) {
        return WhaleContentScope::CurrentFolder;
    }
    reloadIfChanged();
    return m_scopes.value(key, WhaleContentScope::CurrentFolder);
}

void WhaleViewPreferences::setContentScope(const QUrl &url, WhaleContentScope scope)
{
    const QString key = locationKey(url);
    if (key.isEmpty()) {
        return;
    }
    reloadIfChanged();
    if (m_scopes.value(key, WhaleContentScope::CurrentFolder) == scope) {
        return;
    }
    KConfigGroup group = m_config->group(scopeGroupName);
    if (scope == WhaleContentScope::CurrentFolder) {
        m_scopes.remove(key);
        group.deleteEntry(entryKey(key));
    } else {
        m_scopes.insert(key, scope);
        group.writeEntry(entryKey(key), QStringList{WhaleContentScopes::name(scope), key});
    }
    write();
    qCDebug(WhaleViewDebug) << "Content scope of" << key << "is now" << WhaleContentScopes::name(scope);
    Q_EMIT contentScopeChanged(key);
}

int WhaleViewPreferences::contentScopeCount()
{
    reloadIfChanged();
    return m_scopes.count();
}

void WhaleViewPreferences::reloadIfChanged()
{
    // One stat per lookup notices what another Whale process wrote.
    const QDateTime modified = QFileInfo(m_filePath).lastModified();
    if (m_loaded && modified == m_fileModified) {
        return;
    }
    if (m_loaded) {
        m_config->reparseConfiguration();
    }
    m_fileModified = modified;
    load();
}

void WhaleViewPreferences::load()
{
    m_loaded = true;
    m_views.clear();
    const KConfigGroup group = m_config->group(groupName);
    const QStringList keys = group.keyList();
    for (const QString &key : keys) {
        const QStringList entry = group.readEntry(key, QStringList());
        if (entry.count() != 2 || entry.at(0).isEmpty() || entryKey(entry.at(1)) != key) {
            qCDebug(WhaleViewDebug) << "Ignoring the damaged preferred view entry" << key;
            continue;
        }
        m_views.insert(entry.at(1), entry.at(0));
    }

    m_scopes.clear();
    const KConfigGroup scopeGroup = m_config->group(scopeGroupName);
    const QStringList scopeKeys = scopeGroup.keyList();
    for (const QString &key : scopeKeys) {
        const QStringList entry = scopeGroup.readEntry(key, QStringList());
        const std::optional<WhaleContentScope> scope = entry.count() == 2 ? WhaleContentScopes::fromName(entry.at(0)) : std::nullopt;
        if (!scope || entryKey(entry.at(1)) != key) {
            qCDebug(WhaleViewDebug) << "Ignoring the damaged content scope entry" << key;
            continue;
        }
        m_scopes.insert(entry.at(1), *scope);
    }
}

void WhaleViewPreferences::write()
{
    // KConfig merges the entries written here into the file as it is on disk, so entries
    // that another process wrote in the meantime survive.
    if (!m_config->sync()) {
        qCWarning(WhaleViewDebug) << "Cannot write the preferred views to" << m_filePath;
    }
    m_fileModified = QFileInfo(m_filePath).lastModified();
}

#include "moc_whaleviewpreferences.cpp"
