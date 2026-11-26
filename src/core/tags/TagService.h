#pragma once

#include "core/tags/IAtomicReplace.h"
#include "core/tags/IFileReleaser.h"
#include "core/tags/TrackTags.h"

#include <QStringList>

namespace semplice {

class ITagService {
public:
    virtual ~ITagService() = default;
    // Opens the file read-only. E_UNSUPPORTED_FILE if it is not a readable audio file.
    virtual Result<TrackTags> read(const QString& absolutePath) = 0;
    // The front cover (or the first picture) if the file has one.
    virtual Result<std::optional<CoverImage>> readCover(const QString& absolutePath) = 0;
    // Writes through a temporary copy and an atomic replace (SPEC 6.2). The original is untouched on failure.
    virtual Result<void> write(const QString& absolutePath, const TagWrite& request) = 0;
    // For a file the running operation has just created (an import copy): written in place, no temporary
    // copy and no replace, because nobody can have it open yet. Verified like write().
    virtual Result<void> writeNewFile(const QString& absolutePath, const TagWrite& request) = 0;
};

// TagLib implementation for MP3, FLAC, OGG Vorbis, M4A, WAV and WMA.
class TagService final : public ITagService {
public:
    // `releaser` may be null (no audio backend yet, or not playing).
    explicit TagService(IAtomicReplace& replace, IFileReleaser* releaser = nullptr);

    static bool isSupportedExtension(const QString& suffix);
    static QStringList supportedExtensions();

    Result<TrackTags> read(const QString& absolutePath) override;
    Result<std::optional<CoverImage>> readCover(const QString& absolutePath) override;
    Result<void> write(const QString& absolutePath, const TagWrite& request) override;
    Result<void> writeNewFile(const QString& absolutePath, const TagWrite& request) override;

    // Start-up housekeeping inside the library: removes temporary files left by a crash during a tag
    // write and puts a set-aside original back if the crash happened in the middle of the replace.
    // Returns how many files were cleaned up.
    static int removeStaleTempFiles(const QString& libraryRoot);

private:
    Result<void> writeToCopy(const QString& copyPath, const QString& originalPath, const TagWrite& request);

    IAtomicReplace& m_replace;
    IFileReleaser* m_releaser;
};

}  // namespace semplice
