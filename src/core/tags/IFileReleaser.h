#pragma once

#include "core/util/Result.h"

namespace semplice {

// Handshake between the tag writer and the audio backend (SPEC 6.2). Windows keeps a playing file
// open, so before a tag write replaces it the backend must let go of it (stop/unload, remember the
// position) and afterwards take it back. M5's IAudioBackend::release implements this.
class IFileReleaser {
public:
    virtual ~IFileReleaser() = default;
    // The file is about to be replaced. Return an error to abort the write.
    virtual Result<void> releaseFile(const QString& absolutePath) = 0;
    // The write is over (successfully or not): the file may be opened again.
    virtual void fileReady(const QString& absolutePath) = 0;
    // Is the file open in the player right now (thread-safe, cheap)? A tag write then waits until the track is over:
    // stopping and reopening the media costs the player up to a second.
    virtual bool inUse(const QString& absolutePath) const
    {
        Q_UNUSED(absolutePath)
        return false;
    }
};

}  // namespace semplice
