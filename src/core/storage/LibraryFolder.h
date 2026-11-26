#pragma once

#include "core/db/Database.h"
#include "core/storage/FileStorageService.h"
#include "core/storage/IFileAttributes.h"

#include <memory>

namespace semplice {

// Where everything of one library lives (SPEC 4). All paths absolute, forward slashes.
struct LibraryLayout {
    QString root;
    QString dotLibrary;   // <root>/.library (hidden)
    QString dbPath;       // .library/library.db
    QString jsonPath;     // .library/library.json
    QString journalDir;   // .library/journal
    QString coversDir;    // .library/covers
    QString artistsDir;   // <root>/Artists
    QString playlistsDir; // <root>/Playlists

    static LibraryLayout forRoot(const QString& root);
    QString backupPath(int n) const;  // .library/library.db.bak1 .. bak3
    static constexpr int kBackupCount = 3;
};

struct LibraryInfo {
    QString uid;
    int schema = 0;
};

// Folder-level operations: marker file, layout, health check, backups (SPEC 6.6, 6.7).
class LibraryFolder {
public:
    static constexpr const char* kAppId = "semplice";

    // Starts a new library in an EMPTY folder (created if missing): `.library` (hidden), `journal`,
    // `covers`, `Artists`, `Playlists` and `library.json`. Does not create the database.
    static Result<LibraryInfo> create(const QString& root, IFileAttributes& attributes);

    // Reads and validates `.library/library.json`: E_NOT_A_LIBRARY if missing or foreign,
    // E_SCHEMA_TOO_NEW if written by a newer Semplice.
    static Result<LibraryInfo> inspect(const QString& root);

    // PRAGMA quick_check; E_DATABASE_CORRUPT unless the answer is "ok".
    static Result<void> quickCheck(Database& db);

    // If the newest backup is missing or older than 24 h: rotate bak2->bak3, bak1->bak2 and write a
    // fresh bak1 with VACUUM INTO. Returns true if a backup was made.
    static Result<bool> rotateBackupsIfDue(const LibraryLayout& layout, Database& db, const IClock& clock);

    // Replaces a corrupt library.db with the newest backup that passes quick_check. The corrupt file is
    // renamed to `library.db.corrupt-<timestamp>`, never deleted. Close every connection first.
    // Returns the number (1..3) of the backup used.
    static Result<int> restoreNewestGoodBackup(const LibraryLayout& layout, const IClock& clock);
};

// A library that is ready to use: database migrated and checked, journal recovered.
struct OpenedLibrary {
    LibraryLayout layout;
    LibraryInfo info;
    std::unique_ptr<Database> db;
    std::unique_ptr<FileStorageService> storage;
    RecoveryReport recovery;     // anything repaired -> show ONE info banner
    bool backupCreated = false;
};

// Opens an existing library: inspect, open + migrate the database, quick_check, backups, journal recovery.
Result<OpenedLibrary> openLibrary(const QString& root, ITrash& trash, const IClock& clock,
                                  FaultInjector* injector = nullptr);

// Creates a new library in an empty folder and opens it.
Result<OpenedLibrary> createLibrary(const QString& root, IFileAttributes& attributes, ITrash& trash,
                                    const IClock& clock);

}  // namespace semplice
