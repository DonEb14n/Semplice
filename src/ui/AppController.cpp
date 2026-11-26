#include "ui/AppController.h"

#include "core/recommend/LibraryRecommendationSource.h"
#include "core/storage/ThumbnailCache.h"
#include "core/util/Logger.h"
#include "ui/AsyncPlaybackStore.h"
#include "ui/DeleteText.h"

#include <QDate>
#include "platform/StallSampler.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <atomic>
#include <QRandomGenerator>
#include <QThreadPool>
#include <thread>
#include <QTimer>
#include <QDir>
#include <QFileInfo>

namespace semplice {

// The engine thread asks the GUI thread's audio backend to let go of a file before a tag write replaces it.
struct AppController::ReleaserProxy final : IFileReleaser {
    explicit ReleaserProxy(platform::QtAudioBackend& backend) : m_backend(backend) {}
    // The backend itself waits for its audio thread (not for the GUI thread): the window never freezes for this.
    Result<void> releaseFile(const QString& path) override
    {
        if (m_closing)
            return {};
        QElapsedTimer waited;
        waited.start();
        auto result = m_backend.releaseFile(path);
        if (waited.elapsed() > 100)
            SLOG_WARN(QStringLiteral("the engine waited %1 ms for the player to let go of a file").arg(waited.elapsed()));
        return result;
    }
    bool inUse(const QString& path) const override { return !m_closing && m_backend.maybeAffectedBy(path); }
    void fileReady(const QString& path) override
    {
        if (!m_closing)
            m_backend.fileReady(path);
    }
    platform::QtAudioBackend& m_backend;
    std::atomic<bool> m_closing{false};
};

// A thread with a database connection of its own: "play this album" is answered here, so it never queues behind a save.
struct AppController::RequestWorker {
    explicit RequestWorker(const QString& dbPath)
    {
        context.moveToThread(&thread);
        thread.setObjectName(QStringLiteral("semplice-requests"));
        thread.start();
        QMetaObject::invokeMethod(&context, [this, dbPath] {
            auto opened = Database::open(dbPath);
            if (opened.ok())
                db = std::move(opened).value();
            else
                SLOG_ERROR(QStringLiteral("request database: ") + opened.error().devMessage);
        }, Qt::BlockingQueuedConnection);
    }
    ~RequestWorker()
    {
        QMetaObject::invokeMethod(&context, [this] { db.reset(); }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
    QThread thread;
    QObject context;
    std::unique_ptr<Database> db;  // used on `thread` only
};

// Everything that exists only while a library is open and belongs to the GUI thread.
struct AppController::PlaybackStack {
    PlaybackStack(std::unique_ptr<Database> database, const QString& root, const QString& dbPath, const GenreVocabulary& genres,
                  platform::QtAudioBackend& backend, ISettings& settings)
        : db(std::move(database)),
          catalog(*db, root),
          store(dbPath, *db),
          recommender(dbPath, genres, clock, QRandomGenerator::global()->generate64()),
          service(PlaybackService::Dependencies{backend, catalog, store, clock, &recommender, &settings, 0})
    {
    }
    std::unique_ptr<Database> db;  // the playback connection (GUI thread)
    SystemClock clock;
    LibraryTrackCatalog catalog;
    AsyncPlaybackStore store;  // writes on a thread of its own (the engine may hold the database for seconds)
    LibraryRecommendationSource recommender;  // endless stream after an album / artist / carousel (M9)
    PlaybackService service;
};

namespace {
GenreVocabulary loadGenres()
{
    auto genres = GenreVocabulary::load();
    return genres.ok() ? std::move(genres).value() : GenreVocabulary();
}
}  // namespace

AppController::AppController(ISettings& settings, ShellVm& shell, QObject* parent)
    : QObject(parent),
      m_settings(settings),
      m_uiState(settings),
      m_genres(loadGenres()),
      m_libraryVm(m_uiState),
      m_homeVm([&shell] { return shell.userName(); }, &m_genres, settings.language()),
      m_categoryVm(&m_genres, settings.language()),
      m_artistCard(&m_genres, settings.language()),
      m_albumCard(&m_genres, settings.language()),
      m_playlistCard(&m_genres, settings.language()),
      m_settingsVm(settings),
      m_editor(&m_genres, settings.language()),
      m_artistEditor(&m_genres, settings.language()),
      m_trackEditor(&m_genres, settings.language()),
      m_shareVm(&m_genres, settings.language())
{
    wireEditor(m_trackEditor);
    m_editor.setCurrentYear(QDate::currentDate().year());
    m_tagSyncTimer.setSingleShot(true);
    m_tagSyncTimer.setInterval(1500);
    connect(&m_tagSyncTimer, &QTimer::timeout, this, [this] { QMetaObject::invokeMethod(m_engine, "syncTags", Qt::QueuedConnection); });
    m_savePauseTimer.setSingleShot(true);
    m_savePauseTimer.setInterval(400);
    connect(&m_savePauseTimer, &QTimer::timeout, this, [this] {
        if (!m_saveTokens.isEmpty() && m_playback && m_playback->service.state() == PlayState::Playing) {
            m_playback->service.pause();
            m_pausedForSave = true;
        }
    });
    m_artistEditor.setCurrentYear(QDate::currentDate().year());
    wireEditor(m_editor);
    wireEditor(m_artistEditor);
    connect(&m_settingsVm, &SettingsVm::saved, &shell, &ShellVm::notifyUserNameChanged);
    connect(&m_settingsVm, &SettingsVm::rescanRequested, this, [this] {
        if (m_startup != Ready || m_settingsVm.rescanning())
            return;
        m_rescanToken = ++m_nextToken;
        m_settingsVm.setRescan(true);
        QMetaObject::invokeMethod(m_engine, "rescan", Qt::QueuedConnection, Q_ARG(int, m_rescanToken));
    });
    connect(&m_settingsVm, &SettingsVm::rescanCancelRequested, this, [this] { m_engine->requestStop(); });
    connect(&shell, &ShellVm::userNameChanged, &m_homeVm, &HomeVm::refreshGreeting);
    m_engine = new LibraryEngine;
    m_releaser = std::make_unique<ReleaserProxy>(m_backend);
    m_engine->setReleaser(m_releaser.get());
    m_engine->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_engine, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("semplice-engine"));
    m_thread.start();

    m_playerVm.setVolumeSource(&settings);

    // engine -> GUI
    connect(m_engine, &LibraryEngine::noLibraryYet, this, [this] { setStartup(NoLibraryYet); });
    connect(m_engine, &LibraryEngine::pathMissing, this, [this](const QString& path) {
        m_libraryPath = path;
        m_shareVm.setLibraryRoot(path);
        setStartup(PathMissing);
    });
    connect(m_engine, &LibraryEngine::opened, this, &AppController::onOpened);
    connect(m_engine, &LibraryEngine::openFailed, this, &AppController::onOpenFailed);
    connect(m_engine, &LibraryEngine::closed, this, &AppController::onClosed);
    connect(m_engine, &LibraryEngine::libraryChanged, this, &AppController::onLibraryChanged);
    connect(m_engine, &LibraryEngine::artistsReady, this, [this](const QList<ListRow>& rows) { SEMPLICE_SLOW_SCOPE("artistsReady"); m_libraryVm.setRows(1, rows); });
    connect(m_engine, &LibraryEngine::albumsReady, this, [this](const QList<ListRow>& rows) { SEMPLICE_SLOW_SCOPE("albumsReady"); m_libraryVm.setRows(2, rows); });
    connect(m_engine, &LibraryEngine::playlistsReady, this, [this](const QList<ListRow>& rows) { m_libraryVm.setRows(0, rows); });
    connect(m_engine, &LibraryEngine::artistTagsReady, this, [this](int token, const QStringList& keys) {
        if (auto editor = m_tagJobs.take(token))
            editor->addTags(keys);
    });
    connect(m_engine, &LibraryEngine::searchReady, this, [this](const SearchResults& r, int token) { m_searchVm.setResults(r, token); });
    connect(m_engine, &LibraryEngine::requestReady, this, [this](const PlayRequest& request, int, qint64) {
        SEMPLICE_SLOW_SCOPE("requestReady");
        if (!m_playback)
            return;
        if (!m_playback->service.play(request))
            m_banners.error(QStringLiteral("cannot_play"));
    });
    connect(m_engine, &LibraryEngine::requestFailed, this, [this](int, qint64) { m_banners.error(QStringLiteral("cannot_play")); });

    // Home page and category page
    connect(m_engine, &LibraryEngine::homeReady, this, [this](const HomeData& d) { SEMPLICE_SLOW_SCOPE("homeReady"); m_homeVm.setHome(d); });
    connect(m_engine, &LibraryEngine::moreCategoriesReady, this, [this](const QList<CategoryContents>& c) { m_homeVm.addCategories(c); });
    connect(m_engine, &LibraryEngine::artistCardReady, this, [this](const ArtistCard& c) {
        m_artistCard.setCard(c);
        m_editor.fillFromArtist(c);
    });
    connect(m_engine, &LibraryEngine::albumCardReady, this, [this](const AlbumCard& c) {
        SEMPLICE_SLOW_SCOPE("albumCardReady");
        {
            SEMPLICE_SLOW_SCOPE("albumCardReady: album card");
            m_albumCard.setCard(c);
        }
        {
            SEMPLICE_SLOW_SCOPE("albumCardReady: editor");
            m_editor.fillFromAlbum(c);
        }
    });
    connect(m_engine, &LibraryEngine::playlistCardReady, this, [this](const PlaylistCard& c) {
        m_playlistCard.setCard(c);
        m_editor.fillFromPlaylist(c);
    });
    connect(m_engine, &LibraryEngine::deletePlanReady, this, [this](const DeleteSummary& plan) {
        if (!plan.ok) {
            m_banners.error(QStringLiteral("error_editing"));
            return;
        }
        emit deleteConfirmation(plan.op, plan.id, deleteTitle(plan.op), deleteText(plan));
    });
    connect(m_engine, &LibraryEngine::trackEditReady, this, [this](const TrackEdit& t) { m_editor.fillFromTrack(t);
        m_trackEditor.fillFromTrack(t);
    });
    connect(m_engine, &LibraryEngine::scanReady, this, [this](const ScanSummary& s) {
        if (EditorVm* owner = m_jobs.take(s.token))
            owner->applyScan(s);
    });
    connect(m_engine, &LibraryEngine::importProgress, this, [this](int token, const QString& phase, qint64 done, qint64 total) {
        if (token == m_rescanToken)
            m_settingsVm.setRescan(true, done, total);
        else if (EditorVm* owner = m_jobs.value(token))
            owner->setProgress(phase, done, total);
    });
    connect(m_engine, &LibraryEngine::mergeTargetFound, this, [this](int token, bool exists, bool hidden) {
        const PendingSave pending = m_mergeChecks.take(token);
        if (!pending.editor)
            return;
        if (!exists) {
            const int saveToken = ++m_nextToken;
            m_jobs.insert(saveToken, pending.editor);
            pauseForSave(saveToken);
            QMetaObject::invokeMethod(m_engine, "saveEntity", Qt::QueuedConnection, Q_ARG(int, saveToken), Q_ARG(QVariantMap, pending.command));
            return;
        }
        // the name is taken: the editor stops waiting and the owner decides
        pending.editor->saveFinished(false);
        m_mergeEditor = pending.editor;
        const QString name = pending.command.value(QStringLiteral("name")).toString().trimmed();
        emit mergeConfirmation(QCoreApplication::translate("semplice::AppController", "Merge the artists?"),
                               hidden ? QCoreApplication::translate("semplice::AppController", "\"%1\" is already the name of a hidden artist (it only appears on tracks). Their tracks and albums will be added to this artist and the other profile disappears.").arg(name)
                                      : QCoreApplication::translate("semplice::AppController", "There already is an artist called \"%1\". Both discographies will become one under this artist, and the other profile disappears.").arg(name));
    });
    connect(m_engine, &LibraryEngine::rescanFinished, this, [this](int, int code, int adopted, bool blocked, bool cancelled) {
        m_rescanToken = 0;
        m_settingsVm.setRescan(false);
        if (code != 0)
            m_banners.error(QStringLiteral("generic_error"), 0, QString::number(code));
        else if (blocked)
            m_banners.error(QStringLiteral("rescan_blocked"));
        else if (!cancelled)
            m_banners.info(QStringLiteral("rescan_done"), adopted);
    });
    connect(m_engine, &LibraryEngine::operationFinished, this, &AppController::onOperationFinished);
    connect(m_engine, &LibraryEngine::pickerReady, this, [this](const QList<ListRow>& rows, int token) { m_picker.setResults(rows, token); });
    connect(&m_picker, &PickerVm::queryRequested, this, [this](int kind, const QString& text, int token) {
        QMetaObject::invokeMethod(m_engine, "searchPicker", Qt::QueuedConnection, Q_ARG(int, kind), Q_ARG(QString, text), Q_ARG(int, token));
    });
    connect(m_engine, &LibraryEngine::cardFailed, this, [this](int page, qint64 id) {
        // The item is gone (deleted elsewhere, or a hidden artist that lost its last track). Its pages leave the history; only
        // when one of them is open does the user land on the previous page. A page of something else stays (a stale card
        // that reloads after a library change must not close the album you are looking at).
        if (page == 14) {
            m_trackEditor.clear();
            return;
        }
        m_nav.forget(static_cast<PageKind>(page), id);
        m_nav.forget(static_cast<PageKind>(page + 9), id);  // its edit page: 2 -> 11, 3 -> 12, 4 -> 13
    });
    connect(&m_artistCard, &ArtistCardVm::requested, this, [this](qint64 id) { QMetaObject::invokeMethod(m_engine, "loadArtistCard", Qt::QueuedConnection, Q_ARG(qint64, id)); });
    connect(&m_albumCard, &AlbumCardVm::requested, this, [this](qint64 id) { QMetaObject::invokeMethod(m_engine, "loadAlbumCard", Qt::QueuedConnection, Q_ARG(qint64, id)); });
    connect(&m_playlistCard, &PlaylistCardVm::requested, this, [this](qint64 id) { QMetaObject::invokeMethod(m_engine, "loadPlaylistCard", Qt::QueuedConnection, Q_ARG(qint64, id)); });
    connect(m_engine, &LibraryEngine::categoryReady, this, [this](const CategoryContents& c) { m_categoryVm.setContents(c); });
    connect(&m_homeVm, &HomeVm::homeRequested, this, [this](const QStringList& keys, bool recentOnly) {
        QMetaObject::invokeMethod(m_engine, "loadHome", Qt::QueuedConnection, Q_ARG(QStringList, keys), Q_ARG(bool, recentOnly));
    });
    connect(&m_homeVm, &HomeVm::moreRequested, this,
            [this](int n) { QMetaObject::invokeMethod(m_engine, "loadMoreCategories", Qt::QueuedConnection, Q_ARG(int, n)); });
    connect(&m_categoryVm, &CategoryVm::categoryRequested, this,
            [this](const QString& key) { QMetaObject::invokeMethod(m_engine, "loadCategory", Qt::QueuedConnection, Q_ARG(QString, key)); });
    auto carousel = [this](const QList<ContainerRef>& items, int index) { requestCarousel(items, index, false, QString(), QString()); };
    connect(&m_homeVm, &HomeVm::carouselPlayRequested, this, carousel);
    connect(&m_categoryVm, &CategoryVm::carouselPlayRequested, this, carousel);
    connect(&m_categoryVm, &CategoryVm::playAllRequested, this, [this](const QList<ContainerRef>& items, const QString& title, const QString& key) {
        requestCarousel(items, 0, true, title, key);
    });
    connect(&m_nav, &NavigationVm::pageChanged, this, [this] {
        if (m_nav.kind() == NavigationVm::Category)
            m_categoryVm.open(m_nav.pageText());
        else if (m_nav.kind() == NavigationVm::Artist)
            m_artistCard.open(m_nav.pageId());
        else if (m_nav.kind() == NavigationVm::Album)
            m_albumCard.open(m_nav.pageId());
        else if (m_nav.kind() == NavigationVm::Playlist)
            m_playlistCard.open(m_nav.pageId());
        else if (m_nav.kind() == NavigationVm::Share && m_nav.pageText() != QLatin1String("file"))   // "file": the content of a package, opened by openFile
            m_shareVm.open(m_nav.pageText() == QLatin1String("artist") ? 0 : m_nav.pageText() == QLatin1String("album") ? 1 : 2, m_nav.pageId());
        else if (m_nav.kind() == NavigationVm::Home)
            m_homeVm.refreshGreeting();
        onPageChangedForEditors();
    });

    // sharing: the Share page and the files of other users
    connect(&m_shareVm, &ShareVm::treeRequested, this, [this](int kind, qint64 id) {
        QMetaObject::invokeMethod(m_engine, "loadShareTree", Qt::QueuedConnection, Q_ARG(int, kind), Q_ARG(qint64, id));
    });
    connect(&m_shareVm, &ShareVm::fileRequested, this, [this](const QString& path, const QString& previewDir) {
        QMetaObject::invokeMethod(m_engine, "describeShareFile", Qt::QueuedConnection, Q_ARG(QString, path), Q_ARG(QString, previewDir));
    });
    connect(&m_shareVm, &ShareVm::importRequested, this, [this](const QString& path, const ImportSelection& selection) {
        const int token = ++m_nextToken;
        m_shareTokens.insert(token);
        QMetaObject::invokeMethod(m_engine, "importShare", Qt::QueuedConnection, Q_ARG(int, token), Q_ARG(QString, path), Q_ARG(semplice::ImportSelection, selection));
    });
    connect(&m_shareVm, &ShareVm::exportRequested, this, [this](const QString& path, const ShareSelection& selection) {
        const int token = ++m_nextToken;
        m_shareTokens.insert(token);
        QMetaObject::invokeMethod(m_engine, "exportShare", Qt::QueuedConnection, Q_ARG(int, token), Q_ARG(QString, path), Q_ARG(semplice::ShareSelection, selection));
    });
    connect(m_engine, &LibraryEngine::shareTreeReady, this, [this](const ShareTree& tree) { m_shareVm.setTree(tree); });
    connect(m_engine, &LibraryEngine::shareProgress, this, [this](int token, qint64 done, qint64 total) {
        if (m_shareTokens.contains(token))
            m_shareVm.setProgress(done, total);
    });
    connect(m_engine, &LibraryEngine::shareFinished, this, [this](int token, bool isImport, int code, int added, int skipped, int tracks, int playlists) {
        if (token != 0 && !m_shareTokens.remove(token))
            return;
        m_shareVm.finished();
        if (token == 0) {   // the file could not even be read
            m_banners.error(QStringLiteral("share_import_failed"), 0, QString::number(code));
            m_nav.back();
            return;
        }
        if (isImport && code == 0)
            m_nav.back();   // done: the page of the file is not needed any more
        if (code == static_cast<int>(ErrorCode::Cancelled))
            return;
        if (code != 0)
            m_banners.error(isImport ? QStringLiteral("share_import_failed") : QStringLiteral("share_export_failed"), 0, QString::number(code));
        else if (!isImport)
            m_banners.info(QStringLiteral("share_exported"));
        else if (tracks > 0 || playlists > 0 || added > 0)
            m_banners.info(QStringLiteral("share_imported"), tracks);
        else
            m_banners.info(QStringLiteral("share_nothing_new"), skipped);
    });

    // GUI -> engine
    connect(&m_libraryVm, &LibraryVm::loadRequested, this, &AppController::requestList);
    connect(&m_searchVm, &SearchVm::queryRequested, this,
            [this](const QString& text, int token) { QMetaObject::invokeMethod(m_engine, "search", Qt::QueuedConnection, Q_ARG(QString, text), Q_ARG(int, token)); });

    // a banner for every batch of unplayable tracks comes from the playback stack (see buildPlayback)
}

AppController::~AppController()
{
    shutdown();
    m_thread.quit();
    m_thread.wait();
}

void AppController::start()
{
    QMetaObject::invokeMethod(m_engine, "startup", Qt::QueuedConnection, Q_ARG(QString, m_settings.libraryPath()));
}

void AppController::shutdown()
{
    if (m_playback)
        m_playback->service.persist();
    tearDownPlayback();
    if (m_engine && m_thread.isRunning()) {
        // The library is closed on its own thread (its connection lives there). The GUI thread keeps handling events while
        // it waits: an operation that is running may be waiting for the GUI thread itself (a file the player holds).
        if (m_releaser)
            m_releaser->m_closing = true;
        std::atomic<bool> closed{false};
        QMetaObject::invokeMethod(m_engine, [this, &closed] {
            m_engine->closeLibrary();
            closed = true;
        }, Qt::QueuedConnection);
        while (!closed)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

void AppController::setStartup(Startup state)
{
    if (m_startup == state)
        return;
    m_startup = state;
    emit startupChanged();
}

void AppController::requestList(int tab, int sortKey, int direction, int typeMask)
{
    switch (tab) {
    case 1: QMetaObject::invokeMethod(m_engine, "loadArtists", Qt::QueuedConnection, Q_ARG(int, sortKey), Q_ARG(int, direction), Q_ARG(bool, typeMask != 0)); break;  // for artists the mask says "hidden ones only"
    case 2:
        QMetaObject::invokeMethod(m_engine, "loadAlbums", Qt::QueuedConnection, Q_ARG(int, sortKey), Q_ARG(int, direction), Q_ARG(int, typeMask));
        break;
    default: QMetaObject::invokeMethod(m_engine, "loadPlaylists", Qt::QueuedConnection, Q_ARG(int, sortKey), Q_ARG(int, direction)); break;
    }
}

// ------------------------------------------------------------------------------------------ library lifecycle

void AppController::selectFolder(const QUrl& folder)
{
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    QMetaObject::invokeMethod(m_engine, "selectFolder", Qt::QueuedConnection, Q_ARG(QString, path));
}

void AppController::onOpened(const QString& root, const QString& dbPath, bool backupCreated, bool repaired)
{
    Q_UNUSED(backupCreated)
    m_libraryPath = root;
    m_shareVm.setLibraryRoot(root);
    m_settings.setLibraryPath(root);
    m_settingsVm.setLibraryPath(root);
    m_coverProvider->setLibraryRoot(root);
    m_artistCard.setLibraryRoot(root);
    m_albumCard.setLibraryRoot(root);
    m_playlistCard.setLibraryRoot(root);
    m_editor.setLibraryRoot(root);
    m_artistEditor.setLibraryRoot(root);
    buildPlayback(root, dbPath);
    QThreadPool::globalInstance()->start([folder = root + QStringLiteral("/.library/covers")] { ThumbnailCache::trim(folder); });
    m_everReady = true;
    m_nav.home();
    setStartup(Ready);
    m_libraryVm.setReady(true);
    m_homeVm.refresh(false);
    if (repaired)
        m_banners.info(QStringLiteral("library_repaired"));
    if (m_playback)
        m_playback->service.restore();
}

void AppController::onOpenFailed(int code, const QString& message, const QString& path)
{
    SLOG_WARN(QStringLiteral("library %1 not opened: %2 %3").arg(path).arg(code).arg(message));
    switch (static_cast<ErrorCode>(code)) {
    case ErrorCode::FolderNotEmpty: m_banners.error(QStringLiteral("folder_not_empty")); break;
    case ErrorCode::NotFound: m_banners.error(QStringLiteral("folder_missing")); break;
    case ErrorCode::SchemaTooNew: m_banners.error(QStringLiteral("schema_too_new")); break;
    default: m_banners.error(QStringLiteral("library_unusable"), 0, QString::number(code)); break;
    }
    if (!m_everReady && m_startup == Loading) {
        m_libraryPath = path;
        m_shareVm.setLibraryRoot(path);
        setStartup(Failed);
    }
}

void AppController::onClosed()
{
    if (m_playback)
        m_playback->service.persist();
    tearDownPlayback();
    m_libraryVm.setReady(false);
    m_homeVm.reset();
    m_categoryVm.reset();
    m_artistCard.reset();
    m_albumCard.reset();
    m_playlistCard.reset();
    m_trackEditor.clear();
    m_editor.clear();
    m_artistEditor.clear();
    m_jobs.clear();
    m_searchVm.clearResults();
}

void AppController::onLibraryChanged(const ChangeSet& changes)
{
    QElapsedTimer timer;
    timer.start();
    struct Report {
        QElapsedTimer& t;
        ~Report()
        {
            if (t.elapsed() > 30)
                SLOG_WARN(QStringLiteral("handling a library change took %1 ms on the GUI thread").arg(t.elapsed()));
        }
    } report{timer};
    m_libraryVm.invalidate();
    m_homeVm.refresh(false);
    if (!m_categoryVm.key().isEmpty())
        m_categoryVm.reload();
    m_artistCard.reload();
    m_albumCard.reload();
    m_playlistCard.reload();
    m_editor.refreshRows();
    if (m_playback)
        m_playback->recommender.refresh();  // the pool is read again in the background
    if (m_playback && !changes.tracks.isEmpty())
        m_playback->service.revalidate(changes.tracks);
    m_searchVm.refresh();
}

// ------------------------------------------------------------------------------------------ playback

void AppController::buildPlayback(const QString& root, const QString& dbPath)
{
    tearDownPlayback();
    auto db = Database::open(dbPath);
    if (!db.ok()) {
        SLOG_ERROR(QStringLiteral("playback database: ") + db.error().devMessage);
        m_banners.error(QStringLiteral("generic_error"));
        return;
    }
    m_playback = std::make_unique<PlaybackStack>(std::move(db).value(), root, dbPath, m_genres, m_backend, m_settings);
    m_requests = std::make_unique<RequestWorker>(dbPath);
    PlaybackService& service = m_playback->service;
    connect(&m_playback->store, &AsyncPlaybackStore::recentWritten, this, [this] { m_homeVm.refresh(true); });  // after the write is on disk
    connect(&service, &PlaybackService::currentChanged, &m_tagSyncTimer, QOverload<>::of(&QTimer::start));
    connect(&service, &PlaybackService::tracksSkipped, this, [this](int n) { m_banners.info(QStringLiteral("tracks_skipped"), n); });
    m_playerVm.attach(&service);
    m_queueVm.attach(&service, &m_playback->catalog);
    rebuildSystemIntegration();
}

void AppController::tearDownPlayback()
{
    m_systemIntegration.reset();
    m_playerVm.attach(nullptr);
    m_queueVm.attach(nullptr, nullptr);
    if (m_playback)
        m_playback->service.stop();
    m_requests.reset();
    m_playback.reset();
}

void AppController::attachWindow(QQuickWindow* window)
{
    if (m_windowAttached || !window)
        return;
    m_windowAttached = true;
    // A window that "does not respond" can also be waiting for the render thread: say in the log when a frame is slow.
    {
        auto* syncClock = new QElapsedTimer;
        auto* renderClock = new QElapsedTimer;
        QObject::connect(window, &QQuickWindow::beforeSynchronizing, window, [syncClock] { syncClock->start(); }, Qt::DirectConnection);
        QObject::connect(window, &QQuickWindow::afterSynchronizing, window, [syncClock] {
            if (syncClock->isValid() && syncClock->elapsed() > 60)
                SLOG_WARN(QStringLiteral("scene graph: synchronising a frame took %1 ms (the window waits for it)").arg(syncClock->elapsed()));
        }, Qt::DirectConnection);
        QObject::connect(window, &QQuickWindow::beforeRendering, window, [renderClock] { renderClock->start(); }, Qt::DirectConnection);
        QObject::connect(window, &QQuickWindow::afterRenderPassRecording, window, [renderClock] {
            if (renderClock->isValid() && renderClock->elapsed() > 60)
                SLOG_WARN(QStringLiteral("scene graph: recording a frame took %1 ms").arg(renderClock->elapsed()));
        }, Qt::DirectConnection);
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [renderClock] {
            if (renderClock->isValid() && renderClock->elapsed() > 100)
                SLOG_WARN(QStringLiteral("scene graph: a frame took %1 ms from start to swap").arg(renderClock->elapsed()));
        }, Qt::DirectConnection);
    }
    const quintptr hwnd = static_cast<quintptr>(window->winId());
    if (qEnvironmentVariableIsSet("SEMPLICE_NO_SMTC")) {
    } else if (!m_smtc.attach(hwnd))
        SLOG_WARN(QStringLiteral("System media controls are not available"));
    if (!qEnvironmentVariableIsSet("SEMPLICE_NO_THUMBS"))
        m_thumbs.attach(hwnd);
    rebuildSystemIntegration();
}

void AppController::rebuildSystemIntegration()
{
    m_systemIntegration.reset();
    if (!m_playback || !m_windowAttached)
        return;
    m_systemIntegration = std::make_unique<SystemIntegration>(m_playback->service, &m_smtc, &m_thumbs);
}

// ------------------------------------------------------------------------------------------ actions from QML

void AppController::setPopup(const QString& name)
{
    // the queue and search popups are never open together; any other interaction closes them
    if (m_popup == name)
        return;
    m_popup = name;
    emit popupChanged();
}

void AppController::play(int kind, qint64 id)
{
    runRequest([kind, id](PlayRequestBuilder& builder) -> Result<PlayRequest> {
        switch (static_cast<PlayKind>(kind)) {
        case PlayKind::Artist: return builder.discography(ArtistId(id));
        case PlayKind::HiddenArtist: return builder.hiddenArtist(ArtistId(id));
        case PlayKind::Album: return builder.album(AlbumId(id));
        case PlayKind::Playlist: return builder.playlist(PlaylistId(id));
        case PlayKind::Track: return builder.singleTrack(TrackId(id));
        }
        return Error::make(ErrorCode::Unknown);
    });
}

void AppController::runRequest(std::function<Result<PlayRequest>(PlayRequestBuilder&)> build)
{
    if (!m_requests) {
        m_banners.error(QStringLiteral("cannot_play"));
        return;
    }
    RequestWorker* worker = m_requests.get();
    QMetaObject::invokeMethod(&worker->context, [this, worker, build = std::move(build)] {
        Result<PlayRequest> result = Error::make(ErrorCode::Unknown);
        if (worker->db) {
            PlayRequestBuilder builder(*worker->db);
            result = build(builder);
        }
        QMetaObject::invokeMethod(this, [this, result = std::move(result)] {
            SEMPLICE_SLOW_SCOPE("requestReady");
            if (!m_playback)
                return;
            if (!result.ok() || !m_playback->service.play(result.value()))
                m_banners.error(QStringLiteral("cannot_play"));
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void AppController::open(int pageKind, qint64 id, const QString& text)
{
    closePopups();
    m_nav.open(pageKind, id, text);
}

}  // namespace semplice

namespace semplice {

void AppController::requestCarousel(const QList<ContainerRef>& items, int index, bool all, const QString& title, const QString& key)
{
    runRequest([items, index, all, title, key](PlayRequestBuilder& builder) {
        return all ? builder.carouselAll(items, title, key) : builder.carouselFrom(items, index);
    });
}

}  // namespace semplice

namespace semplice {

void AppController::playFrom(int kind, qint64 id, int index)
{
    runRequest([kind, id, index](PlayRequestBuilder& builder) -> Result<PlayRequest> {
        Result<PlayRequest> result = Error::make(ErrorCode::Unknown);
        switch (static_cast<PlayKind>(kind)) {
        case PlayKind::Album: result = builder.album(AlbumId(id)); break;
        case PlayKind::Playlist: result = builder.playlist(PlaylistId(id)); break;
        case PlayKind::HiddenArtist: result = builder.hiddenArtist(ArtistId(id)); break;
        default: break;
        }
        if (result.ok())
            result.value().startIndex = index;
        return result;
    });
}

void AppController::operate(int operation, qint64 a, qint64 b, const QString& text)
{
    const int token = ++m_nextToken;
    QMetaObject::invokeMethod(m_engine, "runOperation", Qt::QueuedConnection, Q_ARG(int, token), Q_ARG(int, operation), Q_ARG(qint64, a),
                              Q_ARG(qint64, b), Q_ARG(QString, text));
}

void AppController::onOperationFinished(const OperationResult& result)
{
    SEMPLICE_SLOW_SCOPE("AppController::onOperationFinished");
    resumeAfterSave(result.token);
    if (EditorVm* owner = m_jobs.take(result.token)) {
        finishSave(*owner, result);
        return;
    }
    if (result.code != 0) {
        SLOG_WARN(QStringLiteral("operation failed: %1 %2").arg(result.code).arg(result.message));
        switch (static_cast<ErrorCode>(result.code)) {
        case ErrorCode::AlreadyExists: m_banners.error(QStringLiteral("already_exists")); break;
        case ErrorCode::NotAllowed: m_banners.error(QStringLiteral("not_allowed")); break;
        default: m_banners.error(QStringLiteral("error_editing")); break;
        }
        return;
    }
    // the page of something that was deleted (and its editor) is gone from the history
    switch (result.op) {
    case 4: m_nav.forget(PageKind::EditTrack, result.id); break;
    case 5: m_nav.forget(PageKind::Album, result.id); m_nav.forget(PageKind::EditAlbum, result.id); break;
    case 6: m_nav.forget(PageKind::Artist, result.id); m_nav.forget(PageKind::EditArtist, result.id); break;
    case 7: m_nav.forget(PageKind::Playlist, result.id); m_nav.forget(PageKind::EditPlaylist, result.id); break;
    default: break;
    }
    if (result.createdId > 0) {
        // a playlist made from a track opens; the album made for "Add to Discography" does not
        // (the menu that started it knows what it wants: see the QML)
        emit operationCreated(result.token, result.createdId);
    }
}

}  // namespace semplice

namespace semplice {

void AppController::startPlaylistWithTrack(qint64 trackId, const QString& name)
{
    m_pendingTrack = trackId;
    open(NavigationVm::AddPlaylist, 0, name);
}

// ------------------------------------------------------------------------------------------ editors

void AppController::wireEditor(EditorVm& editor)
{
    EditorVm* e = &editor;
    connect(e, &EditorVm::loadRequested, this, [this](const QString& entity, qint64 id) {
        const char* slot = entity == QLatin1String("artist")     ? "loadArtistCard"
                           : entity == QLatin1String("album")    ? "loadAlbumCard"
                           : entity == QLatin1String("playlist") ? "loadPlaylistCard"
                                                                 : "loadTrackEdit";
        QMetaObject::invokeMethod(m_engine, slot, Qt::QueuedConnection, Q_ARG(qint64, id));
    });
    connect(e, &EditorVm::scanRequested, this, [this, e](const QStringList& paths, qint64 preselected, bool reset) {
        const int token = ++m_nextToken;
        m_jobs.insert(token, e);
        QMetaObject::invokeMethod(m_engine, "scanFiles", Qt::QueuedConnection, Q_ARG(int, token), Q_ARG(QStringList, paths),
                                  Q_ARG(qint64, preselected), Q_ARG(bool, reset));
    });
    connect(e, &EditorVm::saveRequested, this, [this, e](const QVariantMap& command) {
        const int token = ++m_nextToken;
        // renaming an artist to a name that is taken merges the two: ask first
        if (command.value(QStringLiteral("type")).toString() == QLatin1String("artist") && command.value(QStringLiteral("mode")).toString() == QLatin1String("edit") && !m_mergeApproved) {
            m_mergeChecks.insert(token, {e, command});
            QMetaObject::invokeMethod(m_engine, "findMergeTarget", Qt::QueuedConnection, Q_ARG(int, token),
                                      Q_ARG(QString, command.value(QStringLiteral("name")).toString()), Q_ARG(qint64, command.value(QStringLiteral("id")).toLongLong()));
            return;
        }
        m_mergeApproved = false;
        m_jobs.insert(token, e);
        pauseForSave(token);
        QMetaObject::invokeMethod(m_engine, "saveEntity", Qt::QueuedConnection, Q_ARG(int, token), Q_ARG(QVariantMap, command));
    });
    connect(e, &EditorVm::artistTagsRequested, this, [this, e](const QVariantList& ids) {
        const int token = ++m_nextToken;
        m_tagJobs.insert(token, e);
        QMetaObject::invokeMethod(m_engine, "loadArtistTags", Qt::QueuedConnection, Q_ARG(int, token), Q_ARG(QVariantList, ids));
    });
    connect(e, &EditorVm::stopRequested, this, [this] { m_engine->requestStop(); });
}

namespace {
QString entityOf(int kind)
{
    switch (kind) {
    case NavigationVm::AddArtist:
    case NavigationVm::EditArtist: return QStringLiteral("artist");
    case NavigationVm::AddAlbum:
    case NavigationVm::EditAlbum: return QStringLiteral("album");
    case NavigationVm::AddPlaylist:
    case NavigationVm::EditPlaylist: return QStringLiteral("playlist");
    case NavigationVm::EditTrack: return QStringLiteral("track");
    default: return {};
    }
}
bool isEdit(int kind)
{
    return kind == NavigationVm::EditArtist || kind == NavigationVm::EditAlbum || kind == NavigationVm::EditPlaylist || kind == NavigationVm::EditTrack;
}
}  // namespace

// An editor page appears when the user opens one and stays alive while a nested "Add artist" window is on top of it;
// any other page drops the drafts.
void AppController::onPageChangedForEditors()
{
    m_trackEditor.clear();
    const int kind = m_nav.kind();
    const QString entity = entityOf(kind);
    if (entity.isEmpty()) {
        if (!m_editor.entity().isEmpty() || !m_artistEditor.entity().isEmpty()) {
            m_editor.clear();
            m_artistEditor.clear();
            m_jobs.clear();
            QMetaObject::invokeMethod(m_engine, "clearScan", Qt::QueuedConnection);
        }
        return;
    }
    if (kind == NavigationVm::AddArtist) {
        if (m_artistEditor.entity().isEmpty()) {
            m_artistEditor.beginAdd(entity, m_nav.pageText());
            // an artist made for a track stays hidden; one made for an album is a visible artist (SPEC 8.3)
            m_artistEditor.setHiddenArtist(m_editor.entity() == QLatin1String("track"));
        }
        return;
    }
    m_artistEditor.clear();
    const bool same = m_editor.entity() == entity && m_editor.editing() == isEdit(kind) && (!isEdit(kind) || m_editor.id() == m_nav.pageId());
    if (same)
        return;  // coming back from the nested window
    m_editor.clear();
    QMetaObject::invokeMethod(m_engine, "clearScan", Qt::QueuedConnection);
    if (isEdit(kind)) {
        m_editor.beginEdit(entity, m_nav.pageId());
    } else if (kind == NavigationVm::AddPlaylist && m_pendingTrack > 0) {
        m_editor.beginAddWithTrack(m_pendingTrack, m_nav.pageText());
        m_pendingTrack = 0;
    } else {
        m_editor.beginAdd(entity, m_nav.pageText());
    }
}

// Saving may make the window unresponsive for a moment (and with it the sound can break up): the music waits for the save.
void AppController::pauseForSave(int token)
{
    m_saveTokens.insert(token);
    m_savePauseTimer.start();  // a quick save does not interrupt the music; one that takes longer does
}

void AppController::resumeAfterSave(int token)
{
    if (!m_saveTokens.remove(token) || !m_saveTokens.isEmpty())
        return;
    m_savePauseTimer.stop();
    if (!m_pausedForSave)
        return;
    m_pausedForSave = false;
    if (m_playback && m_playback->service.state() == PlayState::Paused)  // not when the owner took over meanwhile
        m_playback->service.play();
}

void AppController::finishSave(EditorVm& owner, const OperationResult& result)
{
    SEMPLICE_SLOW_SCOPE("AppController::finishSave");
    SLOG_INFO(QStringLiteral("a save finished (code %1)").arg(result.code));
    owner.saveFinished(result.code == 0);
    if (&owner == &m_trackEditor) {
        if (result.code == 0)
            m_trackEditor.clear();  // the row goes back to normal; the lists reload by themselves
        else
            m_banners.error(QStringLiteral("error_editing"));
        return;
    }
    if (result.code != 0) {
        SLOG_WARN(QStringLiteral("save failed: %1 %2").arg(result.code).arg(result.message));
        switch (static_cast<ErrorCode>(result.code)) {
        case ErrorCode::AlreadyExists: m_banners.error(QStringLiteral("already_exists")); break;
        case ErrorCode::Cancelled: break;
        case ErrorCode::NotAllowed: m_banners.error(QStringLiteral("not_allowed")); break;
        default: m_banners.error(QStringLiteral("error_editing")); break;
        }
        return;
    }
    if (&owner == &m_artistEditor && !m_editor.entity().isEmpty()) {
        // the artist made from inside another editor joins it
        m_editor.addAuthor(result.id, m_artistEditor.title().trimmed(), m_artistEditor.hiddenArtist());
        m_artistEditor.clear();
        m_nav.back();
        return;
    }
    const bool added = !owner.editing();
    const QString entity = owner.entity();
    m_nav.back();
    if (added) {
        const int kind = entity == QLatin1String("artist") ? NavigationVm::Artist : entity == QLatin1String("album") ? NavigationVm::Album : NavigationVm::Playlist;
        m_nav.open(kind, result.id);
    }
}

}  // namespace semplice

namespace semplice {

void AppController::requestDelete(int operation, qint64 id)
{
    QMetaObject::invokeMethod(m_engine, "planDelete", Qt::QueuedConnection, Q_ARG(int, operation), Q_ARG(qint64, id));
}

}  // namespace semplice

namespace semplice {

// Where the files go depends on the page they were dropped on: an open artist gets a new album with that artist
// preselected, an open album / playlist gets the files appended, an open Add window takes them, anything else
// opens Add album (it turns into Add playlist when the files do not look like one album).
void AppController::importShareFile(const QUrl& file)
{
    if (!file.isLocalFile() || m_startup != Ready)
        return;
    closePopups();
    m_shareVm.openFile(file);   // the page shows what is in the file; the owner ticks what to take in
    m_nav.open(NavigationVm::Share, 0, QStringLiteral("file"));
}

void AppController::dropFiles(const QVariantList& allUrls)
{
    if (allUrls.isEmpty() || m_startup != Ready)
        return;
    // a share package (*.smpl) is not music: it is imported as it is
    {
        QVariantList rest;
        bool any = false;
        for (const QVariant& v : allUrls) {
            const QUrl u = v.toUrl();
            if (u.isLocalFile() && u.toLocalFile().endsWith(QStringLiteral(".smpl"), Qt::CaseInsensitive)) {
                importShareFile(u);
                any = true;
            } else {
                rest.append(v);
            }
        }
        if (any && rest.isEmpty())
            return;
        if (any)
            return dropFiles(rest);
    }
    // pictures go to the window that is open (a cover for an album / playlist, an avatar for an artist);
    // everything else is music
    static const QStringList pictureTypes{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("webp"),
                                          QStringLiteral("bmp"), QStringLiteral("gif")};
    QVariantList urls;
    QUrl picture;
    for (const QVariant& v : allUrls) {
        const QUrl u = v.toUrl();
        const QFileInfo info(u.isLocalFile() ? u.toLocalFile() : u.toString());
        if (info.isFile() && pictureTypes.contains(info.suffix().toLower())) {
            if (picture.isEmpty())
                picture = u;
        } else {
            urls.append(v);
        }
    }
    const int kind = m_nav.kind();
    const qint64 id = m_nav.pageId();
    const bool editorOpen = kind == NavigationVm::AddArtist || kind == NavigationVm::AddAlbum || kind == NavigationVm::AddPlaylist
                            || kind == NavigationVm::EditArtist || kind == NavigationVm::EditAlbum || kind == NavigationVm::EditPlaylist;
    if (!picture.isEmpty() && editorOpen && urls.isEmpty()) {
        (kind == NavigationVm::AddArtist ? m_artistEditor : m_editor).choosePicture(picture);
        return;
    }
    if (urls.isEmpty()) {
        // only a picture and no window to put it in: a new album that has it as its cover
        open(NavigationVm::AddAlbum, 0);
        m_editor.choosePicture(picture);
        return;
    }
    switch (kind) {
    case NavigationVm::AddAlbum:
    case NavigationVm::AddPlaylist:
    case NavigationVm::EditAlbum:
    case NavigationVm::EditPlaylist: break;
    case NavigationVm::Artist:
        open(NavigationVm::AddAlbum, 0);
        m_editor.setPreselectedArtist(id);
        break;
    case NavigationVm::Album: open(NavigationVm::EditAlbum, id); break;
    case NavigationVm::Playlist: open(NavigationVm::EditPlaylist, id); break;
    default: open(NavigationVm::AddAlbum, 0); break;
    }
    if (!picture.isEmpty())
        m_editor.choosePicture(picture);  // music and a picture together: the picture is the cover
    m_editor.addFiles(urls);
}

}  // namespace semplice

namespace semplice {

void AppController::editTrack(qint64 trackId)
{
    m_trackEditor.beginEdit(QStringLiteral("track"), trackId);
}

}  // namespace semplice

namespace semplice {

void AppController::confirmMerge()
{
    if (EditorVm* editor = std::exchange(m_mergeEditor, nullptr)) {
        m_mergeApproved = true;
        editor->save();  // goes through saveRequested again, now without the question
    }
}

}  // namespace semplice

namespace semplice {

void AppController::devSaveAlbum(qint64 albumId)
{
    open(NavigationVm::EditAlbum, albumId);
    QTimer::singleShot(1500, this, [this] {
        SLOG_INFO(QStringLiteral("dev: editor year %1, title %2").arg(m_editor.year()).arg(m_editor.title()));
        m_editor.setDate(m_editor.year() == 2020 ? 2021 : 2020, m_editor.month(), m_editor.day());
        if (qEnvironmentVariableIsSet("SEMPLICE_DEV_RENAME")) {
            const QString t = m_editor.title();
            m_editor.setTitle(t.endsWith(QLatin1String(" X")) ? t.chopped(2) : t + QStringLiteral(" X"));  // the album folder is renamed
        }
        SLOG_INFO(QStringLiteral("dev: saving the album now"));
        m_editor.save();
    });
}

}  // namespace semplice
