#include "ui/BannerVm.h"

#include <QCoreApplication>

namespace semplice {

// Every banner has a short title and one sentence (Figma "Middle window all conditions").
QString BannerVm::titleFor(const Banner& b)
{
    if (b.key == QLatin1String("tracks_skipped") || b.key == QLatin1String("cannot_play"))
        return tr("Playback error");
    if (b.key == QLatin1String("folder_not_empty") || b.key == QLatin1String("folder_missing"))
        return tr("Wrong folder");
    if (b.key == QLatin1String("schema_too_new") || b.key == QLatin1String("library_unusable"))
        return tr("Database error");
    if (b.key == QLatin1String("library_repaired"))
        return tr("Library");
    if (b.key == QLatin1String("rescan_done") || b.key == QLatin1String("rescan_blocked"))
        return tr("Rescan");
    if (b.key == QLatin1String("already_exists") || b.key == QLatin1String("not_allowed"))
        return tr("Error adding");
    if (b.key == QLatin1String("error_editing"))
        return tr("Error editing");
    if (b.key.startsWith(QLatin1String("share_")))
        return tr("Share");
    return tr("Error %1").arg(b.detail.isEmpty() ? QStringLiteral("-1") : b.detail);  // the "Error -1" banner
}

QString BannerVm::textFor(const Banner& b)
{
    if (b.key == QLatin1String("tracks_skipped"))
        return tr("%n track(s) could not be played and were skipped.", nullptr, b.count);
    if (b.key == QLatin1String("folder_not_empty"))
        return tr("The selected directory is not empty.");
    if (b.key == QLatin1String("folder_missing"))
        return tr("This folder does not exist any more.");
    if (b.key == QLatin1String("schema_too_new"))
        return tr("This library was created by a newer version of Semplice. Please update the app.");
    if (b.key == QLatin1String("library_unusable"))
        return tr("The library could not be opened. Error %1").arg(b.detail);
    if (b.key == QLatin1String("library_repaired"))
        return tr("An operation that was interrupted last time has been completed.");
    if (b.key == QLatin1String("cannot_play"))
        return tr("This item could not be played.");
    if (b.key == QLatin1String("rescan_done"))
        return b.count > 0 ? tr("%n new file(s) were found and added to a new playlist.", nullptr, b.count) : tr("The library is up to date.");
    if (b.key == QLatin1String("share_imported"))
        return tr("%n track(s) were added from the shared file.", nullptr, b.count);
    if (b.key == QLatin1String("share_nothing_new"))
        return tr("Everything in this file is already in your library.");
    if (b.key == QLatin1String("share_exported"))
        return tr("The file was saved.");
    if (b.key == QLatin1String("share_export_failed"))
        return tr("The file could not be created. Error %1").arg(b.detail);
    if (b.key == QLatin1String("share_import_failed"))
        return tr("This file could not be imported. It may be damaged or made by a newer version. Error %1").arg(b.detail);
    if (b.key == QLatin1String("rescan_blocked"))
        return tr("Many files are missing, so nothing was removed. Is the drive connected?");
    if (b.key == QLatin1String("already_exists"))
        return tr("Unable to add the artist/album/playlist. It already exists.");
    if (b.key == QLatin1String("not_allowed"))
        return tr("This operation is not possible: the track exists nowhere else.");
    if (b.key == QLatin1String("error_editing"))
        return tr("Something went wrong when you tried to edit this artist/playlist/album. Most likely, some fields are filled in incorrectly or not filled in at all.");
    return tr("Something went wrong.");
}

QVariant BannerVm::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const Banner& b = m_queue.banners().at(index.row());
    switch (role) {
    case IdRole: return b.id;
    case TitleRole: return titleFor(b);
    case TextRole: return textFor(b);
    case IsErrorRole: return b.kind == BannerKind::Error;
    }
    return {};
}

void BannerVm::push(BannerKind kind, const QString& key, int count, const QString& detail)
{
    const int before = rowCount();
    m_queue.push(kind, key, count, detail);
    if (before == BannerQueue::kMaxVisible) {  // the oldest went away
        beginResetModel();
        endResetModel();
    } else {
        beginInsertRows({}, before, before);
        endInsertRows();
    }
}

void BannerVm::info(const QString& key, int count, const QString& detail)
{
    push(BannerKind::Info, key, count, detail);
}

void BannerVm::error(const QString& key, int count, const QString& detail)
{
    push(BannerKind::Error, key, count, detail);
}

void BannerVm::dismiss(int id)
{
    for (int i = 0; i < rowCount(); ++i) {
        if (m_queue.banners().at(i).id == id) {
            beginRemoveRows({}, i, i);
            m_queue.dismiss(id);
            endRemoveRows();
            return;
        }
    }
}

}  // namespace semplice
