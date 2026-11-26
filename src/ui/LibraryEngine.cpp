#include "ui/LibraryEngine.h"

#include "core/browse/PlayRequestBuilder.h"
#include "core/storage/ITrash.h"
#include "core/util/Logger.h"
#include "platform/WindowsAtomicReplace.h"
#include "platform/WindowsCodepageConverter.h"
#include "platform/WindowsFileAttributes.h"

#include "core/db/ArtistRepo.h"

#include <QDir>
#include <QFileInfo>

namespace semplice {

struct LibraryEngine::Platform {
    QtTrash trash;
    platform::WindowsAtomicReplace replace;
    platform::WindowsCodepageConverter codepages;
    SystemClock clock;
    platform::WindowsFileAttributes attributes;
};

struct LibraryEngine::HomeState {
    explicit HomeState(Database& db) : queries(db), generator(queries, std::random_device{}()) {}
    HomeQueries queries;
    CategoryGenerator generator;
    std::mt19937_64 rng{std::random_device{}()};
};

LibraryEngine::LibraryEngine(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<semplice::ListRow>();
    qRegisterMetaType<semplice::ShareTree>();
    qRegisterMetaType<semplice::ShareSelection>();
    qRegisterMetaType<semplice::ImportSelection>();
    qRegisterMetaType<QList<semplice::ListRow>>();
    qRegisterMetaType<semplice::SearchResults>();
    qRegisterMetaType<semplice::PlayRequest>();
    qRegisterMetaType<semplice::ChangeSet>();
    qRegisterMetaType<semplice::CarouselItem>();
    qRegisterMetaType<semplice::CategoryContents>();
    qRegisterMetaType<QList<semplice::CategoryContents>>();
    qRegisterMetaType<semplice::HomeData>();
    qRegisterMetaType<semplice::ContainerRef>();
    qRegisterMetaType<semplice::ArtistCard>();
    qRegisterMetaType<semplice::AlbumCard>();
    qRegisterMetaType<semplice::PlaylistCard>();
    qRegisterMetaType<semplice::ArtistRef>();
    qRegisterMetaType<QList<semplice::ArtistRef>>();
    qRegisterMetaType<semplice::OperationResult>();
    qRegisterMetaType<semplice::ScanSummary>();
    qRegisterMetaType<semplice::TrackEdit>();
    qRegisterMetaType<semplice::DeleteSummary>();
    qRegisterMetaType<QVariantMap>();
    qRegisterMetaType<QList<semplice::ContainerRef>>();
}

LibraryEngine::~LibraryEngine() = default;

SessionDependencies LibraryEngine::dependencies()
{
    if (!m_platform)
        m_platform = std::make_unique<Platform>();
    return SessionDependencies{m_platform->trash, m_platform->replace, m_platform->codepages, m_platform->clock,
                               m_platform->attributes, m_releaser, nullptr};
}

void LibraryEngine::adopt(std::unique_ptr<LibrarySession> session)
{
    m_home.reset();
    m_session = std::move(session);
    m_syncGaveUp.clear();
    connect(&m_session->library(), &LibraryService::libraryChanged, this, &LibraryEngine::libraryChanged);
    emit opened(m_session->layout().root, m_session->layout().dbPath, m_session->backupCreated(),
                m_session->recovery().repairedAnything());
}

void LibraryEngine::startup(const QString& storedPath)
{
    if (storedPath.isEmpty()) {
        emit noLibraryYet();
        return;
    }
    if (LibrarySwitcher::classify(storedPath) == FolderState::Missing) {
        emit pathMissing(storedPath);
        return;
    }
    auto session = LibrarySession::open(storedPath, dependencies());
    if (!session.ok()) {
        SLOG_ERROR(QStringLiteral("cannot open library %1: %2 %3").arg(storedPath, session.error().symbol(), session.error().devMessage));
        emit openFailed(session.error().numericCode(), session.error().devMessage, storedPath);
        return;
    }
    adopt(std::move(session).value());
}

void LibraryEngine::selectFolder(const QString& path)
{
    const QString clean = QDir::cleanPath(path);
    const FolderState state = LibrarySwitcher::classify(clean);
    if (state == FolderState::Other) {
        emit openFailed(static_cast<int>(ErrorCode::FolderNotEmpty), clean, clean);
        return;
    }
    if (state == FolderState::Missing) {
        emit openFailed(static_cast<int>(ErrorCode::NotFound), clean, clean);
        return;
    }
    // Close the current library first: two sessions never exist at once (TECH_DESIGN 1.3).
    if (m_session) {
        m_home.reset();
        m_session.reset();
        emit closed();
    }
    auto session = state == FolderState::Library ? LibrarySession::open(clean, dependencies()) : LibrarySession::create(clean, dependencies());
    if (!session.ok()) {
        SLOG_ERROR(QStringLiteral("cannot open library %1: %2 %3").arg(clean, session.error().symbol(), session.error().devMessage));
        emit openFailed(session.error().numericCode(), session.error().devMessage, clean);
        return;
    }
    adopt(std::move(session).value());
}

void LibraryEngine::closeLibrary()
{
    m_home.reset();
    if (m_session) {
        m_session.reset();
        emit closed();
    }
}

namespace {
QList<ListRow> toRows(const QList<ArtistRow>& in)
{
    QList<ListRow> out;
    out.reserve(in.size());
    for (const ArtistRow& r : in)
        {
        ListRow row{r.id.value, r.name, {}, r.coverPath, 0, 0, 0};
        row.dimmed = r.userHidden;
        out.append(row);
    }
    return out;
}
QList<ListRow> toRows(const QList<AlbumRow>& in)
{
    QList<ListRow> out;
    out.reserve(in.size());
    for (const AlbumRow& r : in)
        out.append({r.id.value, r.title, r.artists, r.coverPath, r.primaryArtist.value, r.year, static_cast<int>(r.type)});
    return out;
}
QList<ListRow> toRows(const QList<PlaylistRow>& in)
{
    QList<ListRow> out;
    out.reserve(in.size());
    for (const PlaylistRow& r : in)
        out.append({r.id.value, r.name, {}, r.coverPath, 0, 0, 0});
    return out;
}
LibrarySort sortOf(int key, int direction)
{
    return {static_cast<SortKey>(key), direction == 0 ? SortDirection::Ascending : SortDirection::Descending};
}
}  // namespace

void LibraryEngine::loadArtists(int sortKey, int direction, bool hiddenOnly)
{
    if (!m_session)
        return;
    auto rows = LibraryQueries(m_session->database()).artists(sortOf(sortKey, direction), hiddenOnly);
    if (rows.ok())
        emit artistsReady(toRows(rows.value()));
}

void LibraryEngine::loadAlbums(int sortKey, int direction, int typeMask)
{
    if (!m_session)
        return;
    QSet<AlbumType> types;
    for (int t = 0; t <= static_cast<int>(AlbumType::Other); ++t)
        if (typeMask & (1 << t))
            types.insert(static_cast<AlbumType>(t));
    auto rows = LibraryQueries(m_session->database()).albums(sortOf(sortKey, direction), types);
    if (rows.ok())
        emit albumsReady(toRows(rows.value()));
}

void LibraryEngine::loadPlaylists(int sortKey, int direction)
{
    if (!m_session)
        return;
    auto rows = LibraryQueries(m_session->database()).playlists(sortOf(sortKey, direction));
    if (rows.ok())
        emit playlistsReady(toRows(rows.value()));
}

void LibraryEngine::search(const QString& text, int token)
{
    if (!m_session)
        return;
    // the full result lists are fetched (limit = everything) so "Show more" needs no second trip
    emit searchReady(m_session->search().search(text, 100000), token);
}

void LibraryEngine::buildRequest(int kind, qint64 id)
{
    if (!m_session) {
        emit requestFailed(kind, id);
        return;
    }
    PlayRequestBuilder builder(m_session->database());
    Result<PlayRequest> result = Error::make(ErrorCode::Unknown);
    switch (static_cast<PlayKind>(kind)) {
    case PlayKind::Artist: result = builder.discography(ArtistId(id)); break;
    case PlayKind::HiddenArtist: result = builder.hiddenArtist(ArtistId(id)); break;
    case PlayKind::Album: result = builder.album(AlbumId(id)); break;
    case PlayKind::Playlist: result = builder.playlist(PlaylistId(id)); break;
    case PlayKind::Track: result = builder.singleTrack(TrackId(id)); break;
    }
    if (result.ok())
        emit requestReady(result.value(), kind, id);
    else
        emit requestFailed(kind, id);
}

}  // namespace semplice

