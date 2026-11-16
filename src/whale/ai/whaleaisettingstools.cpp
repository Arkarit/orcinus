/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaisettingstools.h"

#include "dolphin_compactmodesettings.h"
#include "dolphin_contentdisplaysettings.h"
#include "dolphin_contextmenusettings.h"
#include "dolphin_detailsmodesettings.h"
#include "dolphin_folderspanelsettings.h"
#include "dolphin_generalsettings.h"
#include "dolphin_iconsmodesettings.h"
#include "dolphin_placespanelsettings.h"
#include "dolphinmainwindow.h"
#include "dolphinurlnavigatorscontroller.h"

#include <KCoreConfigSkeleton>
#include <KLocalizedString>

#include <QJsonArray>
#include <QPointer>

namespace
{
struct Allowed {
    KCoreConfigSkeleton *skeleton;
    QStringList entries;
};

/**
 * The entries whose page of the settings dialog only writes the value. Everything else stays out
 * (see the plan, AI9d): confirmations, the home folder, internal entries, entries whose page does
 * more than write them, fonts, the hand-written keys.
 */
const QList<Allowed> &allowed()
{
    static const QList<Allowed> list{
        {GeneralSettings::self(),
         {QStringLiteral("ShowFullPath"),
          QStringLiteral("ShowFullPathInTitlebar"),
          QStringLiteral("EditableUrl"),
          QStringLiteral("AlwaysShowTabBar"),
          QStringLiteral("ShowCloseButtonOnTabs"),
          QStringLiteral("TabStyle"),
          QStringLiteral("OpenNewTabAfterLastTab"),
          QStringLiteral("OpenExternallyCalledFolderInNewTab"),
          QStringLiteral("FollowSymlinksToTarget"),
          QStringLiteral("SplitViewAtStartup"),
          QStringLiteral("CloseSplitViewChoice"),
          QStringLiteral("FilterBar"),
          QStringLiteral("BrowseThroughArchives"),
          QStringLiteral("RenameInline"),
          QStringLiteral("ShowSelectionToggle"),
          QStringLiteral("ShowPasteBarAfterCopying"),
          QStringLiteral("ShowToolTips"),
          QStringLiteral("AutoExpandFolders"),
          QStringLiteral("ShowStatusBar"),
          QStringLiteral("ShowZoomSlider"),
          QStringLiteral("EnlargeSmallPreviews"),
          QStringLiteral("SortingChoice"),
          QStringLiteral("HideXTrashFile"),
          QStringLiteral("DynamicView")}},
        {ContentDisplaySettings::self(),
         {QStringLiteral("DirectorySizeMode"),
          QStringLiteral("RecursiveDirectorySizeLimit"),
          QStringLiteral("UseShortRelativeDates"),
          QStringLiteral("UsePermissionsFormat"),
          QStringLiteral("ElidingMode")}},
        {DetailsModeSettings::self(),
         {QStringLiteral("IconSize"),
          QStringLiteral("PreviewSize"),
          QStringLiteral("DragFromAnywhereOnRow"),
          QStringLiteral("ExpandableFolders"),
          QStringLiteral("Density"),
          QStringLiteral("CustomPadding"),
          QStringLiteral("CustomLineHeight")}},
        {IconsModeSettings::self(),
         {QStringLiteral("IconSize"),
          QStringLiteral("PreviewSize"),
          QStringLiteral("TextWidthIndex"),
          QStringLiteral("CompactSpacing"),
          QStringLiteral("MaximumTextLines"),
          QStringLiteral("Density"),
          QStringLiteral("CustomPadding"),
          QStringLiteral("CustomLineHeight")}},
        {CompactModeSettings::self(),
         {QStringLiteral("IconSize"),
          QStringLiteral("PreviewSize"),
          QStringLiteral("MaximumTextWidthIndex"),
          QStringLiteral("ExpandableFolders"),
          QStringLiteral("Density"),
          QStringLiteral("CustomPadding"),
          QStringLiteral("CustomLineHeight")}},
        {ContextMenuSettings::self(),
         {QStringLiteral("ShowCopyMoveMenu"),
          QStringLiteral("ShowAddToPlaces"),
          QStringLiteral("ShowSortBy"),
          QStringLiteral("ShowViewMode"),
          QStringLiteral("ShowOpenInNewTab"),
          QStringLiteral("ShowOpenInNewWindow"),
          QStringLiteral("ShowOpenInSplitView"),
          QStringLiteral("ShowCopyLocation"),
          QStringLiteral("ShowDuplicateHere"),
          QStringLiteral("ShowOpenTerminal"),
          QStringLiteral("ShowCopyToOtherSplitView"),
          QStringLiteral("ShowMoveToOtherSplitView")}},
        {FoldersPanelSettings::self(), {QStringLiteral("HiddenFilesShown"), QStringLiteral("LimitFoldersPanelToHome"), QStringLiteral("AutoScrolling")}},
        {PlacesPanelSettings::self(), {QStringLiteral("IconSize")}},
    };
    return list;
}

/** Entries that act on what happens next, not on what is shown now. */
bool appliesLater(const QString &key)
{
    static const QStringList later{
        QStringLiteral("General/OpenNewTabAfterLastTab"),
        QStringLiteral("General/OpenExternallyCalledFolderInNewTab"),
        QStringLiteral("General/FollowSymlinksToTarget"),
        QStringLiteral("General/SplitViewAtStartup"),
        QStringLiteral("General/CloseSplitViewChoice"),
        QStringLiteral("General/FilterBar"),
        QStringLiteral("General/BrowseThroughArchives"),
        QStringLiteral("General/RenameInline"),
        QStringLiteral("General/ShowPasteBarAfterCopying"),
    };
    return later.contains(key);
}

/**
 * The choices of the enum entries, in the order of their .kcfg (the value is the index). Entries
 * with a change signal are wrapped in KConfigCompilerSignallingItem, which hides the ItemEnum and
 * its choices; dolphinmainwindowtest::testAiSettingsTools compares this table
 * with the .kcfg files.
 */
const QHash<QString, QStringList> &enumChoices()
{
    static const QHash<QString, QStringList> choices{
        {QStringLiteral("General/TabStyle"), {QStringLiteral("AutoSize"), QStringLiteral("FullWidth"), QStringLiteral("FixedSize")}},
        {QStringLiteral("General/SplitViewAtStartup"), {QStringLiteral("LastState"), QStringLiteral("AlwaysSplit"), QStringLiteral("AlwaysSingle")}},
        {QStringLiteral("General/CloseSplitViewChoice"), {QStringLiteral("ActiveView"), QStringLiteral("InactiveView"), QStringLiteral("RightView")}},
        {QStringLiteral("General/ShowStatusBar"), {QStringLiteral("Small"), QStringLiteral("FullWidth"), QStringLiteral("Disabled")}},
        {QStringLiteral("General/SortingChoice"),
         {QStringLiteral("NaturalSorting"), QStringLiteral("CaseInsensitiveSorting"), QStringLiteral("CaseSensitiveSorting")}},
        {QStringLiteral("ContentDisplay/DirectorySizeMode"), {QStringLiteral("ContentCount"), QStringLiteral("ContentSize"), QStringLiteral("None")}},
        {QStringLiteral("ContentDisplay/UsePermissionsFormat"),
         {QStringLiteral("SymbolicFormat"), QStringLiteral("NumericFormat"), QStringLiteral("CombinedFormat")}},
        {QStringLiteral("ContentDisplay/ElidingMode"), {QStringLiteral("Right"), QStringLiteral("Middle")}},
        {QStringLiteral("DetailsMode/Density"), {QStringLiteral("Normal"), QStringLiteral("Compact"), QStringLiteral("VeryCompact"), QStringLiteral("Custom")}},
        {QStringLiteral("IconsMode/Density"), {QStringLiteral("Normal"), QStringLiteral("Compact"), QStringLiteral("VeryCompact"), QStringLiteral("Custom")}},
        {QStringLiteral("CompactMode/Density"), {QStringLiteral("Normal"), QStringLiteral("Compact"), QStringLiteral("VeryCompact"), QStringLiteral("Custom")}},
    };
    return choices;
}

struct Entry {
    QString key;
    KCoreConfigSkeleton *skeleton = nullptr;
    KConfigSkeletonItem *item = nullptr;
};

QList<Entry> entries()
{
    QList<Entry> result;
    for (const Allowed &group : allowed()) {
        for (const QString &name : group.entries) {
            KConfigSkeletonItem *item = group.skeleton->findItem(name);
            if (!item) {
                continue; // an entry of an older or newer version
            }
            result.append({item->group() + QLatin1Char('/') + name, group.skeleton, item});
        }
    }
    return result;
}

QString typeOf(const Entry &entry)
{
    if (enumChoices().contains(entry.key)) {
        return QStringLiteral("enum");
    }
    switch (entry.item->property().typeId()) {
    case QMetaType::Bool:
        return QStringLiteral("bool");
    case QMetaType::Int:
    case QMetaType::UInt:
        return QStringLiteral("int");
    default:
        return QString();
    }
}

QJsonValue valueOf(const Entry &entry)
{
    if (enumChoices().contains(entry.key)) {
        const QStringList choices = enumChoices().value(entry.key);
        const int index = entry.item->property().toInt();
        return index >= 0 && index < choices.size() ? QJsonValue(choices.at(index)) : QJsonValue(index);
    }
    const QVariant value = entry.item->property();
    if (value.typeId() == QMetaType::Bool) {
        return value.toBool();
    }
    return value.toLongLong();
}

/** The value for the transcript, as the user would read it. */
QString shownValue(const Entry &entry)
{
    if (enumChoices().contains(entry.key)) {
        return valueOf(entry).toVariant().toString();
    }
    const QVariant value = entry.item->property();
    if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? i18nc("@info a setting's value", "on") : i18nc("@info a setting's value", "off");
    }
    return value.toString();
}

