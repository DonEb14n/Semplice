#include "ui/CardVms.h"

#include "core/util/Logger.h"
#include <QCoreApplication>
#include <QDate>
#include <QLocale>
#include <QUrl>

namespace semplice {

namespace {
QString tr(const char* text, int n = -1)
{
    return QCoreApplication::translate("semplice::CardVms", text, nullptr, n);
}
}  // namespace

QString clockText(qint64 ms)
{
    const qint64 s = ms / 1000;
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString albumTypeName(int type)
{
    switch (type) {
    case 0: return tr("Single");
    case 1: return tr("Album");
    case 2: return tr("EP");
    case 3: return tr("Live");
    case 4: return tr("Compilation");
    default: return tr("Other");
    }
}

QString durationWords(qint64 durationMs)
{
    const qint64 seconds = durationMs / 1000;
    if (seconds >= 3600)
        return tr("%1 h %2 min").arg(seconds / 3600).arg((seconds % 3600) / 60);
    return tr("%1 min %2 sec").arg(seconds / 60).arg(seconds % 60);
}

QString releaseDateText(const ReleaseDate& d, const QString& language)
{
    if (d.year <= 0)
        return {};
    const QLocale locale(language);
    if (d.month > 0 && d.day > 0)
        return locale.toString(QDate(d.year, d.month, d.day), QStringLiteral("d MMMM yyyy"));
    if (d.month > 0)
        return locale.toString(QDate(d.year, d.month, 1), QStringLiteral("MMMM yyyy"));
    return QString::number(d.year);
}

QString sectionTitle(ArtistSection s)
{
    switch (s) {
    case ArtistSection::Discography: return tr("Discography");
    case ArtistSection::Albums: return tr("Albums");
    case ArtistSection::Singles: return tr("Singles");
    case ArtistSection::Eps: return tr("EP's");
    case ArtistSection::Live: return tr("Live");
    case ArtistSection::Compilations: return tr("Compilations");
    case ArtistSection::Other: return tr("Other");
    case ArtistSection::Collaborations: return tr("Collaborations");
    case ArtistSection::Feats: return tr("Feat's");
    }
    return {};
}

QString coverUrlFor(const QString&, const QString& relativeCover)
{
    return relativeCover.isEmpty() ? QString() : QStringLiteral("image://cover/") + QString::fromLatin1(QUrl::toPercentEncoding(relativeCover, "/"));
}

QStringList genreKeysOnly(const QStringList& keys)
{
    QStringList out;
    for (const QString& k : keys)
        if (GenreVocabulary::isGenreKey(k))
            out << k;
    return out;
}

QStringList tagDisplayNames(const QStringList& keys, const GenreVocabulary* genres, const QString& language)
{
    QStringList out;
    for (const QString& k : keys)
        out << (genres ? genres->name(k, language) : k);
    return out;
}

// ------------------------------------------------------------------------------------------ models

void TrackTableModel::setRows(QList<TrackRow> rows, const QString& libraryRoot)
{
    beginResetModel();
    m_rows = std::move(rows);
    m_root = libraryRoot;
    endResetModel();
    emit countChanged();
}

QVariant TrackTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const TrackRow& r = m_rows.at(index.row());
    switch (role) {
    case TrackIdRole: return r.id;
    case PositionRole: return r.position;
    case Qt::DisplayRole:
    case TitleRole: return r.title;
    case ArtistRole: return r.artistsText();
    case ArtistIdRole: return r.artists.isEmpty() ? 0 : r.artists.first().id;
    case AlbumRole: return r.albumTitle;
    case AlbumIdRole: return r.albumId;
    case DurationRole: return clockText(r.durationMs);
    case CoverRole:
        if (r.hasEmbeddedCover && !m_root.isEmpty())
            return QStringLiteral("image://cover/file?embedded=1&path=") +
                   QString::fromLatin1(QUrl::toPercentEncoding(m_root + QLatin1Char('/') + r.filePath));
        return coverUrlFor(m_root, r.coverPath);
    case CanRemoveRole: return r.memberships > 1;
    case ArtistsRole: {
        QVariantList list;
        for (const ArtistRef& a : r.artists)
            list.append(QVariantMap{{QStringLiteral("id"), a.id}, {QStringLiteral("name"), a.name}});
        return list;
    }
    case InPlaylistRole: return r.inPlaylist;
    }
    return {};
}

QHash<int, QByteArray> TrackTableModel::roleNames() const
{
    return {{TrackIdRole, "trackId"}, {PositionRole, "position"}, {TitleRole, "title"}, {ArtistRole, "artist"},
            {ArtistIdRole, "artistId"}, {AlbumRole, "albumTitle"}, {AlbumIdRole, "albumId"}, {DurationRole, "duration"},
            {CoverRole, "cover"}, {CanRemoveRole, "canRemove"}, {InPlaylistRole, "inPlaylist"}, {ArtistsRole, "artists"}};
}

void ArtistRefModel::setRows(QList<ArtistRef> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

QVariant ArtistRefModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const ArtistRef& r = m_rows.at(index.row());
    switch (role) {
    case IdRole: return r.id;
    case Qt::DisplayRole:
    case NameRole: return r.name;
    case LetterRole: return r.name.trimmed().left(1).toUpper();
    case AvatarRole: return coverUrlFor({}, r.avatar);
    case HiddenRole: return r.hidden;
    }
    return {};
}

QHash<int, QByteArray> ArtistRefModel::roleNames() const
{
    return {{IdRole, "artistId"}, {NameRole, "name"}, {LetterRole, "letter"}, {AvatarRole, "avatar"}, {HiddenRole, "hidden"}};
}

// ------------------------------------------------------------------------------------------ artist card

void ArtistCardVm::open(qint64 id)
{
    if (id == m_id && m_ready)
        return;
    m_id = id;
    m_ready = false;
    emit changed();
    emit requested(id);
}

void ArtistCardVm::reset()
{
    m_id = 0;
    m_ready = false;
    m_sections.clear();
    emit changed();
}

void ArtistCardVm::setCard(const ArtistCard& card)
{
    if (card.artist.id != m_id)
        return;  // an answer for another page
    m_name = card.artist.name;
    m_avatar = coverUrlFor(m_root, card.artist.avatar);
    m_hidden = card.artist.hidden;
    m_userHidden = card.artist.userHidden;
    m_tags = tagDisplayNames(card.tags, m_genres, m_language);
    QStringList parts;
    auto add = [&](int n, const QString& label) {
        if (n > 0)
            parts << QStringLiteral("%1 %2").arg(label).arg(n);
    };
    add(card.albums, tr("Albums"));
    add(card.singles, tr("Singles"));
    add(card.eps, tr("EP's"));
    add(card.lives, tr("Live"));
    add(card.compilations, tr("Compilations"));
    add(card.others, tr("Other"));
    m_stats = card.artist.hidden ? QString() : parts.join(QStringLiteral(" | "));

    m_sections.clear();
    for (const ArtistCardSection& s : card.sections) {
        QList<ListRow> rows;
        for (const CarouselItem& item : s.items) {
            ListRow row = toListRow(item);
            if (row.title.isEmpty())
                row.title = tr("Unsorted tracks");
            if (s.section == ArtistSection::Discography)
                row.subtitle = QStringLiteral("%1 | %2").arg(albumTypeName(item.albumType)).arg(item.year);
            rows.append(row);
        }
        m_sections.append(CardQueries::sectionKey(ArtistId(card.artist.id), s.section), sectionTitle(s.section), rows);
    }
    m_ready = true;
    emit changed();
}

// ------------------------------------------------------------------------------------------ album card

void AlbumCardVm::open(qint64 id)
{
    if (id == m_id && m_ready)
        return;
    m_id = id;
    m_ready = false;
    emit changed();
    emit requested(id);
}

void AlbumCardVm::reset()
{
    m_id = 0;
    m_ready = false;
    m_tracks.setRows({}, m_root);
    emit changed();
}

void AlbumCardVm::setCard(const AlbumCard& card)
{
    if (card.id != m_id)
        return;
    m_title = card.isUnsorted ? tr("Unsorted tracks") : card.title;
    m_type = albumTypeName(static_cast<int>(card.type));
    m_cover = coverUrlFor(m_root, card.cover);
    m_tags = tagDisplayNames(genreKeysOnly(card.tags), m_genres, m_language);   // the card shows genres only
    m_date = releaseDateText(card.date, m_language);
    {   // "30 November 2020 | Artist #1, Artist #2": the authors follow the date as plain text
        QStringList names;
        for (const ArtistRef& a : card.artists)
            names << a.name;
        if (!names.isEmpty())
            m_date = m_date.isEmpty() ? names.join(QStringLiteral(", ")) : m_date + QStringLiteral(" | ") + names.join(QStringLiteral(", "));
    }
    QString year = card.date.year > 0 ? QString::number(card.date.year)
                                      : QString::number(QDateTime::fromMSecsSinceEpoch(card.createdAt).date().year());
    m_stats = QStringLiteral("%1, %2 | %3").arg(tr("%n song(s)", nullptr, static_cast<int>(card.tracks.size())), durationWords(card.durationMs), year);
    {
        SEMPLICE_SLOW_SCOPE("AlbumCardVm::setCard rows");
        m_artists.setRows(card.artists);
        m_tracks.setRows(card.tracks, m_root);
    }
    m_ready = true;
    SEMPLICE_SLOW_SCOPE("AlbumCardVm::setCard changed()");
    emit changed();
}

// ------------------------------------------------------------------------------------------ playlist card

void PlaylistCardVm::open(qint64 id)
{
    if (id == m_id && m_ready)
        return;
    m_id = id;
    m_ready = false;
    emit changed();
    emit requested(id);
}

void PlaylistCardVm::reset()
{
    m_id = 0;
    m_ready = false;
    m_tracks.setRows({}, m_root);
    emit changed();
}

void PlaylistCardVm::setCard(const PlaylistCard& card)
{
    if (card.id != m_id)
        return;
    m_name = card.name;
    m_cover = coverUrlFor(m_root, card.cover);
    m_stats = QStringLiteral("%1, %2").arg(tr("%n song(s)", nullptr, static_cast<int>(card.tracks.size())), durationWords(card.durationMs));
    m_tracks.setRows(card.tracks, m_root);
    m_ready = true;
    emit changed();
}

}  // namespace semplice
