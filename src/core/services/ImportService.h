#pragma once

#include "core/services/LibraryService.h"
#include "core/tags/ArtistStringSplitter.h"
#include "core/tags/EncodingFixer.h"
#include "core/tags/GenreMap.h"

#include <QDateTime>

namespace semplice {

struct SkippedFile {
    QString path;
    QString reason;  // developer text; the UI shows one banner for the whole batch
};

// One audio file found by the scan, with everything the form needs.
struct ScannedTrack {
    QString sourcePath;     // absolute
    QString sourceFolder;   // absolute
    QString extension;      // lower case
    TrackTags tags;         // after the EncodingFixer
    QStringList repairedFields;
    QByteArray hash;        // SHA-256 of the source bytes
    qint64 size = 0;
    qint64 mtime = 0;
    std::optional<TrackId> duplicateOf;  // this exact file is already in the library
};

struct ImportScan {
    QList<ScannedTrack> tracks;
    QList<SkippedFile> skipped;  // unreadable audio files (other file types are ignored silently)
};

// ---- the forms the user edits (SPEC 8.3) ----
struct ImportTrackForm {
    int scanIndex = -1;           // index into ImportScan::tracks
    QString title;
    QList<CreditDraft> credits;   // by name
};

struct ImportAlbumForm {
    QString title;                // blank if the tags did not say
    QStringList authors;          // first = primary; names, created if new
    AlbumType type = AlbumType::Album;
    ReleaseDate date;             // year 0 = the user must choose
    QStringList tags;             // vocabulary keys
    QString coverPath;            // a file chosen by the user or found next to the music
    std::optional<CoverImage> embeddedCover;  // picture from the first track, used when coverPath is empty
    QList<ImportTrackForm> tracks;
};

struct ImportPlaylistForm {
    QString name;
    QString coverPath;
    QList<ImportTrackForm> tracks;
};

enum class ImportKind { Album, Playlist };

struct ImportProposal {
    ImportKind kind = ImportKind::Playlist;
    ImportAlbumForm album;        // filled when kind == Album
    ImportPlaylistForm playlist;  // filled when kind == Playlist
};

struct ImportResult {
    AlbumId album;
    PlaylistId playlist;
    int imported = 0;    // new files copied into the library
    int duplicates = 0;  // already there: only added to the new album/playlist
    OperationReport report;
};

// SPEC 6.1. Scan and propose touch nothing; the commit calls are one journaled operation each.
class ImportService {
public:
    ImportService(LibraryService& library, ITagService& tags, const GenreVocabulary& genres,
                  const ICodepageConverter& codepages, Database& db, const IClock& clock, QString libraryRoot);

    // Files and/or folders (recursive). Hashes every audio file once so duplicates are known early.
    Result<ImportScan> scan(const QStringList& paths, const JobContext& job = {});

    // 1-3 tracks Single, 4-7 EP, 8+ Album, when the tracks share album metadata or more than half share
    // an artist (or an artist page is open); otherwise a playlist (UI doc p.29).
    // The picture inside a track file (nullopt: none).
    std::optional<CoverImage> embeddedCoverOf(const QString& sourcePath) const;
    ImportProposal propose(const ImportScan& scan, std::optional<ArtistId> preselectedArtist = {}) const;

    Result<ImportResult> commitAlbum(const ImportScan& scan, const ImportAlbumForm& form, const JobContext& job = {});
    Result<ImportResult> commitPlaylist(const ImportScan& scan, const ImportPlaylistForm& form, const JobContext& job = {});
    // Drop files onto an open album / playlist page.
    Result<ImportResult> appendToAlbum(AlbumId album, const ImportScan& scan, const QList<ImportTrackForm>& tracks,
                                       const JobContext& job = {});
    Result<ImportResult> appendToPlaylist(PlaylistId playlist, const ImportScan& scan, const QList<ImportTrackForm>& tracks,
                                          const JobContext& job = {});

private:
    QList<CreditDraft> creditsFor(const ScannedTrack& track, const QString& fallbackName) const;
    QList<TrackInput> inputsFor(const ImportScan& scan, const QList<ImportTrackForm>& forms, bool& ok) const;
    ArtistStringSplitter::Exists existsFn() const;

    LibraryService& m_library;
    ITagService& m_tags;
    const GenreVocabulary& m_genres;
    GenreMap m_genreMap;
    EncodingFixer m_fixer;
    Database& m_db;
    const IClock& m_clock;
    QString m_root;
};

}  // namespace semplice
