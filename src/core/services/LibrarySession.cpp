#include "core/services/LibrarySession.h"

#include "core/storage/OwnFiles.h"
#include "core/util/Logger.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace semplice {

namespace {

// Everything under .library/tmp belongs to an import that did not finish (extracted cover pictures).
void clearTemporaryFolder(const LibraryLayout& layout)
{
    const QString tmp = layout.dotLibrary + QStringLiteral("/tmp");
    if (!QFileInfo(tmp).isDir())
        return;
    QDirIterator it(tmp, QDir::Files | QDir::Hidden);
    while (it.hasNext())
        (void)ownfiles::removeFile(it.next());
}

Result<std::unique_ptr<LibrarySession>> finish(Result<OpenedLibrary> opened, const SessionDependencies& deps,
                                               std::unique_ptr<LibrarySession> (*make)(OpenedLibrary, GenreVocabulary, const SessionDependencies&))
{
    if (!opened.ok())
        return opened.error();
    auto genres = GenreVocabulary::load();
    if (!genres.ok())
        return genres.error();
    auto session = make(std::move(opened).value(), std::move(genres).value(), deps);
    if (auto started = session->finishStartup(); !started.ok())
        return started.error();
    return session;
}

}  // namespace

LibrarySession::LibrarySession(OpenedLibrary opened, GenreVocabulary genres, const SessionDependencies& deps)
    : m_opened(std::move(opened)), m_genres(std::move(genres)), m_tags(deps.replace, deps.releaser)
{
    LibraryEnvironment env;
    env.tags = &m_tags;
    env.genres = &m_genres;
    env.libraryRoot = m_opened.layout.root;
    m_opened.storage->setReleaser(deps.releaser);  // renames and deletes let go of the file that is playing
    m_library = std::make_unique<LibraryService>(*m_opened.db, *m_opened.storage, m_namer, deps.clock, m_genres.toTagVocabulary(), env);
    m_import = std::make_unique<ImportService>(*m_library, m_tags, m_genres, deps.codepages, *m_opened.db, deps.clock,
                                               m_opened.layout.root);
    m_rescan = std::make_unique<RescanService>(*m_library, m_tags, m_genres, deps.codepages, *m_opened.db, *m_opened.storage,
                                               m_opened.layout, deps.clock);
    m_share = std::make_unique<ShareService>(*m_opened.db, *m_library, m_opened.layout.root);
}

LibrarySession::~LibrarySession() = default;

Result<void> LibrarySession::finishStartup()
{
    TagService::removeStaleTempFiles(m_opened.layout.root);
    clearTemporaryFolder(m_opened.layout);
    if (auto r = m_search.rebuild(*m_opened.db); !r.ok())
        return r.error();
    // The index follows every committed change instead of being rebuilt.
    Database* db = m_opened.db.get();
    SearchIndex* index = &m_search;
    connect(m_library.get(), &LibraryService::libraryChanged, this, [db, index](const ChangeSet& changes) {
        if (auto r = index->refresh(*db, changes); !r.ok())
            SLOG_WARN(QStringLiteral("search index refresh failed: ") + r.error().devMessage);
    });
    return {};
}

Result<std::unique_ptr<LibrarySession>> LibrarySession::open(const QString& root, const SessionDependencies& deps)
{
    auto opened = openLibrary(root, deps.trash, deps.clock, deps.injector);
    return finish(std::move(opened), deps, [](OpenedLibrary o, GenreVocabulary g, const SessionDependencies& d) {
        return std::unique_ptr<LibrarySession>(new LibrarySession(std::move(o), std::move(g), d));
    });
}

Result<std::unique_ptr<LibrarySession>> LibrarySession::create(const QString& root, const SessionDependencies& deps)
{
    auto opened = createLibrary(root, deps.attributes, deps.trash, deps.clock);
    return finish(std::move(opened), deps, [](OpenedLibrary o, GenreVocabulary g, const SessionDependencies& d) {
        return std::unique_ptr<LibrarySession>(new LibrarySession(std::move(o), std::move(g), d));
    });
}

// ---------------------------------------------------------------------------------------------

FolderState LibrarySwitcher::classify(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir())
        return FolderState::Missing;
    if (LibraryFolder::inspect(path).ok() || QFileInfo::exists(LibraryLayout::forRoot(path).jsonPath))
        return FolderState::Library;  // (a library from a newer Semplice is still "a library": opening explains why not)
    if (QDir(path).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        return FolderState::Empty;
    return FolderState::Other;
}

Result<std::unique_ptr<LibrarySession>> LibrarySwitcher::select(const QString& path, const SessionDependencies& deps,
                                                                ISettings* settings)
{
    Result<std::unique_ptr<LibrarySession>> session = Error::make(ErrorCode::NotFound, path);
    switch (classify(path)) {
    case FolderState::Library: session = LibrarySession::open(path, deps); break;
    case FolderState::Empty: session = LibrarySession::create(path, deps); break;
    case FolderState::Other: return Error::make(ErrorCode::FolderNotEmpty, path);
    case FolderState::Missing: return Error::make(ErrorCode::NotFound, path);
    }
    if (session.ok() && settings)
        settings->setLibraryPath(QDir::cleanPath(path));
    return session;
}

LibrarySwitcher::StartupResult LibrarySwitcher::openStored(ISettings& settings, const SessionDependencies& deps)
{
    StartupResult result;
    result.path = settings.libraryPath();
    if (result.path.isEmpty()) {
        result.state = StartupState::NoLibraryYet;
        return result;
    }
    if (classify(result.path) == FolderState::Missing) {
        result.state = StartupState::PathMissing;  // the folder was moved or the drive is not connected
        return result;
    }
    auto session = LibrarySession::open(result.path, deps);
    if (!session.ok()) {
        result.state = StartupState::Failed;
        result.error = session.error();
        return result;
    }
    result.state = StartupState::Ready;
    result.session = std::move(session).value();
    return result;
}

}  // namespace semplice