namespace semplice {

namespace {

int minimumItems(CategoryType type)
{
    switch (type) {
    case CategoryType::ArtistDiscography: return CategoryGenerator::kMinArtistAlbums;
    case CategoryType::YourPlaylists: return CategoryGenerator::kMinPlaylists;
    default: return CategoryGenerator::kMinAlbums;
    }
}

}  // namespace

void LibraryEngine::loadHome(const QStringList& knownKeys, bool recentOnly)
{
    if (!m_session)
        return;
    if (!m_home)
        m_home = std::make_unique<HomeState>(m_session->database());
    HomeData data;
    data.recentOnly = recentOnly;
    if (auto has = m_home->queries.hasContent(); has.ok())
        data.hasContent = has.value();
    if (auto recent = m_home->queries.recent(); recent.ok())
        data.recent = recent.value();
    if (recentOnly) {  // something was played: only the Recently Played carousel changes
        emit homeReady(data);
        return;
    }
    QList<CategorySpec> specs;
    for (const QString& key : knownKeys)
        if (auto spec = CategorySpec::fromKey(key))
            specs.append(*spec);
    if (specs.isEmpty())
        specs = m_home->generator.next(CategoryGenerator::kInitialCount);
    for (const CategorySpec& spec : std::as_const(specs)) {
        auto contents = m_home->queries.contents(spec, CategoryGenerator::kMaxItems, &m_home->rng);
        if (contents.ok() && contents.value().items.size() >= minimumItems(spec.type)) {
            if (spec.type == CategoryType::ArtistDiscography && spec.label.isEmpty()) {
                // a known key carries no label: ask for the artist name
                ArtistRepo artists(m_session->database());
                if (auto a = artists.get(ArtistId(spec.arg.toLongLong())); a.ok() && a.value())
                    contents.value().spec.label = a.value()->name;
            } else {
                contents.value().spec.label = spec.label;
            }
            data.categories.append(contents.value());
        }
    }
    emit homeReady(data);
}

void LibraryEngine::loadMoreCategories(int count)
{
    if (!m_session)
        return;
    if (!m_home)
        m_home = std::make_unique<HomeState>(m_session->database());
    QList<CategoryContents> out;
    for (const CategorySpec& spec : m_home->generator.next(count)) {
        auto contents = m_home->queries.contents(spec, CategoryGenerator::kMaxItems, &m_home->rng);
        if (contents.ok() && !contents.value().items.isEmpty()) {
            contents.value().spec.label = spec.label;
            out.append(contents.value());
        }
    }
    emit moreCategoriesReady(out);
}

void LibraryEngine::loadCategory(const QString& key)
{
    if (!m_session)
        return;
    if (!m_home)
        m_home = std::make_unique<HomeState>(m_session->database());
    CategoryContents contents;
    if (key == QLatin1String("recent")) {
        auto recent = m_home->queries.recent();
        if (!recent.ok())
            return;
        contents.items = recent.value();
        // statistics of the recent list
        for (const CarouselItem& item : std::as_const(contents.items)) {
            const QString sql = item.kind == ContainerKind::Album
                                    ? QStringLiteral("SELECT COALESCE(SUM(t.duration_ms), 0) FROM album_track x JOIN track t ON t.id = x.track_id WHERE x.album_id = ?")
                                    : QStringLiteral("SELECT COALESCE(SUM(t.duration_ms), 0) FROM playlist_track x JOIN track t ON t.id = x.track_id WHERE x.playlist_id = ?");
            if (auto d = m_session->database().scalar(sql, {item.id}); d.ok())
                contents.durationMs += d.value().toLongLong();
        }
    } else if (const auto section = CardQueries::parseSectionKey(key)) {
        // the page of one carousel of an artist card (its title opens it)
        auto items = CardQueries(m_session->database()).section(section->first, section->second, 100000);
        if (!items.ok())
            return;
        contents.items = items.value();
        if (auto a = ArtistRepo(m_session->database()).get(section->first); a.ok() && a.value())
            contents.sectionLabel = a.value()->name;
        for (const CarouselItem& item : std::as_const(contents.items)) {
            const QString sql = QStringLiteral("SELECT COALESCE(SUM(t.duration_ms), 0) FROM album_track x JOIN track t ON t.id = x.track_id WHERE x.album_id = ?");
            if (item.kind == ContainerKind::Album)
                if (auto d = m_session->database().scalar(sql, {item.id}); d.ok())
                    contents.durationMs += d.value().toLongLong();
        }
    } else {
        const auto spec = CategorySpec::fromKey(key);
        if (!spec)
            return;
        auto all = m_home->queries.contents(*spec, 0);
        if (!all.ok())
            return;
        contents = all.value();
        if (spec->type == CategoryType::ArtistDiscography) {
            ArtistRepo artists(m_session->database());
            if (auto a = artists.get(ArtistId(spec->arg.toLongLong())); a.ok() && a.value())
                contents.spec.label = a.value()->name;
        }
    }
    contents.requestedKey = key;
    emit categoryReady(contents);
}

void LibraryEngine::buildCarouselRequest(const QList<ContainerRef>& items, int index, bool all, const QString& title, const QString& key)
{
    if (!m_session) {
        emit requestFailed(-1, 0);
        return;
    }
    PlayRequestBuilder builder(m_session->database());
    auto result = all ? builder.carouselAll(items, title, key) : builder.carouselFrom(items, index);
    if (result.ok())
        emit requestReady(result.value(), -1, 0);
    else
        emit requestFailed(-1, 0);
}

}  // namespace semplice

