/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicanvaspanel.h"

#include "whaleaicanvasmedia.h"
#include "whaleaicanvasmodel.h"
#include "whaleaiquickwidget.h"

#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>

#include <QDesktopServices>
#include <QIcon>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QTabBar>
#include <QTabWidget>
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
    m_tabs->tabBar()->setAutoHide(true);
    m_tabs->addTab(m_canvas, QIcon::fromTheme(QStringLiteral("view-preview")), i18nc("@title:tab the AI canvas", "Canvas"));

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
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
