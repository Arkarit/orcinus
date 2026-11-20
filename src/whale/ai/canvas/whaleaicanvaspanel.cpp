/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicanvaspanel.h"

#include "whaleaicanvasmedia.h"
#include "whaleaicanvasmodel.h"
#include "whaleaiquickwidget.h"
#include "whaleaitoolbackend.h"
#include "whaleaitooltab.h"

#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>
#include <KMessageBox>

#include <QDesktopServices>
#include <QDir>
#include <QIcon>
#include <QMenu>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
/** "image://icon/<name>": an icon of the theme, as the file list shows MIME types. */
class IconProvider : public QQuickImageProvider
{
public:
    IconProvider()
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
    {
    }
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        const int extent = requestedSize.isValid() ? std::max(requestedSize.width(), requestedSize.height()) : 22;
        QPixmap pixmap = QIcon::fromTheme(id, QIcon::fromTheme(QStringLiteral("unknown"))).pixmap(extent, extent);
        if (pixmap.isNull()) {
            // No icon theme (tests, minimal desktops): an empty space keeps the rows aligned.
            pixmap = QPixmap(extent, extent);
            pixmap.fill(Qt::transparent);
        }
        if (size) {
            *size = pixmap.size();
        }
        return pixmap;
    }
};

void registerTypes()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    qmlRegisterType<WhaleAiCanvasVideo>("org.kde.orcinus.canvas", 1, 0, "VideoOutput");
    qmlRegisterUncreatableType<WhaleAiCanvasPlayer>("org.kde.orcinus.canvas", 1, 0, "Player", QStringLiteral("Given by the canvas"));
}
}

WhaleAiCanvasPanel::WhaleAiCanvasPanel(QWidget *parent)
    : Panel(parent)
    , m_model(new WhaleAiCanvasModel(this))
    , m_player(new WhaleAiCanvasPlayer(this))
    , m_tabs(new QTabWidget(this))
    , m_canvas(new WhaleAiQuickWidget)
    , m_toolsButton(new QToolButton(this))
{
    Q_INIT_RESOURCE(whaleaicanvas);
    registerTypes();
    m_canvas->setObjectName(QStringLiteral("aiCanvas"));
    m_canvas->engine()->addImageProvider(QStringLiteral("icon"), new IconProvider);
    QQmlContext *context = m_canvas->rootContext();
    context->setContextProperty(QStringLiteral("canvasModel"), m_model);
    context->setContextProperty(QStringLiteral("canvasPlayer"), m_player);
    context->setContextProperty(QStringLiteral("canvasPanel"), this);
    context->setContextProperty(
        QStringLiteral("canvasTexts"),
        QVariantMap{
            {QStringLiteral("empty"),
             i18nc("@info placeholder of the AI canvas", "The AI agent shows its work here: images it made, files it found, sounds, videos, texts.")},
            {QStringLiteral("before"), i18nc("@label left side of a comparison", "Before")},
            {QStringLiteral("after"), i18nc("@label right side of a comparison", "After")},
            {QStringLiteral("open"), i18nc("@action:button open a file in another program", "Open")},
            {QStringLiteral("play"), i18nc("@action:button", "Play")},
            {QStringLiteral("pause"), i18nc("@action:button", "Pause")},
            {QStringLiteral("noPlayer"), i18nc("@info", "Orcinus was built without a player (libmpv).")},
            {QStringLiteral("noVideo"), i18nc("@info", "Videos cannot be shown here when Qt Quick does not use OpenGL; the sound still plays.")},
            {QStringLiteral("failed"), i18nc("@info", "Could not be played.")},
            // %1 is filled in by the QML.
            {QStringLiteral("newestIn"), i18nc("@info %1 is a folder", "Newest in %1", QStringLiteral("%1"))},
            {QStringLiteral("nothingYet"), i18nc("@info", "No image or video in this folder yet.")},
        });
    m_canvas->setSource(QUrl(QStringLiteral("qrc:/orcinus/ai/canvas/Canvas.qml")));

    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(false);
    m_tabs->addTab(m_canvas, QIcon::fromTheme(QStringLiteral("view-preview")), i18nc("@title:tab the AI canvas", "Canvas"));
    // The canvas itself stays.
    m_tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    m_tabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (auto tab = qobject_cast<WhaleAiToolTab *>(m_tabs->widget(index))) {
            closeTool(tab->id());
        }
    });
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs->tabBar(), &QWidget::customContextMenuRequested, this, &WhaleAiCanvasPanel::showTabMenu);

    // Tools (CV2): the ones in the tools folder, opened in tabs of their own.
    m_toolsButton->setObjectName(QStringLiteral("aiCanvasTools"));
    m_toolsButton->setIcon(QIcon::fromTheme(QStringLiteral("tools")));
    m_toolsButton->setToolTip(i18nc("@info:tooltip", "Tools"));
    m_toolsButton->setAutoRaise(true);
    m_toolsButton->setPopupMode(QToolButton::InstantPopup);
    auto toolsMenu = new QMenu(m_toolsButton);
    m_toolsButton->setMenu(toolsMenu);
    connect(toolsMenu, &QMenu::aboutToShow, this, &WhaleAiCanvasPanel::fillToolsMenu);
    m_tabs->setCornerWidget(m_toolsButton, Qt::TopRightCorner);
    m_confirmKeep = [this](const WhaleAiTool &tool, const QStringList &details) {
        return KMessageBox::questionTwoActionsList(this,
                                                   xi18nc("@info",
                                                          "<para>Keep the tool <emphasis>%1</emphasis> on the canvas?</para>"
                                                          "<para>It opens again with Orcinus, also without the AI agent. A tool "
                                                          "works with your files through Orcinus, so its changes can be undone, "
                                                          "but like any program it is not locked in: keep only tools you "
                                                          "trust.</para>",
                                                          tool.name),
                                                   details,
                                                   i18nc("@title:window", "Keep Tool"),
                                                   KGuiItem(i18nc("@action:button", "Keep Tool"), QStringLiteral("window-pin")),
                                                   KStandardGuiItem::cancel())
            == KMessageBox::PrimaryAction;
    };

    auto layout = new QVBoxLayout(this);
    // Some room to the dock's edges, as in the AI panel.
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_tabs);
    setFocusProxy(m_canvas);
}