namespace semplice {

void LibraryEngine::loadArtistCard(qint64 id)
{
    if (!m_session)
        return;
    auto card = CardQueries(m_session->database()).artist(ArtistId(id));
    if (card.ok())
        emit artistCardReady(card.value());
    else
        emit cardFailed(2, id);
}

void LibraryEngine::loadAlbumCard(qint64 id)
{
    if (!m_session)
        return;
    auto card = CardQueries(m_session->database()).album(id);
    if (card.ok())
        emit albumCardReady(card.value());
    else
        emit cardFailed(3, id);
}

void LibraryEngine::loadPlaylistCard(qint64 id)
{
    if (!m_session)
        return;
    auto card = CardQueries(m_session->database()).playlist(PlaylistId(id));
    if (card.ok())
        emit playlistCardReady(card.value());
    else
        emit cardFailed(4, id);
}

void LibraryEngine::searchArtistsFor(const QString& text, int token)
{
    if (!m_session)
        return;
    auto found = CardQueries(m_session->database()).artists(text, 200);
    emit artistsFound(found.ok() ? found.value() : QList<ArtistRef>{}, token);
}

void LibraryEngine::loadArtistTags(int token, const QVariantList& artistIds)
{
    QStringList keys;
    if (m_session) {
        TagRepo repo(m_session->database());
        ArtistRepo known(m_session->database());
        for (const QVariant& who : artistIds) {
            const QVariantMap w = who.toMap();
            qint64 id = w.value(QStringLiteral("id")).toLongLong();
            if (id <= 0) {
                auto found = known.findByName(w.value(QStringLiteral("name")).toString());
                if (!found.ok() || !found.value())
                    continue;
                id = found.value()->id.value;
            }
            if (auto t = repo.tags(TagOwner::Artist, id); t.ok())
                for (const QString& key : t.value())
                    if (!keys.contains(key))
                        keys << key;
        }
    }
    emit artistTagsReady(token, keys);
}

void LibraryEngine::loadShareTree(int kind, qint64 id)
{
    if (!m_session)
        return;
    if (auto tree = m_session->share().describe(static_cast<ShareKind>(kind), id); tree.ok())
        emit shareTreeReady(tree.value());
}

void LibraryEngine::exportShare(int token, const QString& path, const ShareSelection& selection)
{
    if (!m_session) {
        emit shareFinished(token, false, static_cast<int>(ErrorCode::NotFound), 0, 0, 0, 0);
        return;
    }
    m_stop = std::stop_source();
    JobContext job;
    job.cancel = m_stop.get_token();
    job.progress = [this, token](const JobProgress& p) { emit shareProgress(token, p.done, p.total); };
    auto r = m_session->share().exportTo(path, selection, job);
    emit shareFinished(token, false, r.ok() ? 0 : static_cast<int>(r.error().code), 0, 0, 0, 0);
}

void LibraryEngine::describeShareFile(const QString& path, const QString& previewDir)
{
    if (!m_session)
        return;
    auto tree = m_session->share().describeFile(path, previewDir);
    if (tree.ok())
        emit shareTreeReady(tree.value());
    else
        emit shareFinished(0, true, static_cast<int>(tree.error().code), 0, 0, 0, 0);   // token 0: the page was not made by a job
}

void LibraryEngine::importShare(int token, const QString& path, const ImportSelection& selection)
{
    if (!m_session) {
        emit shareFinished(token, true, static_cast<int>(ErrorCode::NotFound), 0, 0, 0, 0);
        return;
    }
    m_stop = std::stop_source();
    JobContext job;
    job.cancel = m_stop.get_token();
    job.progress = [this, token](const JobProgress& p) { emit shareProgress(token, p.done, p.total); };
    auto r = m_session->share().importFrom(path, job, selection);
    if (!r.ok()) {
        emit shareFinished(token, true, static_cast<int>(r.error().code), 0, 0, 0, 0);
        return;
    }
    const ShareImportReport& rep = r.value();
    emit shareFinished(token, true, 0, rep.albumsAdded, rep.albumsSkipped, rep.tracksAdded, rep.playlistsAdded);
}

void LibraryEngine::runOperation(int token, int operation, qint64 a, qint64 b, const QString& text)
{
    OperationResult result;
    result.token = token;
    result.op = operation;
    result.id = a;
    if (!m_session) {
        result.code = static_cast<int>(ErrorCode::NotFound);
        emit operationFinished(result);
        return;
    }
    LibraryService& lib = m_session->library();
    auto fail = [&](const Error& e) {
        result.code = e.numericCode();
        result.message = e.devMessage;
    };
    switch (static_cast<OperationKind>(operation)) {
    case OperationKind::AddTrackToAlbum:
        if (auto r = lib.addTrackToAlbum(TrackId(a), AlbumId(b)); !r.ok()) fail(r.error());
        break;
    case OperationKind::AddTrackToPlaylist:
        if (auto r = lib.addTrackToPlaylist(TrackId(a), PlaylistId(b)); !r.ok()) fail(r.error());
        break;
    case OperationKind::RemoveTrackFromAlbum:
        if (auto r = lib.removeTrackFromAlbum(TrackId(a), AlbumId(b)); !r.ok()) fail(r.error());
        break;
    case OperationKind::RemoveTrackFromPlaylist:
        if (auto r = lib.removeTrackFromPlaylist(TrackId(a), PlaylistId(b)); !r.ok()) fail(r.error());
        break;
    case OperationKind::DeleteTrack:
        if (auto r = lib.deleteTrack(TrackId(a)); !r.ok()) fail(r.error());
        break;
    case OperationKind::DeleteAlbum:
        if (auto r = lib.deleteAlbum(AlbumId(a)); !r.ok()) fail(r.error());
        break;
    case OperationKind::DeleteArtist:
        if (auto r = lib.deleteArtist(ArtistId(a)); !r.ok()) fail(r.error());
        break;
    case OperationKind::DeletePlaylist:
        if (auto r = lib.deletePlaylist(PlaylistId(a)); !r.ok()) fail(r.error());
        break;
    case OperationKind::AddToDiscography:
        if (auto r = lib.addToDiscography(TrackId(a), ArtistId(b)); !r.ok()) fail(r.error()); else result.createdId = r.value().value;
        break;
    case OperationKind::MaterialiseHiddenArtist:
        if (auto r = lib.materialiseHiddenArtist(ArtistId(a)); !r.ok()) fail(r.error()); else result.createdId = r.value().value;
        break;
    case OperationKind::NewPlaylistFromTrack: {
        PlaylistDraft draft;
        draft.name = text;
        TrackInput in;
        in.existing = TrackId(a);
        draft.tracks = {in};
        if (auto r = lib.createPlaylist(draft); !r.ok()) fail(r.error()); else result.createdId = r.value().value;
        break;
    }
    }
    emit operationFinished(result);
}

}  // namespace semplice

