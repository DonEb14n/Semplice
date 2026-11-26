#pragma once

#include "core/db/Database.h"
#include "core/services/LibraryService.h"
#include "core/util/Job.h"
#include "core/util/Result.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

namespace semplice {

// Sharing (docs/design-notes/M12-share-smpl.md): a playlist, an album or the whole discography of an artist goes into one
// *.smpl file with its audio and pictures; importing such a file into another library needs no manual work and skips
// what the library has already.
enum class ShareKind { Artist = 0, Album = 1, Playlist = 2 };

struct ShareTrackNode {
    qint64 id = 0;
    QString title;
    QString artists;       // "A, B"
    qint64 durationMs = 0;
    QString cover;         // library-relative cover of the album the track is in (display only)
    bool existing = false; // import: the library has this track already
};

struct ShareAlbumNode {
    qint64 id = 0;
    QString title;
    QString artists;       // "A, B"
    QString cover;         // library-relative
    int type = 1;          // AlbumType as int
    int year = 0;
    int month = 0;
    int day = 0;
    QStringList tags;      // tag keys (genres, "lang:..", "mood:..")
    QStringList authors;
    bool existing = false; // import: the library has this album already (it will be skipped)
    QList<ShareTrackNode> tracks;
};

struct SharePlaylistNode {
    QString name;
    QString cover;
    bool existing = false; // import: a playlist of that name exists (the new one is called "Name (2)")
    QList<ShareTrackNode> tracks;
};

struct ShareArtistNode {
    qint64 id = 0;
    QString name;
    QString avatar;        // library-relative
    bool main = false;     // the artist whose discography is exported
    bool hidden = false;   // hidden by hand
    QStringList tags;
    bool existing = false; // import: the library has this artist already
};

// What the Share page shows and the user ticks.
struct ShareTree {
    ShareKind kind = ShareKind::Album;
    qint64 id = 0;
    QString title;
    QString cover;
    QList<ShareArtistNode> artists;       // the main artist first, then the artists who appear on the albums
    QList<ShareAlbumNode> albums;         // album and artist kind
    QList<ShareTrackNode> tracks;         // playlist kind
    QList<SharePlaylistNode> playlists;   // describeFile: the playlists of a package
    bool importing = false;               // describeFile: the tree is the content of a file, not of the library
};

struct ShareSelection {
    ShareKind kind = ShareKind::Album;
    qint64 id = 0;
    QSet<qint64> albums;          // artist kind: the albums to export
    QSet<qint64> tracks;          // the tracks to export (everything not listed stays out)
    QSet<qint64> avatarArtists;   // the artists whose picture goes along
    bool includeCover = true;     // album / playlist picture
    QHash<qint64, bool> artistHidden;   // the status in the library the file suggests for each artist (true = hidden)
};

// What of a package is imported (describeFile numbers everything). Keys: "artist:<name>", "album:<i>", "track:<i>:<j>" (track j of
// album i), "playlist:<i>", "ptrack:<i>:<j>". Absent = everything.
struct ImportSelection {
    bool all = true;
    QSet<QString> keys;
    QHash<QString, bool> artistHidden;   // by artist name: the status chosen for artists that are new to the library
    bool has(const QString& key) const { return all || keys.contains(key); }
};

struct ShareImportReport {
    int albumsAdded = 0;
    int albumsSkipped = 0;
    int tracksAdded = 0;
    int tracksSkipped = 0;       // already in the library (same audio)
    int playlistsAdded = 0;
    int artistsUpdated = 0;      // got a picture or tags they did not have
};

class ShareService {
public:
    ShareService(Database& db, LibraryService& library, QString libraryRoot);

    Result<ShareTree> describe(ShareKind kind, qint64 id);
    Result<void> exportTo(const QString& path, const ShareSelection& selection, const JobContext& job = {});
    // the content of a package, as the Share page shows it before anything is imported
    // `previewDir`: where the pictures of the package (covers, avatars) are unpacked so the page can show them; the node covers are then absolute paths
    Result<ShareTree> describeFile(const QString& path, const QString& previewDir = {});
    Result<ShareImportReport> importFrom(const QString& path, const JobContext& job = {}, const ImportSelection& selection = {});

private:
    QString absolute(const QString& relative) const;
    QString artistNames(TrackId track);

    Database& m_db;
    LibraryService& m_library;
    QString m_root;
};

}  // namespace semplice
