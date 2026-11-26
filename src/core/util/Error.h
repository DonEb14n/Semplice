#pragma once

#include <QString>

namespace semplice {

// Stable numeric codes. Never renumber a released code; only append.
enum class ErrorCode : int {
    Unknown = -1,  // shown to the user as the generic "Error -1" banner
    None = 0,
    InvalidArgument = 1,
    NotFound = 2,
    IoError = 3,
    Cancelled = 4,
    DatabaseError = 5,
    AlreadyExists = 6,    // artist / album / playlist name taken
    NameEmpty = 7,
    ReservedName = 8,     // e.g. an album called "Unsorted tracks"
    NoTracks = 9,         // album / playlist needs at least one track
    NoArtist = 10,        // album needs at least one artist
    InvalidDate = 11,
    UnknownTag = 12,      // tag key not in the vocabulary
    NotAllowed = 13,      // e.g. removing a track from its only container
    SchemaTooNew = 14,    // database written by a newer Semplice
    FolderNotEmpty = 15,  // library folder must be empty to start a new library
    NotALibrary = 16,     // folder has no (valid) .library/library.json
    DatabaseCorrupt = 17, // quick_check failed
    UnsupportedFile = 18, // not an audio file we can read
    TagWriteFailed = 19,  // writing tags failed (original untouched)
    FileLocked = 20,      // another process holds the file open
    CrashInjected = 99,   // tests only: simulated power loss
    PathTooLong = 100,    // E_PATH_TOO_LONG
};

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    QString devMessage;  // for logs, never shown to the user

    static Error make(ErrorCode code, QString devMessage = {});

    int numericCode() const { return static_cast<int>(code); }
    // Symbolic name, e.g. "E_PATH_TOO_LONG".
    QString symbol() const;
    // Translation key of the user-facing message; empty when the error has none
    // (the UI then shows the generic "Error -1" banner).
    QString userMessageKey() const;
    bool hasUserMessage() const { return !userMessageKey().isEmpty(); }
};

}  // namespace semplice