namespace semplice {

void LibraryEngine::buildRequestAt(int kind, qint64 id, int startIndex)
{
    if (!m_session) {
        emit requestFailed(kind, id);
        return;
    }
    PlayRequestBuilder builder(m_session->database());
    Result<PlayRequest> result = Error::make(ErrorCode::Unknown);
    switch (static_cast<PlayKind>(kind)) {
    case PlayKind::Album: result = builder.album(AlbumId(id)); break;
    case PlayKind::Playlist: result = builder.playlist(PlaylistId(id)); break;
    case PlayKind::HiddenArtist: result = builder.hiddenArtist(ArtistId(id)); break;
    default: break;
    }
    if (result.ok()) {
        result.value().startIndex = startIndex;
        emit requestReady(result.value(), kind, id);
    } else {
        emit requestFailed(kind, id);
    }
}

}  // namespace semplice

namespace semplice {

void LibraryEngine::searchPicker(int kind, const QString& text, int token)
{
    if (!m_session)
        return;
    QList<ListRow> rows;
    const QString needle = text.trimmed().toCaseFolded();
    Database& db = m_session->database();
    if (kind == 0) {
        auto found = CardQueries(db).artists(text, 200);
        if (found.ok())
            for (const ArtistRef& a : found.value())
                rows.append({a.id, a.name, a.hidden ? QStringLiteral("hidden") : QString(), a.avatar, 0, 0, 0});
    } else if (kind == 1) {
        auto list = LibraryQueries(db).playlists({SortKey::Alphabet, SortDirection::Ascending});
        if (list.ok())
            for (const PlaylistRow& p : list.value())
                if (needle.isEmpty() || p.name.toCaseFolded().contains(needle))
                    rows.append({p.id.value, p.name, {}, p.coverPath, 0, 0, -1});
    } else {
        auto list = LibraryQueries(db).albums({SortKey::Alphabet, SortDirection::Ascending});
        if (list.ok())
            for (const AlbumRow& a : list.value())
                if (needle.isEmpty() || a.title.toCaseFolded().contains(needle))
                    rows.append({a.id.value, a.title, a.artists, a.coverPath, a.primaryArtist.value, a.year, static_cast<int>(a.type)});
    }
    emit pickerReady(rows, token);
}

}  // namespace semplice
