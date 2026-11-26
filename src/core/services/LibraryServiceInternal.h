#pragma once

// Private to LibraryService*.cpp: shared helpers and the per-operation context.

#include "core/services/LibraryService.h"

#include <algorithm>

namespace semplice {

// Propagate an error out of a function returning Result<...>.
#define SEMPLICE_TRY(expr)                      \
    do {                                        \
        auto semplice_try_result = (expr);      \
        if (!semplice_try_result.ok())          \
            return semplice_try_result.error(); \
    } while (false)

// auto name = value of a Result, or return its error.
#define SEMPLICE_ASSIGN(name, expr)   \
    auto name##_result = (expr);      \
    if (!name##_result.ok())          \
        return name##_result.error(); \
    auto name = std::move(name##_result).value()

struct LibraryService::Context {
    struct NewFile {
        TrackId id;
        QString relativePath;  // where the operation creates the file
        QString source;        // Copy source (absolute); empty for a move inside the library
        QByteArray expectedHash;
    };

    OperationReport report;
    QSet<ArtistId> creditTouched;  // credits or co-author rows changed: maybe garbage-collect
    QSet<ArtistId> albumLost;      // lost an album: maybe becomes hidden
    QSet<ArtistId> skip;           // handled explicitly by the running operation
    QSet<TrackId> dirty;           // file tags must be rewritten
    QList<NewFile> newFiles;       // tracks whose file this operation creates
    const JobContext* job = nullptr;
};

namespace internal {

inline const QString kArtistsRoot = QStringLiteral("Artists");
inline const QString kPlaylistsRoot = QStringLiteral("Playlists");

template <class T>
void addUnique(QList<T>& list, T value)
{
    if (!list.contains(value))
        list.append(value);
}

inline QString extensionOf(const QString& path)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? QStringLiteral("mp3") : path.mid(dot + 1);
}

inline QString tempPath(qint64 trackId)
{
    return QStringLiteral("\x01tmp/%1").arg(trackId);
}

inline void touch(LibraryService::Context& c, ArtistId id) { c.report.changes.artists.insert(id); }
inline void touch(LibraryService::Context& c, AlbumId id) { c.report.changes.albums.insert(id); }
inline void touch(LibraryService::Context& c, PlaylistId id) { c.report.changes.playlists.insert(id); }
inline void touch(LibraryService::Context& c, TrackId id) { c.report.changes.tracks.insert(id); }

template <class T>
void sortList(QList<T>& list)
{
    std::sort(list.begin(), list.end());
}

}  // namespace internal
}  // namespace semplice
