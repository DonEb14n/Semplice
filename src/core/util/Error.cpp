#include "core/util/Error.h"

#include <iterator>

namespace semplice {

namespace {

struct Entry {
    ErrorCode code;
    const char* symbol;
    const char* userKey;  // empty = no dedicated user message
};

constexpr Entry kRegistry[] = {
    {ErrorCode::Unknown, "E_UNKNOWN", ""},
    {ErrorCode::InvalidArgument, "E_INVALID_ARGUMENT", ""},
    {ErrorCode::NotFound, "E_NOT_FOUND", ""},
    {ErrorCode::IoError, "E_IO", ""},
    {ErrorCode::Cancelled, "E_CANCELLED", ""},
    {ErrorCode::DatabaseError, "E_DATABASE", ""},
    {ErrorCode::AlreadyExists, "E_ALREADY_EXISTS", "error.already_exists"},
    {ErrorCode::NameEmpty, "E_NAME_EMPTY", "error.name_empty"},
    {ErrorCode::ReservedName, "E_RESERVED_NAME", "error.reserved_name"},
    {ErrorCode::NoTracks, "E_NO_TRACKS", "error.no_tracks"},
    {ErrorCode::NoArtist, "E_NO_ARTIST", "error.no_artist"},
    {ErrorCode::InvalidDate, "E_INVALID_DATE", "error.invalid_date"},
    {ErrorCode::UnknownTag, "E_UNKNOWN_TAG", ""},
    {ErrorCode::NotAllowed, "E_NOT_ALLOWED", ""},
    {ErrorCode::SchemaTooNew, "E_SCHEMA_TOO_NEW", "error.schema_too_new"},
    {ErrorCode::FolderNotEmpty, "E_FOLDER_NOT_EMPTY", "error.folder_not_empty"},
    {ErrorCode::NotALibrary, "E_NOT_A_LIBRARY", "error.not_a_library"},
    {ErrorCode::DatabaseCorrupt, "E_DATABASE_CORRUPT", "error.database_corrupt"},
    {ErrorCode::UnsupportedFile, "E_UNSUPPORTED_FILE", ""},
    {ErrorCode::TagWriteFailed, "E_TAG_WRITE", ""},
    {ErrorCode::FileLocked, "E_FILE_LOCKED", "error.file_locked"},
    {ErrorCode::CrashInjected, "E_CRASH_INJECTED", ""},
    {ErrorCode::PathTooLong, "E_PATH_TOO_LONG", "error.path_too_long"},
};

const Entry* find(ErrorCode code)
{
    for (const Entry& e : kRegistry) {
        if (e.code == code)
            return &e;
    }
    return nullptr;
}

}  // namespace

Error Error::make(ErrorCode code, QString devMessage)
{
    return Error{code, std::move(devMessage)};
}

QString Error::symbol() const
{
    if (const Entry* e = find(code))
        return QString::fromLatin1(e->symbol);
    return QStringLiteral("E_UNKNOWN");
}

QString Error::userMessageKey() const
{
    if (const Entry* e = find(code))
        return QString::fromLatin1(e->userKey);
    return {};
}

}  // namespace semplice