QString labelOf(const KConfigSkeletonItem *item)
{
    return item->label().isEmpty() ? item->name() : item->label();
}

QJsonObject describe(const Entry &entry)
{
    QJsonObject result{{QStringLiteral("key"), entry.key},
                       {QStringLiteral("label"), labelOf(entry.item)},
                       {QStringLiteral("type"), typeOf(entry)},
                       {QStringLiteral("value"), valueOf(entry)},
                       {QStringLiteral("isDefault"), entry.item->isDefault()},
                       {QStringLiteral("applies"), appliesLater(entry.key) ? QStringLiteral("next time") : QStringLiteral("now")}};
    if (enumChoices().contains(entry.key)) {
        result.insert(QStringLiteral("choices"), QJsonArray::fromStringList(enumChoices().value(entry.key)));
    } else {
        if (entry.item->minValue().isValid()) {
            result.insert(QStringLiteral("min"), entry.item->minValue().toLongLong());
        }
        if (entry.item->maxValue().isValid()) {
            result.insert(QStringLiteral("max"), entry.item->maxValue().toLongLong());
        }
    }
    return result;
}

QJsonObject failed(bool *error, const QString &message)
{
    *error = true;
    return {{QStringLiteral("error"), message}};
}

QJsonObject listSettings(const QJsonObject &arguments)
{
    const QString filter = arguments.value(QLatin1String("filter")).toString();
    QJsonArray list;
    for (const Entry &entry : entries()) {
        if (!filter.isEmpty() && !entry.key.contains(filter, Qt::CaseInsensitive) && !labelOf(entry.item).contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        list.append(describe(entry));
    }
    return {{QStringLiteral("settings"), list}, {QStringLiteral("count"), list.size()}};
}

QJsonObject setSetting(DolphinMainWindow *window, const QJsonObject &arguments, bool *error, QString *notice)
{
    const QString key = arguments.value(QLatin1String("key")).toString();
    const QJsonValue value = arguments.value(QLatin1String("value"));
    const QList<Entry> all = entries();
    const auto found = std::find_if(all.cbegin(), all.cend(), [&key](const Entry &entry) {
        return entry.key.compare(key, Qt::CaseInsensitive) == 0;
    });
    if (found == all.cend()) {
        return failed(error, QStringLiteral("Unknown or unavailable setting \"%1\" (see list_settings).").arg(key));
    }
    const Entry entry = *found;
    const QString type = typeOf(entry);
    QVariant newValue;
    if (type == QLatin1String("bool")) {
        if (!value.isBool()) {
            return failed(error, QStringLiteral("\"value\" must be true or false."));
        }
        newValue = value.toBool();
    } else if (type == QLatin1String("enum")) {
        const QStringList choices = enumChoices().value(entry.key);
        int index = -1;
        for (int i = 0; i < choices.size(); ++i) {
            if (choices.at(i).compare(value.toString(), Qt::CaseInsensitive) == 0) {
                index = i;
            }
        }
        if (index < 0) {
            return failed(error, QStringLiteral("\"value\" must be one of: %1.").arg(choices.join(QStringLiteral(", "))));
        }
        newValue = index;
    } else if (type == QLatin1String("int")) {
        if (!value.isDouble() || value.toDouble() != double(qint64(value.toDouble()))) {
            return failed(error, QStringLiteral("\"value\" must be a whole number."));
        }
        const qint64 number = qint64(value.toDouble());
        const QVariant min = entry.item->minValue();
        const QVariant max = entry.item->maxValue();
        if ((min.isValid() && number < min.toLongLong()) || (max.isValid() && number > max.toLongLong())
            || (entry.item->property().typeId() == QMetaType::UInt && number < 0)) {
            return failed(error, QStringLiteral("\"value\" is out of range."));
        }
        newValue = entry.item->property().typeId() == QMetaType::UInt ? QVariant(uint(number)) : QVariant(int(number));
    } else {
        return failed(error, QStringLiteral("This setting cannot be changed here."));
    }

    const QJsonValue previous = valueOf(entry);
    QJsonObject result{{QStringLiteral("key"), entry.key}, {QStringLiteral("previous"), previous}};
    if (entry.item->property() != newValue) {
        entry.item->setProperty(newValue);
        // As the settings dialog's Apply: save, then the window reads everything again.
        entry.skeleton->save();
        QMetaObject::invokeMethod(window, "refreshViews");
        DolphinUrlNavigatorsController::slotReadSettings();
        *notice =
            i18nc("@info %1 is the name of a setting, %2 its new value", "The agent changed the setting “%1” to %2.", labelOf(entry.item), shownValue(entry));
    } else {
        result.insert(QStringLiteral("unchanged"), true);
    }
    result.insert(QStringLiteral("value"), valueOf(entry));
    result.insert(QStringLiteral("applies"), appliesLater(entry.key) ? QStringLiteral("next time") : QStringLiteral("now"));
    return result;
}
}

