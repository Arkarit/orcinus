/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalefileoperationsimpl.h"

#include "dolphinviewbridge.h"
#include "whaleviewdebug.h"

#include <KDirModel>
#include <KFileItemListProperties>
#include <KGuiItem>
#include <KIO/ApplicationLauncherJob>
#include <KIO/DeleteOrTrashJob>
#include <KIO/Job>
#include <KIO/JobUiDelegateFactory>
#include <KIO/Paste>
#include <KIO/RenameFileDialog>
#include <KLocalizedString>
#include <KMessageDialog>
#include <KPropertiesDialog>
#include <KStandardGuiItem>
#include <KUrlMimeData>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDrag>
#include <QFileInfo>
#include <QIcon>
#include <QMimeData>
#include <QPointer>
#include <QWidget>

namespace
{
WhaleFileOperationsImpl::DragRunner &dragRunner()
{
    static WhaleFileOperationsImpl::DragRunner runner;
    return runner;
}

WhaleFileOperationsImpl::ConfirmationRunner &confirmationRunner()
{
    static WhaleFileOperationsImpl::ConfirmationRunner runner;
    return runner;
}

QPointer<QMimeData> &dragData()
{
    static QPointer<QMimeData> data;
    return data;
}
}

WhaleFileOperationsImpl::WhaleFileOperationsImpl(DolphinViewBridge *bridge, QObject *parent)
    : WhaleFileOperations(parent)
    , m_bridge(bridge)
{
}

WhaleFileOperationsImpl::~WhaleFileOperationsImpl() = default;

void WhaleFileOperationsImpl::setDragRunner(const DragRunner &runner)
{
    dragRunner() = runner;
}

const QMimeData *WhaleFileOperationsImpl::currentDragData()
{
    return dragData().data();
}

void WhaleFileOperationsImpl::setConfirmRemoval(bool confirm)
{
    m_confirmRemoval = confirm;
}

bool WhaleFileOperationsImpl::confirmRemoval() const
{
    return m_confirmRemoval;
}

void WhaleFileOperationsImpl::setConfirmationRunner(const ConfirmationRunner &runner)
{
    confirmationRunner() = runner;
}

Qt::DropAction WhaleFileOperationsImpl::startDrag(const QList<QUrl> &urls, const QPixmap &pixmap)
{
    QMimeData *data = m_bridge->createDragMimeData(urls);
    if (!data) {
        return Qt::IgnoreAction;
    }

    // As KItemListController::startDragging(): the drag belongs to the active window.
    QObject *source = QApplication::activeWindow();
    auto drag = new QDrag(source ? source : this);
    drag->setMimeData(data);
    dragData() = data; // the drag owns it
    QPixmap image = pixmap;
    if (image.isNull()) {
        const QList<QUrl> dragged = KUrlMimeData::urlsFromMimeData(data, KUrlMimeData::PreferKdeUrls);
        const KFileItem first = dragged.isEmpty() ? KFileItem() : m_bridge->item(dragged.first());
        image = QIcon::fromTheme(first.isNull() ? QStringLiteral("unknown") : first.iconName()).pixmap(32);
    }
    drag->setPixmap(image);
    drag->setHotSpot(QPoint(image.width() / image.devicePixelRatio() / 2, 0));

    if (const DragRunner &runner = dragRunner()) {
        const Qt::DropAction action = runner(drag);
        drag->deleteLater();
        return action;
    }
    // QDrag deletes itself when the drag has ended.
    return drag->exec(Qt::MoveAction | Qt::CopyAction | Qt::LinkAction, Qt::CopyAction);
}

bool WhaleFileOperationsImpl::updateDropAction(QDragMoveEvent *event, const QUrl &url)
{
    return m_bridge->updateDropAction(event, url);
}

bool WhaleFileOperationsImpl::drop(QDropEvent *event, const QUrl &url, QWidget *widget)
{
    return m_bridge->drop(event, url, widget);
}

namespace
{
bool existsLocally(const QUrl &url)
{
    // A broken symbolic link exists too: it can be renamed, trashed and deleted.
    const QFileInfo info(url.toLocalFile());
    return info.exists() || info.isSymLink();
}
}