WhaleAiCanvasPanel::~WhaleAiCanvasPanel()
{
    // The QML refers to the model and the player: it goes first.
    delete m_canvas;
}

WhaleAiCanvasModel *WhaleAiCanvasPanel::model() const
{
    return m_model;
}

WhaleAiCanvasPlayer *WhaleAiCanvasPanel::player() const
{
    return m_player;
}

WhaleAiQuickWidget *WhaleAiCanvasPanel::canvasWidget() const
{
    return m_canvas;
}

QStringList WhaleAiCanvasPanel::qmlErrors() const
{
    QStringList errors;
    for (const QQmlError &error : m_canvas->errors()) {
        errors.append(error.toString());
    }
    return errors;
}

void WhaleAiCanvasPanel::clear()
{
    m_player->stop();
    m_model->clear();
}

void WhaleAiCanvasPanel::activate(const QString &url)
{
    const QUrl target(url);
    if (target.isLocalFile()) {
        Q_EMIT localUrlActivated(target);
    }
}

void WhaleAiCanvasPanel::openExternally(const QString &url)
{
    auto job = new KIO::OpenUrlJob(QUrl(url));
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
    job->start();
}

void WhaleAiCanvasPanel::openLink(const QString &link)
{
    const QUrl url(link);
    if (url.isLocalFile()) {
        Q_EMIT localUrlActivated(url);
    } else if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")) {
        QDesktopServices::openUrl(url);
    }
}

// ---- tools ----

void WhaleAiCanvasPanel::setToolStore(const WhaleAiToolStore &store)
{
    m_store = store;
}

const WhaleAiToolStore &WhaleAiCanvasPanel::toolStore() const
{
    return m_store;
}

void WhaleAiCanvasPanel::setToolHost(const WhaleAiToolHost &host)
{
    m_host = host;
}

void WhaleAiCanvasPanel::setKeepConfirmation(const std::function<bool(const WhaleAiTool &, const QStringList &)> &confirm)
{
    m_confirmKeep = confirm;
}

