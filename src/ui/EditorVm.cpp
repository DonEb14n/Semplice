#include "ui/EditorVm.h"

#include "core/util/Logger.h"
#include "core/domain/ReleaseDate.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <algorithm>

namespace semplice {

namespace {
QString embeddedPictureUrl(const QString& audioFile)
{
    return QStringLiteral("image://cover/file?embedded=1&path=") + QString::fromLatin1(QUrl::toPercentEncoding(audioFile));
}
QString letterOf(const QString& name)
{
    return name.trimmed().left(1).toUpper();
}
QString clock(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
}  // namespace

// ------------------------------------------------------------------------------------------ models

QVariant CreditModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const DraftCredit& c = m_rows.at(index.row());
    switch (role) {
    case IdRole: return c.id;
    case Qt::DisplayRole:
    case NameRole: return c.name;
    case LetterRole: return letterOf(c.name);
    case RoleRole: return c.role;
    case HiddenRole: return c.hidden;
    case AvatarRole: return c.avatar;
    }
    return {};
}

QHash<int, QByteArray> CreditModel::roleNames() const
{
    return {{IdRole, "artistId"}, {NameRole, "name"}, {LetterRole, "letter"}, {RoleRole, "role"}, {HiddenRole, "hidden"}, {AvatarRole, "avatar"}};
}

void CreditModel::setRows(QList<DraftCredit> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

bool CreditModel::add(const DraftCredit& credit)
{
    for (const DraftCredit& c : std::as_const(m_rows))
        if ((credit.id > 0 && c.id == credit.id) || c.name.compare(credit.name, Qt::CaseInsensitive) == 0)
            return false;
    beginInsertRows({}, rowCount(), rowCount());
    m_rows.append(credit);
    endInsertRows();
    emit countChanged();
    return true;
}

void CreditModel::removeAt(int row)
{
    if (row < 0 || row >= rowCount())
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

QVariant EditRowModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const EditRow& r = m_rows.at(index.row());
    switch (role) {
    case ScanIndexRole: return r.scanIndex;
    case TrackIdRole: return r.trackId;
    case Qt::DisplayRole:
    case TitleRole: return r.title;
    case ArtistRole: return r.artistsText;
    case DurationRole: return clock(r.durationMs);
    case CoverRole: return r.cover;
    case IsNewRole: return r.scanIndex >= 0;
    case DuplicateRole: return r.duplicate;
    case PositionRole: return index.row() + 1;
    case CanRemoveRole: return r.canRemove;
    }
    return {};
}

QHash<int, QByteArray> EditRowModel::roleNames() const
{
    return {{ScanIndexRole, "scanIndex"}, {TrackIdRole, "trackId"}, {TitleRole, "title"}, {ArtistRole, "artist"},
            {DurationRole, "duration"},   {CoverRole, "cover"},     {IsNewRole, "isNew"}, {DuplicateRole, "duplicate"},
            {PositionRole, "position"}, {CanRemoveRole, "canRemove"}};
}

void EditRowModel::reset(QList<EditRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

void EditRowModel::append(const QList<EditRow>& rows)
{
    if (rows.isEmpty())
        return;
    beginInsertRows({}, rowCount(), rowCount() + static_cast<int>(rows.size()) - 1);
    m_rows += rows;
    endInsertRows();
    emit countChanged();
}

void EditRowModel::removeAt(int row)
{
    if (row < 0 || row >= rowCount())
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

void EditRowModel::setTitleAt(int row, const QString& title)
{
    if (row < 0 || row >= rowCount())
        return;
    m_rows[row].title = title;
    emit dataChanged(index(row), index(row), {TitleRole});
}

// ------------------------------------------------------------------------------------------ the editor

EditorVm::EditorVm(const GenreVocabulary* genres, const QString& language, QObject* parent)
    : QObject(parent), m_genres(genres), m_language(language)
{
    connect(&m_authors, &CreditModel::countChanged, this, &EditorVm::changed);
    connect(&m_rows, &EditRowModel::countChanged, this, &EditorVm::changed);
}

void EditorVm::clear()
{
    m_entity.clear();
    m_editing = false;
    m_id = 0;
    m_loading = false;
    m_rowsOnly = false;  // a refresh asked for by the old window must not turn the next first load into "rows only"
    m_scanned = false;
    m_preselected = 0;
    m_title.clear();
    m_originalTitle.clear();
    m_currentPicture.clear();
    m_newPicture.clear();
    m_removePicture = false;
    m_albumType = 1;
    m_typeTouched = false;
    m_year = m_month = m_day = 0;
    m_tagKeys.clear();
    m_hidden = false;
    m_userHidden = m_originalUserHidden = false;
    m_skipped = 0;
    m_pendingTrack = 0;
    m_busy = false;
    m_authors.setRows({});
    m_rows.reset({});
    m_albums.setRows({});
    emit busyChanged();
    emit changed();
}

void EditorVm::beginAdd(const QString& entity, const QString& prefillName, qint64 preselectedArtist)
{
    clear();
    m_entity = entity;
    m_title = prefillName;
    m_preselected = preselectedArtist;
    emit changed();
}

void EditorVm::beginAddWithTrack(qint64 trackId, const QString& name)
{
    beginAdd(QStringLiteral("playlist"), name, 0);
    m_pendingTrack = trackId;
    emit loadRequested(QStringLiteral("trackrow"), trackId);
}

void EditorVm::beginEdit(const QString& entity, qint64 id)
{
    clear();
    m_entity = entity;
    m_editing = true;
    m_id = id;
    m_loading = true;
    emit changed();
    emit loadRequested(entity, id);
}

QString EditorVm::picture() const
{
    if (m_removePicture)
        return {};
    if (!m_newPicture.isEmpty())
        return QUrl::fromLocalFile(m_newPicture).toString();
    return m_currentPicture;
}

void EditorVm::setTitle(const QString& text)
{
    if (text == m_title)
        return;
    m_title = text;
    emit changed();
}

void EditorVm::setAlbumType(int type)
{
    m_typeTouched = true;
    if (type == m_albumType)
        return;
    m_albumType = type;
    emit changed();
}

void EditorVm::setDate(int year, int month, int day)
{
    m_year = year;
    m_month = month;
    m_day = day;
    emit changed();
}

bool EditorVm::dateValid() const
{
    if (m_entity != QLatin1String("album"))
        return true;
    if (m_year == 0 && m_month == 0 && m_day == 0)
        return true;  // nothing typed yet: not red, but Save stays off (canSave needs a year)
    return ReleaseDate{m_year, m_month, m_day}.validate(m_currentYear).ok();
}

QStringList EditorVm::keysOf(int category) const
{
    QStringList out;
    for (const QString& key : m_tagKeys) {
        const int c = GenreVocabulary::isLanguageKey(key) ? 1 : GenreVocabulary::isMoodKey(key) ? 2 : 0;
        if (c == category)
            out << key;
    }
    return out;
}

QStringList EditorVm::namesOf(int category) const
{
    return tagDisplayNames(keysOf(category), m_genres, m_language);
}

QStringList EditorVm::tags() const
{
    return namesOf(0);
}

QVariantList EditorVm::choicesOf(int category) const
{
    QVariantList out;
    if (!m_genres)
        return out;
    const QStringList all = category == 0 ? m_genres->keys() : category == 1 ? m_genres->languageKeys() : m_genres->moodKeys();
    QList<QPair<QString, QString>> items;
    for (const QString& key : all)
        if (!m_tagKeys.contains(key))
            items.append({m_genres->name(key, m_language), key});
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first.compare(b.first, Qt::CaseInsensitive) < 0; });
    for (const auto& it : std::as_const(items))
        out.append(QVariantMap{{QStringLiteral("id"), it.second}, {QStringLiteral("title"), it.first}});
    return out;
}

QVariantList EditorVm::tagChoices() const
{
    return choicesOf(0);
}

void EditorVm::addTag(const QString& key)
{
    if (key.isEmpty() || m_tagKeys.contains(key))
        return;
    m_tagKeys.append(key);
    emit changed();
}

void EditorVm::removeOf(int category, int index)
{
    const QStringList keys = keysOf(category);
    if (index < 0 || index >= keys.size())
        return;
    m_tagKeys.removeAll(keys.at(index));
    emit changed();
}

void EditorVm::removeTag(int index)
{
    removeOf(0, index);
}

void EditorVm::copyTagsFromArtists()
{
    QVariantList who;   // [{ id, name }]: an artist that has no id yet is looked up by its name
    const QList<DraftCredit>& rows = m_authors.rows();
    for (const DraftCredit& c : rows)
        who << QVariantMap{{QStringLiteral("id"), c.id}, {QStringLiteral("name"), c.name}};
    if (!who.isEmpty())
        emit artistTagsRequested(who);
}

// Artists named in the tags of the new files that are not authors of the album yet: they cannot be found in the library (they may not
// exist), but they can be added by name
QVariantList EditorVm::pendingArtists() const
{
    QStringList seen;
    for (const DraftCredit& c : m_authors.rows())
        seen << c.name.toLower();
    QVariantList out;
    for (const EditRow& row : m_rows.rows())
        for (const QString& name : row.names) {
            const QString n = name.trimmed();
            if (n.isEmpty() || seen.contains(n.toLower()))
                continue;
            seen << n.toLower();
            out << QVariantMap{{QStringLiteral("id"), QVariant::fromValue<qint64>(0)}, {QStringLiteral("title"), n}};
        }
    return out;
}

void EditorVm::addTags(const QStringList& keys)
{
    bool any = false;
    for (const QString& key : keys)
        if (!key.isEmpty() && !m_tagKeys.contains(key)) {
            m_tagKeys.append(key);
            any = true;
        }
    if (any)
        emit changed();
}

void EditorVm::addAuthor(qint64 id, const QString& name, bool hidden, int role, const QString& avatar)
{
    if (name.trimmed().isEmpty())
        return;
    m_authors.add({id, name.trimmed(), role, hidden, avatar});
}

void EditorVm::removeAuthor(int row)
{
    m_authors.removeAt(row);
}

void EditorVm::moveAuthor(int from, int to)
{
    QList<DraftCredit> rows = m_authors.rows();
    if (from < 0 || to < 0 || from >= rows.size() || to >= rows.size() || from == to)
        return;
    rows.move(from, to);
    m_authors.setRows(rows);
}

void EditorVm::choosePicture(const QUrl& file)
{
    m_newPicture = file.isLocalFile() ? file.toLocalFile() : file.toString();
    m_removePicture = false;
    emit changed();
}

void EditorVm::clearPicture()
{
    m_newPicture.clear();
    m_removePicture = true;
    emit changed();
}

void EditorVm::addFiles(const QVariantList& urls)
{
    QStringList paths;
    for (const QVariant& v : urls) {
        const QUrl u = v.toUrl();
        paths << (u.isLocalFile() ? u.toLocalFile() : v.toString());
    }
    if (paths.isEmpty())
        return;
    m_busy = true;
    m_phase = QCoreApplication::translate("semplice::EditorVm", "Reading files...");
    m_done = 0;
    m_total = 0;
    emit busyChanged();
    emit scanRequested(paths, m_preselected, !m_scanned);
}

void EditorVm::applyScan(const ScanSummary& s)
{
    m_busy = false;
    emit busyChanged();
    m_skipped += s.skipped;
    // files dropped on an empty "Add album" that look like a playlist (mixed artists, no common album) make a playlist
    if (!m_scanned && !m_editing && m_entity == QLatin1String("album") && s.hasProposal && s.kind == 1 && m_title.trimmed().isEmpty()
        && m_authors.count() == 0)
        m_entity = QStringLiteral("playlist");
    if (!m_scanned && s.hasProposal && !m_editing) {
        if (m_entity == QLatin1String("album")) {
            if (s.kind == 0) {
                if (m_title.trimmed().isEmpty())
                    m_title = s.albumTitle;
                if (!s.authorRefs.isEmpty()) {
                    for (const ScanAuthor& a : s.authorRefs)
                        m_authors.add({a.id, a.name, 0, a.hidden, coverUrlFor(m_root, a.avatar)});   // the library's artists come with picture and id
                } else {
                    for (const QString& a : s.authors)
                        m_authors.add({0, a, 0, false});
                }
                if (!m_typeTouched)
                    m_albumType = s.albumType;
                if (m_year == 0) {
                    m_year = s.year;
                    m_month = s.month;
                    m_day = s.day;
                }
                for (const QString& t : s.tags)
                    if (!m_tagKeys.contains(t))
                        m_tagKeys.append(t);
                if (m_newPicture.isEmpty() && !s.coverPath.isEmpty() && !m_removePicture)
                    m_newPicture = s.coverPath;
                else if (s.embeddedCover && m_newPicture.isEmpty() && !s.rows.isEmpty())
                    m_currentPicture = embeddedPictureUrl(s.rows.first().path);  // shown now, saved with the album
            } else if (!m_typeTouched) {
                m_albumType = s.rows.size() >= 8 ? 1 : s.rows.size() >= 4 ? 2 : 0;
                if (s.embeddedCover && m_newPicture.isEmpty() && !s.rows.isEmpty())
                    m_currentPicture = embeddedPictureUrl(s.rows.first().path);
            }
        } else if (m_entity == QLatin1String("playlist")) {
            if (m_title.trimmed().isEmpty())
                m_title = s.kind == 1 ? s.playlistName : s.albumTitle;
            if (m_newPicture.isEmpty() && !s.coverPath.isEmpty() && !m_removePicture)
                m_newPicture = s.coverPath;
        }
    }
    if (!s.rows.isEmpty())
        m_scanned = true;
    QList<EditRow> rows;
    for (const ScanRow& r : s.rows) {
        EditRow row;
        row.scanIndex = r.index;
        row.title = r.title;
        row.names = r.creditNames;
        row.roles = r.creditRoles;
        row.artistsText = r.creditNames.join(QStringLiteral(", "));
        row.durationMs = r.durationMs;
        row.duplicate = r.duplicate;
        rows.append(row);
    }
    m_rows.append(rows);
    emit changed();
}

void EditorVm::removeRow(int row)
{
    m_rows.removeAt(row);
}

void EditorVm::setRowTitle(int row, const QString& title)
{
    m_rows.setTitleAt(row, title);
}

bool EditorVm::canSave() const
{
    if (m_busy || m_loading || m_entity.isEmpty())
        return false;
    if (m_title.trimmed().isEmpty())
        return false;
    if (m_entity == QLatin1String("album"))
        return m_authors.count() >= 1 && dateValid() && m_year > 0 && m_rows.count() >= 1;
    if (m_entity == QLatin1String("playlist"))
        return m_rows.count() >= 1 || m_pendingTrack > 0;
    if (m_entity == QLatin1String("track"))
        return m_authors.count() >= 1;
    return true;
}

QVariantMap EditorVm::buildCommand() const
{
    QVariantMap c;
    c.insert(QStringLiteral("type"), m_entity);
    c.insert(QStringLiteral("mode"), m_editing ? QStringLiteral("edit") : QStringLiteral("add"));
    c.insert(QStringLiteral("id"), m_id);
    c.insert(QStringLiteral("startOver"), true);
    c.insert(QStringLiteral("title"), m_title.trimmed());
    c.insert(QStringLiteral("name"), m_title.trimmed());
    c.insert(QStringLiteral("cover"), m_newPicture);
    c.insert(QStringLiteral("avatar"), m_newPicture);
    c.insert(QStringLiteral("removeCover"), m_removePicture);
    c.insert(QStringLiteral("removeAvatar"), m_removePicture);
    c.insert(QStringLiteral("hidden"), m_hidden);
    if (m_userHidden != m_originalUserHidden)
        c.insert(QStringLiteral("userHidden"), m_userHidden);
    if (m_entity != QLatin1String("track"))
        c.insert(QStringLiteral("tags"), m_tagKeys);  // a track keeps the tags of its albums (they are not edited per track)
    c.insert(QStringLiteral("albumType"), m_albumType);
    c.insert(QStringLiteral("year"), m_year);
    c.insert(QStringLiteral("month"), m_month);
    c.insert(QStringLiteral("day"), m_day);
    QVariantList authors;
    for (const DraftCredit& a : m_authors.rows())
        authors.append(QVariantMap{{QStringLiteral("id"), a.id}, {QStringLiteral("name"), a.name}});
    c.insert(QStringLiteral("authors"), authors);
    QVariantList credits;
    for (const DraftCredit& a : m_authors.rows())
        credits.append(QVariantMap{{QStringLiteral("id"), a.id}, {QStringLiteral("name"), a.name}, {QStringLiteral("role"), a.role}});
    c.insert(QStringLiteral("credits"), credits);
    QVariantList rows;
    for (const EditRow& r : m_rows.rows()) {
        if (r.scanIndex < 0)
            continue;  // tracks that already exist are changed one by one, not by Save
        QVariantList roles;
        for (int role : r.roles)
            roles << role;
        rows.append(QVariantMap{{QStringLiteral("index"), r.scanIndex},
                                {QStringLiteral("title"), r.title},
                                {QStringLiteral("names"), r.names},
                                {QStringLiteral("roles"), roles}});
    }
    c.insert(QStringLiteral("rows"), rows);
    if (m_pendingTrack > 0)
        c.insert(QStringLiteral("existing"), QVariantList{m_pendingTrack});
    return c;
}

void EditorVm::save()
{
    if (!canSave())
        return;
    m_busy = true;
    m_phase = QCoreApplication::translate("semplice::EditorVm", "Saving...");
    m_done = 0;
    m_total = 0;
    emit busyChanged();
    emit changed();
    emit saveRequested(buildCommand());
}

void EditorVm::cancelWork()
{
    emit stopRequested();
}

void EditorVm::setProgress(const QString& phase, qint64 done, qint64 total)
{
    const char* label = phase == QLatin1String("scan") ? "Reading files..." : phase == QLatin1String("copy") ? "Copying files..." : phase == QLatin1String("tags") ? "Writing tags..." : "Saving...";
    m_phase = QCoreApplication::translate("semplice::EditorVm", label);
    m_done = done;
    m_total = total;
    emit busyChanged();
}

void EditorVm::saveFinished(bool ok)
{
    m_busy = false;
    emit busyChanged();
    emit changed();
    Q_UNUSED(ok)
}

// ------------------------------------------------------------------------------------------ filling an edit window

void EditorVm::fillFromArtist(const ArtistCard& card)
{
    if (m_entity != QLatin1String("artist") || card.artist.id != m_id)
        return;
    m_title = card.artist.name;
    m_originalTitle = m_title;
    m_currentPicture = coverUrlFor(m_root, card.artist.avatar);
    m_tagKeys = card.tags;
    m_hidden = card.artist.hidden;
    m_userHidden = m_originalUserHidden = card.artist.userHidden;
    QList<ListRow> albums;
    for (const ArtistCardSection& s : card.sections)
        if (s.section == ArtistSection::Discography)
            for (const CarouselItem& item : s.items)
                if (item.id > 0 && !item.title.isEmpty())
                    albums.append(toListRow(item));
    m_albums.setRows(albums);
    m_loading = false;
    emit changed();
}

void EditorVm::fillFromAlbum(const AlbumCard& card)
{
    if (m_entity != QLatin1String("album") || card.id != m_id)
        return;
    const bool rowsOnly = std::exchange(m_rowsOnly, false);
    if (!rowsOnly) {  // a refresh after a track change keeps what the user typed
        m_title = card.title;
        m_originalTitle = m_title;
        m_currentPicture = coverUrlFor(m_root, card.cover);
        m_albumType = static_cast<int>(card.type);
        m_typeTouched = true;
        m_year = card.date.year;
        m_month = card.date.month;
        m_day = card.date.day;
        m_tagKeys = card.tags;
        QList<DraftCredit> authors;
        for (const ArtistRef& a : card.artists)
            authors.append({a.id, a.name, 0, a.hidden, coverUrlFor(m_root, a.avatar)});
        m_authors.setRows(authors);
    }
    QList<EditRow> rows;
    for (const TrackRow& t : card.tracks) {
        EditRow r;
        r.trackId = t.id;
        r.title = t.title;
        r.artistsText = t.artistsText();
        r.durationMs = t.durationMs;
        r.canRemove = t.memberships > 1;
        r.cover = t.hasEmbeddedCover && !m_root.isEmpty() ? QStringLiteral("image://cover/file?embedded=1&path=") + QString::fromLatin1(QUrl::toPercentEncoding(m_root + QLatin1Char('/') + t.filePath))
                                                         : coverUrlFor(m_root, t.coverPath);
        rows.append(r);
    }
    {
        SEMPLICE_SLOW_SCOPE("EditorVm::fillFromAlbum rows");
        m_rows.reset(keepScanned(rows));
    }
    m_loading = false;
    SEMPLICE_SLOW_SCOPE("EditorVm::fillFromAlbum changed()");
    emit changed();
}

void EditorVm::fillFromPlaylist(const PlaylistCard& card)
{
    if (m_entity != QLatin1String("playlist") || card.id != m_id)
        return;
    const bool rowsOnly = std::exchange(m_rowsOnly, false);
    if (!rowsOnly) {
        m_title = card.name;
        m_originalTitle = m_title;
        m_currentPicture = coverUrlFor(m_root, card.cover);
    }
    QList<EditRow> rows;
    for (const TrackRow& t : card.tracks) {
        EditRow r;
        r.trackId = t.id;
        r.title = t.title;
        r.artistsText = t.artistsText();
        r.durationMs = t.durationMs;
        r.canRemove = t.memberships > 1;
        r.cover = coverUrlFor(m_root, t.coverPath);
        rows.append(r);
    }
    m_rows.reset(keepScanned(rows));
    m_loading = false;
    emit changed();
}

void EditorVm::fillFromTrack(const TrackEdit& t)
{
    if (m_entity == QLatin1String("playlist") && m_pendingTrack == t.id) {
        // "Add as new playlist" from a track menu: that track is the first row
        EditRow r;
        r.trackId = t.id;
        r.title = t.title;
        r.artistsText = t.artistNames.join(QStringLiteral(", "));
        m_rows.reset({r});
        emit changed();
        return;
    }
    if (m_entity != QLatin1String("track") || t.id != m_id)
        return;
    m_title = t.title;
    m_originalTitle = t.title;
    QList<DraftCredit> authors;
    for (int i = 0; i < t.artistNames.size(); ++i)
        authors.append({t.artistIds.value(i), t.artistNames.at(i), t.roles.value(i), t.hidden.value(i), coverUrlFor(m_root, t.avatars.value(i))});
    m_authors.setRows(authors);
    m_tagKeys = t.tags;
    m_loading = false;
    emit changed();
}

}  // namespace semplice

namespace semplice {

QList<EditRow> EditorVm::keepScanned(QList<EditRow> rows) const
{
    for (const EditRow& r : m_rows.rows())
        if (r.scanIndex >= 0)
            rows.append(r);
    return rows;
}

}  // namespace semplice

namespace semplice {

void EditorVm::refreshRows()
{
    if (m_loading || !m_editing || (m_entity != QLatin1String("album") && m_entity != QLatin1String("playlist")))
        return;
    m_rowsOnly = true;
    emit loadRequested(m_entity, m_id);
}

}  // namespace semplice