KFileItemList WhaleFileOperationsImpl::itemsFor(const QList<QUrl> &urls) const
{
    KFileItemList items;
    items.reserve(urls.count());
    for (const QUrl &url : urls) {
        const KFileItem listed = m_bridge->item(url);
        // Not listed, e.g. deep below the location: KFileItem reads a local one from the file system.
        items.append(listed.isNull() ? KFileItem(url) : listed);
    }
    return items;
}

bool WhaleFileOperationsImpl::checkExisting(const QList<QUrl> &urls) const
{
    for (const QUrl &url : urls) {
        if (url.isLocalFile() && !existsLocally(url)) {
            // KIO's own wording, as for an item that disappears during a job.
            m_bridge->showErrorMessage(KIO::buildErrorString(KIO::ERR_DOES_NOT_EXIST, url.toLocalFile()), KIO::ERR_DOES_NOT_EXIST);
            return false;
        }
    }
    return true;
}

bool WhaleFileOperationsImpl::removesLocation(const QList<QUrl> &urls) const
{
    const QUrl location = m_bridge->url().adjusted(QUrl::StripTrailingSlash);
    return std::any_of(urls.cbegin(), urls.cend(), [&location](const QUrl &url) {
        const QUrl item = url.adjusted(QUrl::StripTrailingSlash);
        return item == location || item.isParentOf(location);
    });
}

WhaleFileOperations::Operations WhaleFileOperationsImpl::supportedOperations(const QList<QUrl> &urls) const
{
    Operations operations;
    if (urls.isEmpty() || std::any_of(urls.cbegin(), urls.cend(), [](const QUrl &url) {
            return !url.isValid();
        })) {
        return operations;
    }
    if (urls.count() == 1) {
        const QUrl folder = KIO::upUrl(urls.first());
        if (!folder.isLocalFile() || QFileInfo(folder.toLocalFile()).isDir()) {
            operations |= Operation::ShowInFolder;
        }
    }
    const bool missing = std::any_of(urls.cbegin(), urls.cend(), [](const QUrl &url) {
        return url.isLocalFile() && !existsLocally(url);
    });
    if (missing) {
        return operations;
    }

    // The rules of DolphinMainWindow::updateFileAndEditActions() for a selection.
    operations |= Operation::Open | Operation::OpenWith | Operation::Properties | Operation::Copy | Operation::CopyLocation;
    const KFileItemListProperties properties(itemsFor(urls));
    if (!removesLocation(urls)) {
        if (properties.supportsMoving()) {
            operations |= Operation::Rename | Operation::Cut;
            if (properties.isLocal()) {
                operations |= Operation::Trash;
            }
        }
        if (properties.supportsDeleting()) {
            operations |= Operation::Delete;
        }
        // As the file list's Duplicate (it creates the copies next to the items).
        if (properties.supportsWriting()) {
            operations |= Operation::Duplicate;
        }
    }
    return operations;
}

void WhaleFileOperationsImpl::openItems(const QList<QUrl> &urls)
{
    if (!urls.isEmpty() && checkExisting(urls)) {
        m_bridge->openItems(itemsFor(urls));
    }
}

void WhaleFileOperationsImpl::openItemsWith(const QList<QUrl> &urls)
{
    if (urls.isEmpty() || !checkExisting(urls)) {
        return;
    }
    // KFileItemActions' "Other Application...": a launcher job without an application
    // asks the user in KDE's "Open With" dialog.
    auto job = new KIO::ApplicationLauncherJob();
    job->setUrls(urls);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, m_bridge->operationsWidget()));
    job->start();
}

void WhaleFileOperationsImpl::showItemInFolder(const QUrl &url)
{
    m_bridge->showItemInFolder(url);
}