WhaleAiToolTab *WhaleAiCanvasPanel::toolTab(const QString &id) const
{
    for (int i = 1; i < m_tabs->count(); ++i) {
        auto tab = qobject_cast<WhaleAiToolTab *>(m_tabs->widget(i));
        if (tab && tab->id() == id) {
            return tab;
        }
    }
    return nullptr;
}

QStringList WhaleAiCanvasPanel::openTools() const
{
    QStringList ids;
    for (int i = 1; i < m_tabs->count(); ++i) {
        if (auto tab = qobject_cast<WhaleAiToolTab *>(m_tabs->widget(i))) {
            ids.append(tab->id());
        }
    }
    return ids;
}

WhaleAiToolTab *WhaleAiCanvasPanel::addToolTab(const WhaleAiTool &tool)
{
    auto tab = new WhaleAiToolTab(tool, m_store, m_host);
    m_tabs->addTab(tab, QString());
    connect(tab, &WhaleAiToolTab::trustRequested, this, [this, tab] {
        if (keepTool(tab->id())) {
            tab->load();
        }
    });
    connect(tab, &WhaleAiToolTab::removeRequested, this, [this, tab] {
        const QString id = tab->id();
        unkeepTool(id);
        closeTool(id);
    });
    connect(tab, &WhaleAiToolTab::loaded, this, [this, tab] {
        updateToolTab(tab);
    });
    updateToolTab(tab);
    return tab;
}

void WhaleAiCanvasPanel::updateToolTab(WhaleAiToolTab *tab)
{
    const int index = m_tabs->indexOf(tab);
    if (index < 0) {
        return;
    }
    const WhaleAiTool &tool = tab->tool();
    const bool kept = m_store.pinned().contains(tool.id);
    m_tabs->setTabText(index, tool.name);
    m_tabs->setTabIcon(index, QIcon::fromTheme(tool.icon, QIcon::fromTheme(QStringLiteral("tools"))));
    m_tabs->setTabToolTip(index, kept ? i18nc("@info:tooltip %1 is a tool's description", "%1\nKept on the canvas", tool.description) : tool.description);
}

void WhaleAiCanvasPanel::restoreKeptTools()
{
    for (const QString &id : m_store.pinned()) {
        if (toolTab(id)) {
            continue;
        }
        const auto tool = m_store.tool(id);
        if (!tool) {
            continue;
        }
        WhaleAiToolTab *tab = addToolTab(*tool);
        if (!m_store.isTrusted(id)) {
            tab->block(i18nc("@info %1 is the name of a tool",
                             "“%1” changed since you kept it. It does not run until you have looked at it and keep it again.",
                             tool->name));
        }
    }
}

QString WhaleAiCanvasPanel::openTool(const QString &id)
{
    if (WhaleAiToolTab *tab = toolTab(id)) {
        m_tabs->setCurrentWidget(tab);
        return QString();
    }
    QString error;
    const auto tool = m_store.tool(id, &error);
    if (!tool) {
        return error;
    }
    WhaleAiToolTab *tab = addToolTab(*tool);
    // A kept tool whose files changed waits for the user, also when opened again.
    if (m_store.pinned().contains(id) && !m_store.isTrusted(id)) {
        tab->block(i18nc("@info %1 is the name of a tool",
                         "“%1” changed since you kept it. It does not run until you have looked at it and keep it again.",
                         tool->name));
    }
    m_tabs->setCurrentWidget(tab);
    return QString();
}

bool WhaleAiCanvasPanel::closeTool(const QString &id)
{
    WhaleAiToolTab *tab = toolTab(id);
    if (!tab) {
        return false;
    }
    m_tabs->removeTab(m_tabs->indexOf(tab));
    // The backend ends in the background; only quitting Orcinus waits for it.
    if (tab->backend()) {
        tab->backend()->stop();
    }
    delete tab;
    return true;
}

QStringList WhaleAiCanvasPanel::keepDetails(const WhaleAiTool &tool) const
{
    QStringList details;
    details.append(i18nc("@item %1 is a folder", "Folder: %1", tool.folder));
    for (const QString &file : m_store.files(tool.id)) {
        details.append(i18nc("@item a file of a tool", "File: %1", file));
    }
    if (!tool.backend.isEmpty()) {
        details.append(
            i18nc("@item %1 is a command", "Runs: %1", tool.backend.value(QLatin1String("command")).toVariant().toStringList().join(QLatin1Char(' '))));
    }
    for (const QString &target : tool.writes) {
        details.append(i18nc("@item %1 is where a tool writes, as the tool says", "Writes to: %1", target));
    }
    return details;
}

