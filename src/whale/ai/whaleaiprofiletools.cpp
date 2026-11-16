/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaiprofiletools.h"

#include "dolphinmainwindow.h"
#include "whaleaichangelog.h"
#include "whalestylemanager.h"
#include "whalestyles.h"
#include "whalestylestore.h"

#include <KLocalizedString>

#include <QJsonArray>
#include <QPointer>

namespace
{
QJsonObject failed(bool *error, const QString &message)
{
    *error = true;
    return {{QStringLiteral("error"), message}};
}

QJsonObject property(const QString &type, const QString &description)
{
    return {{QStringLiteral("type"), type}, {QStringLiteral("description"), description}};
}

/** The profile named or with the id @p text (ignoring case), or nullptr. */
const WhaleStyle *findProfile(const QString &text)
{
    const WhaleStyleStore &store = WhaleStyles::self()->store();
    if (const WhaleStyle *byId = store.style(text)) {
        return byId;
    }
    for (const QString &id : store.ids()) {
        const WhaleStyle *style = store.style(id);
        if (style && style->name.compare(text, Qt::CaseInsensitive) == 0) {
            return style;
        }
    }
    return nullptr;
}

QJsonObject listProfiles(DolphinMainWindow *window)
{
    QJsonArray profiles;
    for (const WhaleStyle &style : WhaleStyles::self()->store().styles()) {
        QJsonArray elements;
        for (auto it = style.elements.cbegin(); it != style.elements.cend(); ++it) {
            if (it->enabled) {
                elements.append(it.key());
            }
        }
        profiles.append(QJsonObject{{QStringLiteral("id"), style.id}, {QStringLiteral("name"), style.name}, {QStringLiteral("sets"), elements}});
    }
    const QString last = WhaleStyleManager::ensure(window)->lastStyleId();
    return {{QStringLiteral("profiles"), profiles}, {QStringLiteral("lastProfile"), last.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(last)}};
}

QJsonObject applyProfile(DolphinMainWindow *window, const QJsonObject &arguments, bool *error, QString *notice)
{
    const WhaleStyle *found = findProfile(arguments.value(QLatin1String("profile")).toString());
    if (!found) {
        return failed(error, QStringLiteral("No such profile (see list_profiles)."));
    }
    const WhaleStyle profile = *found;
    WhaleStyleManager *manager = WhaleStyleManager::ensure(window);
    // Undo applies the window as it was before the agent's first profile (AI9e).
    QPointer<DolphinMainWindow> guard(window);
    const WhaleStyle before = manager->captureStyle();
    WhaleAiChangeLog::of(window)->record(QStringLiteral("profile:applied"), i18nc("@info", "the look of the window"), [guard, before] {
        if (guard) {
            WhaleStyleManager::ensure(guard.data())->applyStyle(before);
        }
    });
    manager->applyStyle(profile.id);
    *notice = i18nc("@info %1 is the name of a profile", "The agent applied the profile “%1”.", profile.name);
    return {{QStringLiteral("applied"), profile.id}, {QStringLiteral("name"), profile.name}};
}

QJsonObject saveProfile(DolphinMainWindow *window, const QJsonObject &arguments, bool *error, QString *notice)
{
    const QString name = arguments.value(QLatin1String("name")).toString().trimmed();
    if (name.isEmpty()) {
        return failed(error, QStringLiteral("\"name\" must be the name of the profile."));
    }
    const bool replaces = findProfile(name) != nullptr;
    // Undo puts back the profiles as they were before the agent's first save (AI9e).
    const QList<WhaleStyle> before = WhaleStyles::self()->store().styles();
    WhaleAiChangeLog::of(window)->record(QStringLiteral("profile:saved"), i18nc("@info", "the profiles"), [before] {
        WhaleStyles::self()->editableStore().replaceStyles(before);
        WhaleStyles::self()->save();
    });
    const QString id = WhaleStyleManager::ensure(window)->saveCurrentAsStyle(name);
    *notice = replaces ? i18nc("@info %1 is the name of a profile", "The agent saved the window as the profile “%1” again.", name)
                       : i18nc("@info %1 is the name of a profile", "The agent saved the window as the new profile “%1”.", name);
    return {{QStringLiteral("saved"), id}, {QStringLiteral("name"), name}, {QStringLiteral("replaced"), replaces}};
}
}

QList<WhaleAiWindowTool> WhaleAiProfileTools::create(DolphinMainWindow *window)
{
    QPointer<DolphinMainWindow> guard(window);
    auto bind = [guard](auto function) {
        return [guard, function](const QJsonObject &arguments, bool *error, QString *notice) -> QJsonObject {
            if (!guard) {
                return failed(error, QStringLiteral("The window is gone."));
            }
            return function(guard.data(), arguments, error, notice);
        };
    };
    return {
        {QStringLiteral("list_profiles"),
         QStringLiteral("List the profiles of Orcinus"),
         QStringLiteral("Lists the user's profiles (saved looks of the window: view modes, toolbars, panels, split, ...): id, name and "
                        "what each sets, plus the profile applied last."),
         true,
         {},
         {},
         bind([](DolphinMainWindow *window, const QJsonObject &, bool *, QString *) {
             return listProfiles(window);
         })},
        {QStringLiteral("apply_profile"),
         QStringLiteral("Apply a profile in Orcinus"),
         QStringLiteral("Applies one of the user's profiles to the window, as its entry in the Profiles menu does. The user can undo it."),
         false,
         {{QStringLiteral("profile"), property(QStringLiteral("string"), QStringLiteral("Name or id from list_profiles"))}},
         {QStringLiteral("profile")},
         bind(applyProfile)},
        {QStringLiteral("save_profile"),
         QStringLiteral("Save the window as a profile"),
         QStringLiteral("Saves the current look of the window as a profile of the user: a profile of that name is overwritten, else a "
                        "new one is added. Saved, so only when the user asks for it; refused in a plan or read-only mode. The user can "
                        "undo it."),
         false,
         {{QStringLiteral("name"), property(QStringLiteral("string"), QStringLiteral("Name of the profile"))}},
         {QStringLiteral("name")},
         bind(saveProfile)},
    };
}