void WhaleFileOperationsImpl::showProperties(const QList<QUrl> &urls)
{
    if (urls.isEmpty() || !checkExisting(urls)) {
        return;
    }
    // As DolphinViewActionHandler::slotProperties().
    auto dialog = new KPropertiesDialog(itemsFor(urls), m_bridge->operationsWidget());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void WhaleFileOperationsImpl::renameItems(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    if (removesLocation(urls)) {
        qCWarning(WhaleViewDebug) << "Refusing to rename the location of the view area or a folder above it:" << urls;
        Q_EMIT operationFinished(Operation::Rename, urls, Result::Failed, {});
        return;
    }
    if (!checkExisting(urls)) {
        Q_EMIT operationFinished(Operation::Rename, urls, Result::Failed, {});
        return;
    }
    // The dialog path of DolphinView::renameSelectedItems().
    auto dialog = new KIO::RenameFileDialog(itemsFor(urls), m_bridge->operationsWidget());
    connect(dialog, &KIO::RenameFileDialog::renamingFinished, this, [this, urls](const QList<QUrl> &newUrls) {
        Q_EMIT operationFinished(Operation::Rename, urls, Result::Done, newUrls);
    });
    connect(dialog, &KIO::RenameFileDialog::error, this, [this, urls]() {
        // The job has shown its error already.
        Q_EMIT operationFinished(Operation::Rename, urls, Result::Failed, {});
    });
    connect(dialog, &QDialog::rejected, this, [this, urls]() {
        Q_EMIT operationFinished(Operation::Rename, urls, Result::Canceled, {});
    });
    dialog->open();
}

void WhaleFileOperationsImpl::trashItems(const QList<QUrl> &urls)
{
    removeItems(Operation::Trash, urls);
}

void WhaleFileOperationsImpl::deleteItems(const QList<QUrl> &urls)
{
    removeItems(Operation::Delete, urls);
}

void WhaleFileOperationsImpl::removeItems(Operation operation, const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    if (removesLocation(urls)) {
        qCWarning(WhaleViewDebug) << "Refusing to remove the location of the view area or a folder above it:" << urls;
        Q_EMIT operationFinished(operation, urls, Result::Failed, {});
        return;
    }
    if (!checkExisting(urls)) {
        Q_EMIT operationFinished(operation, urls, Result::Failed, {});
        return;
    }

    // As DolphinView::trashSelectedItems() and deleteSelectedItems(): the user's
    // confirmation, undo for the trash, warnings by KIO, errors in the view area.
    using Iface = KIO::AskUserActionInterface;
    const bool trash = operation == Operation::Trash;
    QWidget *widget = m_bridge->operationsWidget();
    Iface::ConfirmationType confirmation = m_confirmRemoval ? Iface::ForceConfirmation : Iface::DefaultConfirmation;
    if (m_confirmRemoval && confirmationRunner()) {
        if (!confirmationRunner()(operation, urls)) {
            Q_EMIT operationFinished(operation, urls, Result::Canceled, {});
            return;
        }
        confirmation = Iface::DefaultConfirmation;
    }
    // The job belongs to the view area, not to this view: switching views does not abort it.
    auto job = new KIO::DeleteOrTrashJob(KDirModel::simplifiedUrlList(urls), trash ? Iface::Trash : Iface::Delete, confirmation, widget);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoWarningHandlingEnabled, widget));
    connect(job, &KJob::result, m_bridge, [bridge = m_bridge, trash](KJob *job) {
        if (job->error() == 0) {
            bridge->showOperationCompletedMessage(trash ? i18nc("@info:status", "Trash operation completed.")
                                                        : i18nc("@info:status", "Delete operation completed."));
        } else if (job->error() != KIO::ERR_USER_CANCELED) {
            bridge->showErrorMessage(job->errorString(), job->error());
        }
    });
    connect(job, &KJob::result, this, [this, operation, urls](KJob *job) {
        const Result result = job->error() == 0 ? Result::Done : job->error() == KIO::ERR_USER_CANCELED ? Result::Canceled : Result::Failed;
        Q_EMIT operationFinished(operation, urls, result, {});
    });
    job->start();
}

void WhaleFileOperationsImpl::copyItems(const QList<QUrl> &urls)
{
    if (!urls.isEmpty() && checkExisting(urls)) {
        putOnClipboard(urls, false);
    }
}

