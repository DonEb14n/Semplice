#pragma once

#include "core/browse/LibraryQueries.h"
#include "core/browse/PlayRequestBuilder.h"
#include "core/share/ShareService.h"
#include "core/cards/CardQueries.h"
#include "core/home/CategoryGenerator.h"
#include "core/home/HomeQueries.h"
#include "core/playback/PlaybackTypes.h"
#include "core/services/LibrarySession.h"
#include "ui/EditorTypes.h"

#include <QMetaType>
#include <QObject>
#include <QVariantMap>

#include <stop_token>

#include <memory>

namespace semplice {

// One line of a library list, already formatted for display (built on the engine thread).
struct ListRow {
    qint64 id = 0;
    QString title;
    QString subtitle;   // albums: "Artist, Artist"
    QString cover;      // library-relative cover/avatar path, empty = placeholder
    qint64 ownerId = 0; // albums: the primary artist (opens the artist card)
    int year = 0;
    int type = 0;       // albums: AlbumType as int
    QList<OwnerLink> owners = {};  // carousel albums: every artist of the album (each is a link), primary first
    bool dimmed = false;  // an artist hidden by hand (grey name)
};

enum class PlayKind { Artist, HiddenArtist, Album, Playlist, Track };

// The outcome of one write operation started from the UI (code 0 = success).
enum class OperationKind {
    AddTrackToAlbum, AddTrackToPlaylist, RemoveTrackFromAlbum, RemoveTrackFromPlaylist, DeleteTrack, DeleteAlbum,
    DeleteArtist, DeletePlaylist, AddToDiscography, NewPlaylistFromTrack, MaterialiseHiddenArtist
};
struct OperationResult {
    int token = 0;
    int op = -1;           // OperationKind when this answers runOperation
    QString entity;        // "artist", "album", "playlist", "track" when a save finished
    qint64 id = 0;         // the saved entity
    int code = 0;          // ErrorCode as int
    QString message;       // developer text
    qint64 createdId = 0;  // e.g. the playlist made from a track
};

// What the Home page needs: the Recently Played list and the generated carousels.
struct HomeData {
    bool recentOnly = false;  // only `recent` is valid (something was played), keep the shown categories
    bool hasContent = false;
    QList<CarouselItem> recent;
    QList<CategoryContents> categories;
};

// Lives on the engine thread. Owns the open library (database, storage, services, search index) and
// answers the GUI's questions with plain value types. Every public slot is meant to be invoked through a
// queued connection.
class LibraryEngine : public QObject {
    Q_OBJECT
public:
    explicit LibraryEngine(QObject* parent = nullptr);
    // Thread-safe: asks the running import / save to stop (the Cancel button). Called from the GUI thread directly.
    void requestStop() { m_stop.request_stop(); }
    ~LibraryEngine() override;

    // The object the engine uses to let go of a playing file before a tag write replaces it. Set before start.
    void setReleaser(IFileReleaser* releaser) { m_releaser = releaser; }

public slots:
    // Start-up: the path stored in the settings (empty = first launch).
    void startup(const QString& storedPath);
    // "Select folder" / "Change": opens a library, or starts one in an empty folder.
    void selectFolder(const QString& path);
    void closeLibrary();

    void loadArtists(int sortKey, int direction, bool hiddenOnly);
    void loadAlbums(int sortKey, int direction, int typeMask);  // bit per AlbumType, 0 = all
    void loadPlaylists(int sortKey, int direction);

    // Home page. `knownKeys` are the categories already shown: they are kept (refreshed); with none, five new ones
    // are generated. The generator remembers what it produced during this library session.
    void loadHome(const QStringList& knownKeys, bool recentOnly);
    void loadMoreCategories(int count);
    void loadCategory(const QString& key);
    void buildCarouselRequest(const QList<semplice::ContainerRef>& items, int index, bool all, const QString& title, const QString& key);

    // Cards and pickers
    void loadArtistCard(qint64 id);
    void loadAlbumCard(qint64 id);      // negative: the Unsorted tracks of the hidden artist -id
    void loadPlaylistCard(qint64 id);
    void searchArtistsFor(const QString& text, int token);
    void loadShareTree(int kind, qint64 id);
    void exportShare(int token, const QString& path, const semplice::ShareSelection& selection);
    void describeShareFile(const QString& path, const QString& previewDir);
    void importShare(int token, const QString& path, const semplice::ImportSelection& selection);
    void loadArtistTags(int token, const QVariantList& artistIds);
    void loadTrackEdit(qint64 id);
    // What deleting would do (op: OperationKind of a delete): answered by deletePlanReady.
    void planDelete(int op, qint64 id);
    // Settings -> Rescan (SPEC 6.5); progress comes as importProgress(token), the end as rescanFinished.
    void rescan(int token);
    // Writes the tags that could not be written while their file was playing (called when the player moves on).
    void syncTags();
    // Is this name already another artist's? (the answer: mergeTargetFound; used before a rename that would merge two artists)
    void findMergeTarget(int token, const QString& name, qint64 exceptId);
    // Pickers (artist 0, playlist 1, album 2): best matches first, hidden artists included.
    void searchPicker(int kind, const QString& text, int token);
    // One write operation; the answer comes as operationFinished(token).
    void runOperation(int token, int operation, qint64 a, qint64 b, const QString& text);

