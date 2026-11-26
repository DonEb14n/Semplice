#pragma once

#include "core/util/Result.h"

#include <QString>

#include <functional>

namespace semplice {

// What a name is for; only decides the English fallback used when nothing is left (SPEC 4.1).
enum class NameKind { Artist, Album, Playlist, Track };

// Turns arbitrary user text into a safe Windows folder/file name component. Pure functions.
class PathSanitizer {
public:
    // Each component is at most this many UTF-16 units (SPEC 4.1).
    static constexpr int kMaxComponentLength = 120;
    // Beyond this the Win32 extended-length limit is exceeded: E_PATH_TOO_LONG.
    static constexpr int kMaxFullPathLength = 32000;

    // "Unknown artist" / "Unknown album" / "Untitled" - English on disk, never localised.
    static QString fallbackFor(NameKind kind);

    // Folder name. Never empty, never reserved, never ends with a dot or space.
    static QString sanitizeDirName(const QString& name, NameKind kind);

    // "stem.ext". The extension (lower-cased, [a-z0-9] only) is kept; the stem is shortened to fit.
    static QString sanitizeFileName(const QString& stem, const QString& extension, NameKind kind = NameKind::Track);

    // Case-insensitive NFC comparison key (NTFS is case-insensitive).
    static QString compareKey(const QString& name);

    // "name (2)" or, for files, "name (2).ext"; the result still fits in 120 units.
    static QString withCollisionSuffix(const QString& name, int n, bool isFile);

    // First of name, name (2), name (3) ... for which isTaken() is false.
    static QString resolveCollision(const QString& name, bool isFile, const std::function<bool(const QString&)>& isTaken);

    // E_PATH_TOO_LONG when the full path cannot exist even with long-path support.
    static Result<void> checkPathLength(const QString& fullPath);
};

}  // namespace semplice
