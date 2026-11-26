#pragma once

#include <QString>

namespace semplice {

// The folder of small cover thumbnails next to the database (SPEC 9: cap 1 GB, least recently used go first).
// The folder can be emptied at any time: it only holds copies.
struct ThumbnailCache {
    static constexpr qint64 kCapBytes = 1024LL * 1024 * 1024;

    // When the files of `folder` together exceed `capBytes`, removes the least recently used ones until
    // 80 % of the cap is left. Returns the number of removed files. Safe to call from a worker thread.
    static int trim(const QString& folder, qint64 capBytes = kCapBytes);

    // Marks a cache file as used now (called on a hit, at most once a day per file).
    static void touch(const QString& file);
};

}  // namespace semplice
