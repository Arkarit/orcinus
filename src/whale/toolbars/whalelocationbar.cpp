/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 * SPDX-FileCopyrightText: 2020 Felix Ernst <felixernst@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// The buttons and the remote: behaviour follow Dolphin's DolphinNavigatorsWidgetAction
// (LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL).

#include "whalelocationbar.h"

#include "dolphinurlnavigator.h"
#include "dolphinviewcontainer.h"
#include "trash/dolphintrash.h"

#include <KIO/ApplicationLauncherJob>
#include <KIO/CommandLauncherJob>
#include <KJob>
#include <KLocalizedString>
#include <KNotificationJobUiDelegate>
#include <KService>

#include <QEvent>
#include <QHBoxLayout>
#include <QPushButton>

#include <algorithm>

namespace
{
/**
 * Places the location bar in its toolbar. The widget is the existing location bar, never a
 * new one, and taking it out of the toolbar only hides it: it stays the toolbar's child and
 * connected to its view. (QWidgetAction::setDefaultWidget() and QToolBar::addWidget() would
 * take it from the toolbar while it is not placed, and addWidget() would make a new action
 * each time.)
 */
class LocationBarAction : public QWidgetAction
{
public:
    LocationBarAction(WhaleLocationBar *locationBar, QObject *parent)
        : QWidgetAction(parent)
        , m_locationBar(locationBar)
    {
    }

protected:
    QWidget *createWidget(QWidget *parent) override
    {
        // Only in the toolbar it belongs to: one widget, one place.
        if (!m_locationBar || parent != m_locationBar->parentWidget()) {
            return nullptr;
        }
        return m_locationBar;
    }

    void deleteWidget(QWidget *widget) override
    {
        widget->hide();
    }

private:
    QPointer<WhaleLocationBar> m_locationBar;
};

QPushButton *newButton(const QString &icon, const QString &text, QWidget *parent)
{
    auto button = new QPushButton(QIcon::fromTheme(icon), text, parent);
    button->setFlat(true);
    // A flat QPushButton is taller than the url navigator next to it, so let the navigator
    // decide the height of that row (Dolphin bug 522650).
    button->setSizePolicy(button->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
    button->hide();
    return button;
}

// Shows the focus indicator of the location bars of @p window if it shows more than one.
void updateFocusIndicators(QWidget *window)
{
    const auto locationBars = window->findChildren<WhaleLocationBar *>();
    const auto shown = std::count_if(locationBars.cbegin(), locationBars.cend(), [](const WhaleLocationBar *locationBar) {
        return locationBar->isVisible();
    });
    for (WhaleLocationBar *locationBar : locationBars) {
        locationBar->urlNavigator()->setHighlightFocusIndicator(shown > 1);
    }
}
}

WhaleLocationBar::WhaleLocationBar(DolphinViewContainer *container, QWidget *parent)
    : QWidget(parent)
    , m_container(container)
    , m_urlNavigator(new DolphinUrlNavigator(this))
    , m_action(new LocationBarAction(this, parent))
{
    Q_ASSERT(container);
    setObjectName(actionName());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // In a toolbar: Breeze draws the navigator's frame on the toolbar's background.
    m_urlNavigator->setBackgroundEnabled(true);

    m_emptyTrashButton = newButton(QStringLiteral("edit-delete"), i18nc("@action:button", "Empty Trash"), this);
    m_emptyTrashButton->setToolTip(i18n("Empties Trash to create free space"));
    connect(m_emptyTrashButton, &QPushButton::clicked, this, [this]() {
        m_emptyTrashButton->setDisabled(true);
        KJob *job = Trash::empty(this);
        connect(job, &KJob::result, m_emptyTrashButton, [this]() {
            m_emptyTrashButton->setDisabled(Trash::isEmpty());
        });
    });
    connect(&Trash::instance(), &Trash::emptinessChanged, m_emptyTrashButton, &QPushButton::setDisabled);
    m_emptyTrashButton->setDisabled(Trash::isEmpty());

    m_networkFolderButton = newButton(QStringLiteral("folder-add"), i18nc("@action:button", "Add Network Folder"), this);
    connect(m_networkFolderButton, &QPushButton::clicked, this, [this]() {
        const KService::Ptr service = KService::serviceByDesktopName(QStringLiteral("org.kde.knetattach"));
        auto job = new KIO::ApplicationLauncherJob(service, m_networkFolderButton);
        auto delegate = new KNotificationJobUiDelegate;
        delegate->setAutoErrorHandlingEnabled(true);
        job->setUiDelegate(delegate);
        job->start();
    });

    m_kdeConnectButton = newButton(QStringLiteral("kdeconnect"), i18nc("@action:button", "Open KDE Connect"), this);
    connect(m_kdeConnectButton, &QPushButton::clicked, this, [this]() {
        QStringList args;
        if (const QString deviceId = m_urlNavigator->locationUrl().host(); !deviceId.isEmpty()) {
            args << QStringLiteral("--device") << deviceId;
        }
        auto job = new KIO::CommandLauncherJob(QStringLiteral("kdeconnect-app"), args);
        job->setDesktopName(QStringLiteral("org.kde.kdeconnect.app"));
        job->setUiDelegate(new KNotificationJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled));
        job->start();
    });

