#pragma once

#include "core/tags/TagService.h"

namespace semplice {

enum class CoverOwner { Album, Artist, Playlist };

// Where a cover came from, in priority order (SPEC 4.1): a cover the user picked, a cover file in the
// source folder, the picture embedded in the first track.
struct CoverSource {
    enum class Kind { None, UserChosen, FolderFile, Embedded };
    Kind kind = Kind::None;
    QString path;                     // UserChosen / FolderFile: absolute path of the image file
    std::optional<CoverImage> image;  // always set unless kind == None
};

class CoverDiscovery {
public:
    // The cover file of a folder that is being imported: `cover.*` first, then `album_cover.*`
    // (jpg, jpeg, png; any case; the content must really be an image). Empty if there is none.
    static QString findInFolder(const QString& folder);

    // Reads an image file; std::nullopt unless it is a JPEG or PNG (checked by content, not by name)
    // no larger than kMaxCoverBytes.
    static std::optional<CoverImage> loadImageFile(const QString& path);
    static constexpr qint64 kMaxCoverBytes = 32 * 1024 * 1024;

    // Name under which a cover is stored INSIDE the library: always album_cover.<ext> / artist_cover.<ext>
    // / playlist_cover.<ext> with the extension of the real image type.
    static QString storedName(CoverOwner owner, const CoverImage& image);

    // user choice > folder file > picture embedded in the FIRST track. E_INVALID_ARGUMENT if the user's
    // choice is not a usable image.
    static Result<CoverSource> resolve(const QString& userChosenPath, const QString& sourceFolder, ITagService& tags,
                                       const QStringList& trackPathsInOrder);

    // Writes the cover to a new file (never overwrites; atomic).
    static Result<void> writeFile(const CoverImage& image, const QString& absolutePath);
};

}  // namespace semplice
