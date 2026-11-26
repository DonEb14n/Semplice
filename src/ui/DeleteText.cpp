#include "ui/DeleteText.h"

#include <QCoreApplication>

namespace semplice {

namespace {
QString tr(const char* text, const char* disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("semplice::DeleteText", text, disambiguation, n);
}
}  // namespace

QString deleteTitle(int op)
{
    switch (op) {
    case 4: return tr("Delete track?");
    case 5: return tr("Delete album?");
    case 6: return tr("Delete artist?");
    case 7: return tr("Delete playlist?");
    default: return {};
    }
}

QString deleteText(const DeleteSummary& p)
{
    QStringList lines;
    switch (p.op) {
    case 4: lines << tr("The track will be removed from every album and playlist."); break;
    case 5: lines << tr("The album will be deleted."); break;
    case 6: lines << tr("The artist will be deleted."); break;
    case 7: lines << tr("The playlist will be deleted."); break;
    default: break;
    }
    if (p.op == 6 && p.albumsDeleted > 0)
        lines << tr("%n album(s) of this artist will be deleted.", nullptr, p.albumsDeleted);
    if (p.op == 6 && p.albumsRehomed > 0)
        lines << tr("%n album(s) shared with other artists will stay with them.", nullptr, p.albumsRehomed);
    if (p.op != 5 && p.op != 6 && p.albumsDeleted > 0)
        lines << tr("%n album(s) left without tracks will be deleted too.", nullptr, p.albumsDeleted);
    if (p.op == 5 && p.playlistsDeleted > 0)
        lines << tr("%n playlist(s) left without tracks will be deleted too.", nullptr, p.playlistsDeleted);
    if (p.tracksDeleted > 0)
        lines << tr("%n track(s) that exist nowhere else will be moved to the Recycle Bin.", nullptr, p.tracksDeleted);
    if (p.tracksKept > 0)
        lines << tr("%n track(s) are used elsewhere and stay in your collection.", nullptr, p.tracksKept);
    if (p.artistsHidden > 0)
        lines << tr("%n artist(s) are still named on tracks and become hidden.", nullptr, p.artistsHidden);
    if (p.artistsDeleted > 0 && p.op != 6)
        lines << tr("%n artist(s) with nothing left will be deleted.", nullptr, p.artistsDeleted);
    return lines.join(QLatin1Char(' '));
}

}  // namespace semplice