void WhaleFileOperationsImpl::cutItems(const QList<QUrl> &urls)
{
    if (urls.isEmpty() || !checkExisting(urls)) {
        return;
    }
    if (removesLocation(urls)) {
        qCWarning(WhaleViewDebug) << "Refusing to cut the location of the view area or a folder above it:" << urls;
        return;
    }
    if (!m_confirmRemoval) {
        putOnClipboard(urls, true);
    } else if (confirmationRunner()) {
        if (confirmationRunner()(Operation::Cut, urls)) {
            putOnClipboard(urls, true);
        }
    } else {
        confirmCut(urls);
    }
}

void WhaleFileOperationsImpl::confirmCut(const QList<QUrl> &urls)
{
    // As KIO's confirmation of the trash: the items listed, here by their path below the
    // location; not modal, so the window stays responsive.
    const QString location = m_bridge->url().isLocalFile() ? m_bridge->url().toLocalFile() : QString();
    QStringList names;
    for (const QUrl &url : urls) {
        names.append(url.isLocalFile() && !location.isEmpty() ? QDir(location).relativeFilePath(url.toLocalFile())
                                                              : url.toDisplayString(QUrl::PreferLocalFile));
    }
    auto dialog = new KMessageDialog(KMessageDialog::QuestionTwoActions,
                                     i18ncp("@info",
                                            "Do you really want to cut this item? It is moved when you paste it.",
                                            "Do you really want to cut these %1 items? They are moved when you paste them.",
                                            urls.count()),
                                     m_bridge->operationsWidget());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setCaption(i18nc("@title:window", "Cut"));
    dialog->setListWidgetItems(names);
    dialog->setButtons(KGuiItem(i18nc("@action:button", "Cut"), QStringLiteral("edit-cut")), KStandardGuiItem::cancel());
    connect(dialog, &QDialog::finished, this, [this, urls](int result) {
        if (result == KMessageDialog::PrimaryAction) {
            putOnClipboard(urls, true);
        }
    });
    dialog->open();
}

void WhaleFileOperationsImpl::putOnClipboard(const QList<QUrl> &urls, bool cut) const
{
    // The data of DolphinView::copySelectedItemsToClipboard() and cutSelectedItemsToClipboard()
    // (KFileItemModel::createMimeData(): the URLs and their most local URLs).
    QList<QUrl> itemUrls;
    QList<QUrl> mostLocalUrls;
    const KFileItemList items = itemsFor(KDirModel::simplifiedUrlList(urls));
    for (const KFileItem &item : items) {
        itemUrls.append(item.url());
        mostLocalUrls.append(item.mostLocalUrl());
    }
    auto mimeData = new QMimeData();
    KUrlMimeData::setUrls(itemUrls, mostLocalUrls, mimeData);
    if (cut) {
        KIO::setClipboardDataCut(mimeData, true);
    }
    KUrlMimeData::exportUrlsToPortal(mimeData);
    QApplication::clipboard()->setMimeData(mimeData);
}

void WhaleFileOperationsImpl::copyLocations(const QList<QUrl> &urls)
{
    if (urls.isEmpty() || !checkExisting(urls)) {
        return;
    }
    // As DolphinView::copyPathToClipboard(), one line per item.
    QStringList paths;
    const KFileItemList items = itemsFor(urls);
    for (const KFileItem &item : items) {
        const QString path = item.localPath();
        paths.append(path.isEmpty() ? item.url().toDisplayString() : QDir::toNativeSeparators(path));
    }
    QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
}

void WhaleFileOperationsImpl::duplicateItems(const QList<QUrl> &urls)
{
    if (urls.isEmpty() || !checkExisting(urls)) {
        return;
    }
    if (removesLocation(urls)) {
        qCWarning(WhaleViewDebug) << "Refusing to duplicate the location of the view area or a folder above it:" << urls;
        return;
    }
    m_bridge->duplicateItems(itemsFor(urls));
}

#include "moc_whalefileoperationsimpl.cpp"
