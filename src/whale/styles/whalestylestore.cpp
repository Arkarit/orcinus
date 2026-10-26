/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalestylestore.h"

#include "config-dolphin.h"

#include <KConfigGroup>

#include <algorithm>

namespace
{
const QString GeneralGroup = QStringLiteral("General");
const QString StyleGroup = QStringLiteral("Style");
const QString ElementGroup = QStringLiteral("Element");
const QString VersionKey = QStringLiteral("Version");
const QString OrderKey = QStringLiteral("Order");
const QString NameKey = QStringLiteral("Name");
const QString ShortcutKey = QStringLiteral("Shortcut");
const QString EnabledKey = QStringLiteral("Enabled");
const QString ValueKey = QStringLiteral("Value");

WhaleStyle readStyle(const KConfigGroup &group)
{
    WhaleStyle style;
    style.id = group.name();
    style.name = group.readEntry(NameKey, QString()).trimmed();
    if (style.name.isEmpty()) {
        style.name = style.id;
    }
    style.shortcut = QKeySequence::fromString(group.readEntry(ShortcutKey, QString()), QKeySequence::PortableText);

    const QMap<QString, QString> entries = group.entryMap();
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        if (it.key() != NameKey && it.key() != ShortcutKey) {
            style.unknownEntries.insert(it.key(), it.value());
        }
    }

    const KConfigGroup elements = group.group(ElementGroup);
    const QStringList keys = elements.groupList();
    for (const QString &key : keys) {
        const KConfigGroup element = elements.group(key);
        style.elements.insert(key, {element.readEntry(EnabledKey, false), element.readEntry(ValueKey, QString())});
    }
    return style;
}

void writeStyle(KConfigGroup group, const WhaleStyle &style)
{
    for (auto it = style.unknownEntries.cbegin(); it != style.unknownEntries.cend(); ++it) {
        group.writeEntry(it.key(), it.value());
    }
    group.writeEntry(NameKey, style.name);
    if (!style.shortcut.isEmpty()) {
        group.writeEntry(ShortcutKey, style.shortcut.toString(QKeySequence::PortableText));
    }

    KConfigGroup elements = group.group(ElementGroup);
    for (auto it = style.elements.cbegin(); it != style.elements.cend(); ++it) {
        KConfigGroup element = elements.group(it.key());
        element.writeEntry(EnabledKey, it.value().enabled);
        element.writeEntry(ValueKey, it.value().value);
    }
}
}

int WhaleStyleStore::currentVersion()
{
    return 1;
}

KSharedConfigPtr WhaleStyleStore::defaultConfig()
{
    return KSharedConfig::openConfig(QStringLiteral(WHALE_COMPONENT "stylesrc"));
}

QString WhaleStyleStore::idForName(const QString &name, const QStringList &takenIds)
{
    // Letters with accents lose them ("ö" -> "o"); everything else that is not an ASCII letter
    // or digit separates words. IDs end up in group and action names, so they stay plain.
    const QString decomposed = name.normalized(QString::NormalizationForm_KD).toLower();
    QString base;
    bool separate = false;
    for (const QChar c : decomposed) {
        if (c.isMark()) {
            continue;
        }
        if ((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9')) {
            if (separate && !base.isEmpty()) {
                base += u'-';
            }
            base += c;
            separate = false;
        } else {
            separate = true;
        }
    }
    if (base.isEmpty()) {
        base = QStringLiteral("style");
    }

    QString id = base;
    for (int i = 2; takenIds.contains(id); ++i) {
        id = base + u'-' + QString::number(i);
    }
    return id;
}

WhaleStyleStore::WhaleStyleStore(KSharedConfigPtr config)
    : m_config(std::move(config))
    , m_fileVersion(currentVersion())
{
    load();
}

void WhaleStyleStore::load()
{
    m_config->reparseConfiguration();
    m_styles.clear();

    const KConfigGroup general = m_config->group(GeneralGroup);
    m_fileVersion = std::max(general.readEntry(VersionKey, currentVersion()), currentVersion());

    const KConfigGroup styles = m_config->group(StyleGroup);
    QStringList remaining = styles.groupList();
    remaining.sort();

    // The order first, then styles missing in it (e.g. written by hand) by their IDs.
    QStringList ordered;
    const QStringList order = general.readEntry(OrderKey, QStringList());
    for (const QString &id : order) {
        if (remaining.removeOne(id)) {
            ordered.append(id);
        }
    }
    ordered += remaining;

    for (const QString &id : std::as_const(ordered)) {
        m_styles.append(readStyle(styles.group(id)));
    }
}

void WhaleStyleStore::save()
{
    m_config->deleteGroup(StyleGroup);
    KConfigGroup styles = m_config->group(StyleGroup);
    for (const WhaleStyle &style : std::as_const(m_styles)) {
        writeStyle(styles.group(style.id), style);
    }

    KConfigGroup general = m_config->group(GeneralGroup);
    general.writeEntry(VersionKey, m_fileVersion);
    general.writeEntry(OrderKey, ids());
    m_config->sync();
}

int WhaleStyleStore::fileVersion() const
{
    return m_fileVersion;
}

QList<WhaleStyle> WhaleStyleStore::styles() const
{
    return m_styles;
}

QStringList WhaleStyleStore::ids() const
{
    QStringList result;
    result.reserve(m_styles.size());
    for (const WhaleStyle &style : m_styles) {
        result.append(style.id);
    }
    return result;
}

const WhaleStyle *WhaleStyleStore::style(const QString &id) const
{
    const int index = indexOf(id);
    return index < 0 ? nullptr : &m_styles.at(index);
}

QString WhaleStyleStore::addStyle(WhaleStyle style)
{
    style.name = style.name.trimmed();
    if (style.name.isEmpty()) {
        return QString();
    }
    style.id = idForName(style.name, ids());
    m_styles.append(style);
    return style.id;
}

bool WhaleStyleStore::updateStyle(const WhaleStyle &style)
{
    const int index = indexOf(style.id);
    if (index < 0 || style.name.trimmed().isEmpty()) {
        return false;
    }
    m_styles[index] = style;
    m_styles[index].name = style.name.trimmed();
    return true;
}

bool WhaleStyleStore::renameStyle(const QString &id, const QString &name)
{
    const int index = indexOf(id);
    if (index < 0 || name.trimmed().isEmpty()) {
        return false;
    }
    m_styles[index].name = name.trimmed();
    return true;
}

bool WhaleStyleStore::removeStyle(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0) {
        return false;
    }
    m_styles.removeAt(index);
    return true;
}

bool WhaleStyleStore::moveStyle(const QString &id, int index)
{
    const int from = indexOf(id);
    if (from < 0) {
        return false;
    }
    m_styles.move(from, std::clamp(index, 0, int(m_styles.size()) - 1));
    return true;
}

bool WhaleStyleStore::replaceStyles(const QList<WhaleStyle> &styles)
{
    QStringList seen;
    for (const WhaleStyle &style : styles) {
        if (style.id.isEmpty() || seen.contains(style.id) || style.name.trimmed().isEmpty()) {
            return false;
        }
        seen.append(style.id);
    }
    m_styles = styles;
    for (WhaleStyle &style : m_styles) {
        style.name = style.name.trimmed();
    }
    return true;
}

int WhaleStyleStore::indexOf(const QString &id) const
{
    for (int i = 0; i < m_styles.size(); ++i) {
        if (m_styles.at(i).id == id) {
            return i;
        }
    }
    return -1;
}