    // Editors (UI doc p.31-47). The scan lives in the engine until clearScan(); rows refer to it by index.
    void clearScan();
    void scanFiles(int token, const QStringList& paths, qint64 preselectedArtist, bool reset);
    // Saves what an editor window holds; `command` is described in LibraryEngineEditors.cpp.
    void saveEntity(int token, const QVariantMap& command);

    void search(const QString& text, int token);
    void buildRequest(int kind, qint64 id);
    void buildRequestAt(int kind, qint64 id, int startIndex);

signals:
    void noLibraryYet();
    void pathMissing(const QString& path);
    void opened(const QString& root, const QString& dbPath, bool backupCreated, bool repaired);
    void openFailed(int errorCode, const QString& devMessage, const QString& path);
    void closed();

    void artistsReady(const QList<semplice::ListRow>& rows);
    void albumsReady(const QList<semplice::ListRow>& rows);
    void playlistsReady(const QList<semplice::ListRow>& rows);
    void searchReady(const semplice::SearchResults& results, int token);
    void requestReady(const semplice::PlayRequest& request, int kind, qint64 id);
    void requestFailed(int kind, qint64 id);
    void homeReady(const semplice::HomeData& data);
    void moreCategoriesReady(const QList<semplice::CategoryContents>& categories);
    void categoryReady(const semplice::CategoryContents& contents);
    void artistCardReady(const semplice::ArtistCard& card);
    void albumCardReady(const semplice::AlbumCard& card);
    void playlistCardReady(const semplice::PlaylistCard& card);
    void cardFailed(int pageKind, qint64 id);
    void artistsFound(const QList<semplice::ArtistRef>& artists, int token);
    void shareTreeReady(const semplice::ShareTree& tree);
    void shareProgress(int token, qint64 done, qint64 total);
    // a share export (isImport false) or import ended: code 0 = done; for an import: albums added, skipped, tracks added, playlists added
    void shareFinished(int token, bool isImport, int code, int added, int skipped, int tracks, int playlists);
    void artistTagsReady(int token, const QStringList& keys);
    void trackEditReady(const semplice::TrackEdit& track);
    void deletePlanReady(const semplice::DeleteSummary& plan);
    void rescanFinished(int token, int code, int adopted, bool blocked, bool cancelled);
    void mergeTargetFound(int token, bool exists, bool hidden);
    void pickerReady(const QList<semplice::ListRow>& rows, int token);
    void operationFinished(const semplice::OperationResult& result);
    void scanReady(const semplice::ScanSummary& summary);
    void importProgress(int token, const QString& phase, qint64 done, qint64 total);
    void libraryChanged(const semplice::ChangeSet& changes);

private:
    SessionDependencies dependencies();
    OperationResult saveArtist(int token, const QVariantMap& c);
    OperationResult saveAlbum(int token, const QVariantMap& c);
    OperationResult savePlaylist(int token, const QVariantMap& c);
    OperationResult saveTrack(int token, const QVariantMap& c);
    JobContext jobFor(int token);
    void adopt(std::unique_ptr<LibrarySession> session);

    // real platform objects (created on the engine thread)
    struct Platform;
    std::unique_ptr<Platform> m_platform;
    IFileReleaser* m_releaser = nullptr;
    std::unique_ptr<LibrarySession> m_session;
    QSet<qint64> m_syncGaveUp;  // tracks whose tags could not be written: not tried again until the library is reopened
    ImportScan m_scan;
    ImportProposal m_proposal;
    std::stop_source m_stop;
    struct HomeState;
    std::unique_ptr<HomeState> m_home;
};

}  // namespace semplice

Q_DECLARE_METATYPE(semplice::ListRow)
Q_DECLARE_METATYPE(semplice::ShareTree)
Q_DECLARE_METATYPE(semplice::ShareSelection)
Q_DECLARE_METATYPE(semplice::ImportSelection)
Q_DECLARE_METATYPE(QList<semplice::ListRow>)
Q_DECLARE_METATYPE(semplice::SearchResults)
Q_DECLARE_METATYPE(semplice::PlayRequest)
Q_DECLARE_METATYPE(semplice::ChangeSet)
Q_DECLARE_METATYPE(semplice::CarouselItem)
Q_DECLARE_METATYPE(semplice::CategoryContents)
Q_DECLARE_METATYPE(QList<semplice::CategoryContents>)
Q_DECLARE_METATYPE(semplice::HomeData)
Q_DECLARE_METATYPE(semplice::ArtistCard)
Q_DECLARE_METATYPE(semplice::AlbumCard)
Q_DECLARE_METATYPE(semplice::PlaylistCard)
Q_DECLARE_METATYPE(semplice::ArtistRef)
Q_DECLARE_METATYPE(QList<semplice::ArtistRef>)
Q_DECLARE_METATYPE(semplice::OperationResult)
Q_DECLARE_METATYPE(semplice::ContainerRef)
Q_DECLARE_METATYPE(QList<semplice::ContainerRef>)
