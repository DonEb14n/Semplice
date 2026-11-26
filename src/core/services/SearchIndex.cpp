#include "core/services/SearchIndex.h"

#include "core/util/Normalizer.h"

#include <algorithm>

namespace semplice {

namespace {

// 0 = prefix, 1 = at the start of a word, 2 = anywhere; -1 = no match.
int rankOf(const QString& key, const QString& query)
{
    const qsizetype pos = key.indexOf(query);
    if (pos < 0)
        return -1;
    if (pos == 0)
        return 0;
    // Look for a later occurrence that starts a word (preceded by a space or punctuation).
    qsizetype p = pos;
    while (p >= 0) {
        const QChar before = key.at(p - 1);
        if (!before.isLetterOrNumber())
            return 1;
        p = key.indexOf(query, p + 1);
    }
    return 2;
}

}  // namespace

Result<void> SearchIndex::rebuild(Database& db)
{
    std::vector<Entry> fresh[4];

    auto load = [&](SearchKind kind, const QString& sql, bool withHidden) -> Result<void> {
        auto r = db.run(sql);
        if (!r.ok())
            return r.error();
        QSqlQuery q = std::move(r).value();
        std::vector<Entry>& out = fresh[index(kind)];
        while (q.next()) {
            Entry e;
            e.id = q.value(0).toLongLong();
            e.display = q.value(1).toString();
            e.key = Normalizer::searchKey(e.display);
            e.hidden = withHidden && (q.value(2).toInt() & 1) != 0;
            e.userHidden = withHidden && (q.value(2).toInt() & 2) != 0;
            out.push_back(std::move(e));
        }
        return {};
    };
    if (auto r = load(SearchKind::Artist, QStringLiteral("SELECT id, name, hidden | (user_hidden << 1) FROM artist"), true); !r.ok())
        return r.error();
    if (auto r = load(SearchKind::Album, QStringLiteral("SELECT id, title FROM album WHERE is_unsorted = 0"), false);
        !r.ok())
        return r.error();
    if (auto r = load(SearchKind::Playlist, QStringLiteral("SELECT id, name FROM playlist"), false); !r.ok())
        return r.error();
    if (auto r = load(SearchKind::Track, QStringLiteral("SELECT id, title FROM track"), false); !r.ok())
        return r.error();

    QWriteLocker lock(&m_lock);
    for (int i = 0; i < 4; ++i)
        m_entries[i] = std::move(fresh[i]);
    return {};
}

void SearchIndex::upsertLocked(SearchKind kind, qint64 id, const QString& display, int flags)
{
    std::vector<Entry>& list = m_entries[index(kind)];
    Entry e{id, display, Normalizer::searchKey(display), (flags & 1) != 0, (flags & 2) != 0};
    for (Entry& existing : list) {
        if (existing.id == id) {
            existing = std::move(e);
            return;
        }
    }
    list.push_back(std::move(e));
}

void SearchIndex::removeLocked(SearchKind kind, qint64 id)
{
    std::vector<Entry>& list = m_entries[index(kind)];
    list.erase(std::remove_if(list.begin(), list.end(), [id](const Entry& e) { return e.id == id; }), list.end());
}

Result<void> SearchIndex::refresh(Database& db, const ChangeSet& changes)
{
    // Read first (no lock held), then apply under one write lock.
    struct Row {
        SearchKind kind;
        qint64 id;
        bool exists;
        QString display;
        int flags;
    };
    std::vector<Row> rows;

    auto fetch = [&](SearchKind kind, qint64 id, const QString& sql) -> Result<void> {
        auto r = db.run(sql, {id});
        if (!r.ok())
            return r.error();
        QSqlQuery q = std::move(r).value();
        if (q.next())
            rows.push_back({kind, id, true, q.value(0).toString(), q.value(1).toInt()});
        else
            rows.push_back({kind, id, false, {}, 0});
        return {};
    };
    for (ArtistId id : changes.artists) {
        if (auto r = fetch(SearchKind::Artist, id.value, QStringLiteral("SELECT name, hidden | (user_hidden << 1) FROM artist WHERE id = ?"));
            !r.ok())
            return r.error();
    }
    for (AlbumId id : changes.albums) {
        if (auto r = fetch(SearchKind::Album, id.value,
                           QStringLiteral("SELECT title, 0 FROM album WHERE id = ? AND is_unsorted = 0"));
            !r.ok())
            return r.error();
    }
    for (PlaylistId id : changes.playlists) {
        if (auto r = fetch(SearchKind::Playlist, id.value, QStringLiteral("SELECT name, 0 FROM playlist WHERE id = ?"));
            !r.ok())
            return r.error();
    }
    for (TrackId id : changes.tracks) {
        if (auto r = fetch(SearchKind::Track, id.value, QStringLiteral("SELECT title, 0 FROM track WHERE id = ?"));
            !r.ok())
            return r.error();
    }

    QWriteLocker lock(&m_lock);
    for (const Row& row : rows) {
        if (row.exists)
            upsertLocked(row.kind, row.id, row.display, row.flags);
        else
            removeLocked(row.kind, row.id);
    }
    return {};
}

int SearchIndex::size(SearchKind kind) const
{
    QReadLocker lock(&m_lock);
    return static_cast<int>(m_entries[index(kind)].size());
}

SearchCategory SearchIndex::searchLocked(SearchKind kind, const QString& queryKey, int limit,
                                         bool includeHidden) const
{
    struct Match {
        int rank;
        const Entry* entry;
    };
    std::vector<Match> matches;
    for (const Entry& e : m_entries[index(kind)]) {
        if (e.hidden && !includeHidden)
            continue;
        const int rank = rankOf(e.key, queryKey);
        if (rank >= 0)
            matches.push_back({rank, &e});
    }

    auto better = [](const Match& a, const Match& b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        if (a.entry->key.size() != b.entry->key.size())
            return a.entry->key.size() < b.entry->key.size();
        if (a.entry->key != b.entry->key)
            return a.entry->key < b.entry->key;
        return a.entry->id < b.entry->id;
    };
    const size_t take = std::min<size_t>(matches.size(), static_cast<size_t>(std::max(limit, 0)));
    std::partial_sort(matches.begin(), matches.begin() + static_cast<std::ptrdiff_t>(take), matches.end(), better);

    SearchCategory out;
    out.total = static_cast<int>(matches.size());
    for (size_t i = 0; i < take; ++i)
        out.top.append({kind, matches[i].entry->id, matches[i].entry->display, matches[i].rank, matches[i].entry->userHidden});
    return out;
}

SearchResults SearchIndex::search(const QString& query, int perCategoryLimit, bool includeHiddenArtists) const
{
    SearchResults out;
    const QString key = Normalizer::searchKey(query);
    if (key.isEmpty())
        return out;
    QReadLocker lock(&m_lock);
    out.artists = searchLocked(SearchKind::Artist, key, perCategoryLimit, includeHiddenArtists);
    out.albums = searchLocked(SearchKind::Album, key, perCategoryLimit, false);
    out.playlists = searchLocked(SearchKind::Playlist, key, perCategoryLimit, false);
    out.tracks = searchLocked(SearchKind::Track, key, perCategoryLimit, false);
    return out;
}

SearchCategory SearchIndex::searchArtists(const QString& query, int limit, bool includeHidden) const
{
    const QString key = Normalizer::searchKey(query);
    if (key.isEmpty())
        return {};
    QReadLocker lock(&m_lock);
    return searchLocked(SearchKind::Artist, key, limit, includeHidden);
}

}  // namespace semplice