    auto layout = new QHBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_urlNavigator, 1);
    layout->addWidget(m_emptyTrashButton);
    layout->addWidget(m_networkFolderButton);
    layout->addWidget(m_kdeConnectButton);

    // Queued as in Dolphin: the navigator has finished with the new location.
    connect(m_urlNavigator, &KUrlNavigator::urlChanged, this, &WhaleLocationBar::updateForLocation, Qt::QueuedConnection);

    m_action->setObjectName(actionName());
    m_action->setText(i18nc("@action:intoolbar", "Location Bar"));
    m_action->setIcon(QIcon::fromTheme(QStringLiteral("edit-select-text")));

    // Shown when its toolbar places it (action()).
    hide();

    // For good: the container's visible navigator from now on, whether placed or not.
    container->connectUrlNavigator(m_urlNavigator);
    updateForLocation();
}

WhaleLocationBar::~WhaleLocationBar()
{
    if (QWidget *window = this->window(); window != this && isVisible()) {
        QMetaObject::invokeMethod(
            window,
            [window]() {
                updateFocusIndicators(window);
            },
            Qt::QueuedConnection);
    }
}

QString WhaleLocationBar::actionName()
{
    return QStringLiteral("whale_location_bar");
}

DolphinUrlNavigator *WhaleLocationBar::urlNavigator() const
{
    return m_urlNavigator;
}

QWidgetAction *WhaleLocationBar::action() const
{
    return m_action;
}

bool WhaleLocationBar::event(QEvent *event)
{
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
        QWidget *window = this->window();
        if (window != this) {
            QMetaObject::invokeMethod(
                window,
                [window]() {
                    updateFocusIndicators(window);
                },
                Qt::QueuedConnection);
        }
    }
    return QWidget::event(event);
}

void WhaleLocationBar::updateForLocation()
{
    const QString scheme = m_urlNavigator->locationUrl().scheme();
    // On remote: one most likely wants to enter a server URL (Dolphin bug 414670).
    if (scheme == QLatin1String("remote")) {
        if (!m_urlNavigator->isUrlEditable()) {
            m_urlNavigator->setUrlEditable(true);
        }
        m_urlNavigator->clearText();
        m_urlNavigator->setPlaceholderText(i18n("Enter server URL (e.g. smb://[ip address])"));
    } else {
        m_urlNavigator->setPlaceholderText(QString());
    }

    m_emptyTrashButton->setVisible(scheme == QLatin1String("trash"));
    // Looking up a service can be a bit slow: only when it is needed.
    m_networkFolderButton->setVisible(scheme == QLatin1String("remote") && KService::serviceByDesktopName(QStringLiteral("org.kde.knetattach")));
    m_kdeConnectButton->setVisible(scheme == QLatin1String("kdeconnect") && KService::serviceByDesktopName(QStringLiteral("org.kde.kdeconnect.app")));
}

#include "moc_whalelocationbar.cpp"
