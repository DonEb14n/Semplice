#pragma once

#include "core/services/LibraryService.h"
#include "core/services/Maintenance.h"
#include "core/tags/EncodingFixer.h"
#include "core/tags/GenreMap.h"

namespace semplice {

struct RescanReport {
    int checked = 0;            // audio files found on disk
    int refreshed = 0;          // known files whose size/time changed
    int adopted = 0;            // files nobody knew: imported into the "Rescan ..." playlist
    int duplicatesTrashed = 0;  // stray copies of tracks already in the library (sent to the Recycle Bin)
    int missingRemoved = 0;     // rows whose file vanished (removed with the normal cascades)
    int missingBlocked = 0;     // would have been removed, but so many are missing that the folder is probably offline
    int skipped = 0;            // unreadable files left where they are
    int tagsSynced = 0;         // tag writes retried successfully
    int tagsFailed = 0;         // still out of sync (locked or read-only)
    PlaylistId rescanPlaylist;  // the playlist created for adopted files, if any
    bool cancelled = false;
    QStringList notes;
    MaintenanceReport maintenance;
};

struct RescanOptions {
    // Removing the rows of more than max(20, half of all tracks) missing files needs this flag: a
    // disconnected drive must not wipe the library.
    bool allowMassRemoval = false;
};

// Settings -> Rescan and F5 (SPEC 6.5). Incremental: only size + modification time are compared.
// The stored hash is of the SOURCE bytes and is never recomputed for a known file (decision 7).
class RescanService {
public:
    RescanService(LibraryService& library, ITagService& tags, const GenreVocabulary& genres,
                  const ICodepageConverter& codepages, Database& db, IFileStorage& storage, const LibraryLayout& layout,
                  const IClock& clock);

    Result<RescanReport> run(const RescanOptions& options = {}, const JobContext& job = {});

private:
    LibraryService& m_library;
    ITagService& m_tags;
    GenreMap m_genreMap;
    EncodingFixer m_fixer;
    Database& m_db;
    IFileStorage& m_storage;
    LibraryLayout m_layout;
    const IClock& m_clock;
};

}  // namespace semplice
