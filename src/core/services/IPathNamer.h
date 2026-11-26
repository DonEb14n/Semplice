#pragma once

#include "core/storage/PathSanitizer.h"

#include <QString>

namespace semplice {

// Builds folder and file names. Collision handling (" (2)") is done by the caller against the
// database (LibraryService) or the disk (FileStorageService).
class IPathNamer {
public:
    virtual ~IPathNamer() = default;

    virtual QString dirName(const QString& name, NameKind kind) const = 0;
    // Album file: "NN - Title.ext", NN = position with at least two digits.
    virtual QString albumTrackFileName(int position, const QString& title, const QString& extension) const = 0;
    // Playlist file: "Title - Artist.ext".
    virtual QString playlistTrackFileName(const QString& title, const QString& artist,
                                          const QString& extension) const = 0;
};

// The production namer: every name goes through PathSanitizer (SPEC 4.1).
class SanitizingPathNamer final : public IPathNamer {
public:
    QString dirName(const QString& name, NameKind kind) const override;
    QString albumTrackFileName(int position, const QString& title, const QString& extension) const override;
    QString playlistTrackFileName(const QString& title, const QString& artist,
                                  const QString& extension) const override;
};

}  // namespace semplice
