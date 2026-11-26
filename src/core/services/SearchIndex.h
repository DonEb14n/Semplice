#pragma once

#include "core/db/Database.h"
#include "core/services/LibraryTypes.h"

#include <QReadWriteLock>

#include <vector>

namespace semplice {

enum class SearchKind { Artist = 0, Album = 1, Playlist = 2, Track = 3 };

struct SearchHit {
    SearchKind kind = SearchKind::Artist;
    qint64 id = 0;
    QString display;
    int rank = 0;  // 0 = prefix, 1 = word start, 2 = substring
    bool userHidden = false;  // an artist hidden by hand (shown grey)
};

struct SearchCategory {
    QList<SearchHit> top;  // best `limit` hits, ranked
    int total = 0;         // all matches ("Show more" appears when total > top.size())
};

struct SearchResults {
    SearchCategory artists;
    SearchCategory albums;
    SearchCategory playlists;
    SearchCategory tracks;
};

// In-memory index of normalised names (SPEC 3). Substring match, ranked prefix > word start >
// substring. Thread-safe: many readers or one writer. Does not use SQL LIKE.
class SearchIndex {
public:
    // Full rebuild from the database (startup). The Unsorted album has no title and is not indexed.
    Result<void> rebuild(Database& db);
    // Re-reads every touched item; items that no longer exist are removed.
    Result<void> refresh(Database& db, const ChangeSet& changes);

    SearchResults search(const QString& query, int perCategoryLimit = 5, bool includeHiddenArtists = false) const;
    // Artists only, hidden ones included (the "Add artist" submenu).
    SearchCategory searchArtists(const QString& query, int limit, bool includeHidden) const;

    int size(SearchKind kind) const;

private:
    struct Entry {
        qint64 id = 0;
        QString display;
        QString key;  // Normalizer::searchKey(display)
        bool hidden = false;
        bool userHidden = false;
    };

    static int index(SearchKind kind) { return static_cast<int>(kind); }
    void upsertLocked(SearchKind kind, qint64 id, const QString& display, int flags);  // flags: 1 = no folder, 2 = hidden by hand
    void removeLocked(SearchKind kind, qint64 id);
    SearchCategory searchLocked(SearchKind kind, const QString& queryKey, int limit, bool includeHidden) const;

    mutable QReadWriteLock m_lock;
    std::vector<Entry> m_entries[4];
};

}  // namespace semplice