bool WhaleAiCanvasPanel::keepTool(const QString &id)
{
    const auto tool = m_store.tool(id);
    if (!tool || !m_confirmKeep || !m_confirmKeep(*tool, keepDetails(*tool))) {
        return false;
    }
    m_store.pin(id);
    if (WhaleAiToolTab *tab = toolTab(id)) {
        updateToolTab(tab);
    }
    return true;
}

void WhaleAiCanvasPanel::unkeepTool(const QString &id)
{
    m_store.unpin(id);
    if (WhaleAiToolTab *tab = toolTab(id)) {
        updateToolTab(tab);
    }
}

void WhaleAiCanvasPanel::contextChanged()
{
    for (int i = 1; i < m_tabs->count(); ++i) {
        if (auto tab = qobject_cast<WhaleAiToolTab *>(m_tabs->widget(i))) {
            tab->notifyContextChanged();
        }
    }
}

void WhaleAiCanvasPanel::fillToolsMenu()
{
    QMenu *menu = m_toolsButton->menu();
    menu->clear();
    const QStringList ids = m_store.ids();
    for (const QString &id : ids) {
        const auto tool = m_store.tool(id);
        QAction *action = menu->addAction(QIcon::fromTheme(tool->icon, QIcon::fromTheme(QStringLiteral("tools"))), tool->name);
        action->setToolTip(tool->description);
        connect(action, &QAction::triggered, this, [this, id] {
            openTool(id);
        });
    }
    if (ids.isEmpty()) {
        menu->addAction(i18nc("@item:inmenu", "No tools yet; ask the AI agent to make one"))->setEnabled(false);
    }
    menu->addSeparator();
    QAction *folder = menu->addAction(QIcon::fromTheme(QStringLiteral("folder-open")), i18nc("@action:inmenu", "Show Tools Folder"));
    connect(folder, &QAction::triggered, this, [this] {
        QDir().mkpath(m_store.root());
        Q_EMIT localUrlActivated(QUrl::fromLocalFile(m_store.root()));
    });
}

void WhaleAiCanvasPanel::showTabMenu(const QPoint &position)
{
    const int index = m_tabs->tabBar()->tabAt(position);
    auto tab = qobject_cast<WhaleAiToolTab *>(m_tabs->widget(index));
    if (!tab) {
        return;
    }
    const QString id = tab->id();
    QMenu menu(this);
    const bool kept = m_store.pinned().contains(id);
    QAction *keep = menu.addAction(QIcon::fromTheme(QStringLiteral("window-pin")), i18nc("@action:inmenu", "Keep on Canvas"));
    keep->setCheckable(true);
    keep->setChecked(kept);
    connect(keep, &QAction::triggered, this, [this, id, kept] {
        if (kept) {
            unkeepTool(id);
        } else {
            keepTool(id);
        }
    });
    connect(menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action:inmenu", "Reload")), &QAction::triggered, tab, [this, tab] {
        if (!tab->isBlocked()) {
            tab->load();
        }
    });
    connect(menu.addAction(QIcon::fromTheme(QStringLiteral("folder-open")), i18nc("@action:inmenu", "Show Folder")), &QAction::triggered, this, [this, tab] {
        Q_EMIT localUrlActivated(QUrl::fromLocalFile(tab->tool().folder));
    });
    menu.addSeparator();
    connect(menu.addAction(QIcon::fromTheme(QStringLiteral("tab-close")), i18nc("@action:inmenu", "Close")), &QAction::triggered, this, [this, id] {
        closeTool(id);
    });
    menu.exec(m_tabs->tabBar()->mapToGlobal(position));
}

bool WhaleAiCanvasPanel::urlChanged()
{
    return true;
}

void WhaleAiCanvasPanel::hideEvent(QHideEvent *event)
{
    // Nothing plays unseen.
    m_player->stop();
    Panel::hideEvent(event);
}

#include "moc_whaleaicanvaspanel.cpp"
