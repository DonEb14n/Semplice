#pragma once

#include "core/services/ISettings.h"
#include "core/services/ImportService.h"
#include "core/services/LibraryService.h"
#include "core/services/RescanService.h"
#include "core/share/ShareService.h"
#include "core/services/SearchIndex.h"
#include "core/storage/LibraryFolder.h"
#include "core/tags/EncodingFixer.h"
#include "core/tags/TagService.h"

#include <QObject>

#include <memory>

namespace semplice {

// What a session needs from the outside world (all replaceable in tests).
struct SessionDependencies {
    ITrash& trash;
    IAtomicReplace& replace;
    const ICodepageConverter& codepages;
    const IClock& clock;
    IFileAttributes& attributes;
    IFileReleaser* releaser = nullptr;  // the audio backend (M5)
    FaultInjector* injector = nullptr;  // tests only
};

// Everything that belongs to ONE open library (TECH_DESIGN 1.3): database, journaled storage, services,
// search index. Switching library = destroying this object and creating another.
//
// Opening runs, in this order: marker check, database migration + quick_check, backup rotation,
// journal recovery, stale temp file cleanup, search index build.
class LibrarySession final : public QObject {
    Q_OBJECT
public:
    static Result<std::unique_ptr<LibrarySession>> open(const QString& root, const SessionDependencies& deps);
    // Starts a new library in an EMPTY folder.
    static Result<std::unique_ptr<LibrarySession>> create(const QString& root, const SessionDependencies& deps);
    ~LibrarySession() override;

    const LibraryLayout& layout() const { return m_opened.layout; }
    const LibraryInfo& info() const { return m_opened.info; }
    Database& database() { return *m_opened.db; }
    FileStorageService& storage() { return *m_opened.storage; }
    const GenreVocabulary& genres() const { return m_genres; }
    ITagService& tags() { return m_tags; }
    LibraryService& library() { return *m_library; }
    ImportService& importer() { return *m_import; }
    RescanService& rescanner() { return *m_rescan; }
    ShareService& share() { return *m_share; }
    SearchIndex& search() { return m_search; }

    // What start-up repaired (journal recovery); show ONE info banner if repairedAnything().
    const RecoveryReport& recovery() const { return m_opened.recovery; }
    bool backupCreated() const { return m_opened.backupCreated; }

    // Called by open()/create() once the object is built.
    Result<void> finishStartup();

private:
    LibrarySession(OpenedLibrary opened, GenreVocabulary genres, const SessionDependencies& deps);

    OpenedLibrary m_opened;
    GenreVocabulary m_genres;
    SanitizingPathNamer m_namer;
    TagService m_tags;
    std::unique_ptr<LibraryService> m_library;
    std::unique_ptr<ImportService> m_import;
    std::unique_ptr<RescanService> m_rescan;
    std::unique_ptr<ShareService> m_share;
    SearchIndex m_search;
};

// The "Select folder" / "Change" / "locate your library" logic (SPEC 6.6).
enum class FolderState {
    Missing,        // the path does not exist
    Empty,          // an empty folder: a new library can start here
    Library,        // contains .library/library.json of Semplice
    Other           // a folder with other things in it: not allowed
};

class LibrarySwitcher {
public:
    static FolderState classify(const QString& path);

    // Opens an existing library or creates one in an empty folder; anything else is E_FOLDER_NOT_EMPTY
    // (the UI shows an error banner and asks again). On success the path is stored in the settings.
    static Result<std::unique_ptr<LibrarySession>> select(const QString& path, const SessionDependencies& deps,
                                                          ISettings* settings = nullptr);

    enum class StartupState {
        NoLibraryYet,  // first launch: show the "Select folder" card
        PathMissing,   // the stored folder is gone (moved or drive unplugged): "locate your library" wording
        Ready,
        Failed         // the library is there but cannot be used (see `error`: schema too new, corrupt, ...)
    };
    struct StartupResult {
        StartupState state = StartupState::NoLibraryYet;
        std::unique_ptr<LibrarySession> session;
        Error error;
        QString path;  // the stored path (also for PathMissing)
    };
    static StartupResult openStored(ISettings& settings, const SessionDependencies& deps);
};

}  // namespace semplice