QStringList WhaleAiSettingsTools::choices(const QString &key)
{
    return enumChoices().value(key);
}

QStringList WhaleAiSettingsTools::keys()
{
    QStringList result;
    for (const Entry &entry : entries()) {
        result.append(entry.key);
    }
    return result;
}

QList<WhaleAiWindowTool> WhaleAiSettingsTools::create(DolphinMainWindow *window)
{
    QPointer<DolphinMainWindow> guard(window);
    return {
        {QStringLiteral("list_settings"),
         QStringLiteral("List settings of Orcinus"),
         QStringLiteral("Lists the settings of Orcinus an agent may change: key, label, type (bool, int, enum), current value, choices or "
                        "range, and whether a change applies now or next time (new tabs, the next start, ...). Filter by part of the key "
                        "or label. Settings not listed (confirmations, the home folder, AI settings, ...) are for the user to change."),
         true,
         {{QStringLiteral("filter"),
           QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Part of a key or label")}}}},
         {},
         [](const QJsonObject &arguments, bool *, QString *) {
             return listSettings(arguments);
         }},
        {QStringLiteral("set_setting"),
         QStringLiteral("Change a setting of Orcinus"),
         QStringLiteral("Changes one setting from list_settings and applies it as the settings dialog does. The setting is saved, so "
                        "change settings only when the user asks for it; for the current view prefer trigger_action (view mode, "
                        "sorting, hidden files), which is not a lasting setting."),
         false,
         {{QStringLiteral("key"),
           QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("description"), QStringLiteral("Key from list_settings, e.g. General/ShowStatusBar")}}},
          {QStringLiteral("value"), QJsonObject{{QStringLiteral("description"), QStringLiteral("true/false, a whole number, or the name of a choice")}}}},
         {QStringLiteral("key"), QStringLiteral("value")},
         [guard](const QJsonObject &arguments, bool *error, QString *notice) {
             if (!guard) {
                 return failed(error, QStringLiteral("The window is gone."));
             }
             return setSetting(guard.data(), arguments, error, notice);
         }},
    };
}
