-- Semplice library schema, migration 0001 (DRAFT for Claude Code to refine in M1).
-- Conventions: timestamps are Unix epoch milliseconds (UTC). All paths are stored
-- RELATIVE to the library root, with forward slashes, NFC-normalised.
-- All text comparisons for identity use COLLATE NOCASE on NFC-normalised, trimmed text.
-- Run with: PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;
-- Schema version is tracked with PRAGMA user_version (set to 1 at the end).

CREATE TABLE artist (
    id            INTEGER PRIMARY KEY,
    name          TEXT    NOT NULL CHECK (length(trim(name)) > 0),
    dir_name      TEXT,                       -- folder under Artists/. NULL while hidden (no folder yet)
    hidden        INTEGER NOT NULL DEFAULT 0 CHECK (hidden IN (0,1)),
    avatar_path   TEXT,                       -- relative path or NULL (UI draws letter avatar)
    tags_seeded   INTEGER NOT NULL DEFAULT 0 CHECK (tags_seeded IN (0,1)), -- first-album tag copy already happened
    created_at    INTEGER NOT NULL,
    updated_at    INTEGER NOT NULL
);
CREATE UNIQUE INDEX ux_artist_name ON artist(name COLLATE NOCASE);
CREATE UNIQUE INDEX ux_artist_dir  ON artist(dir_name COLLATE NOCASE) WHERE dir_name IS NOT NULL;

CREATE TABLE album (
    id                INTEGER PRIMARY KEY,
    primary_artist_id INTEGER NOT NULL REFERENCES artist(id) ON DELETE RESTRICT,
    title             TEXT    NOT NULL,       -- for is_unsorted=1 this is '' (title is translated at display time)
    dir_name          TEXT    NOT NULL,       -- folder under Artists/<artist.dir_name>/
    type              TEXT    NOT NULL CHECK (type IN ('single','album','ep','live','compilation','other')),
    is_unsorted       INTEGER NOT NULL DEFAULT 0 CHECK (is_unsorted IN (0,1)),
    year              INTEGER NOT NULL CHECK (year BETWEEN 1744 AND 9999),
    month             INTEGER CHECK (month BETWEEN 1 AND 12),
    day               INTEGER CHECK (day BETWEEN 1 AND 31),
    cover_path        TEXT,                   -- relative path to album_cover.* or NULL
    created_at        INTEGER NOT NULL,
    updated_at        INTEGER NOT NULL,
    CHECK (day IS NULL OR month IS NOT NULL), -- day without month is invalid
    CHECK (is_unsorted = 0 OR (title = '' AND type = 'other'))
);
-- Albums exist only in the context of one primary artist (owner decision A1).
CREATE UNIQUE INDEX ux_album_title ON album(primary_artist_id, title COLLATE NOCASE) WHERE is_unsorted = 0;
CREATE UNIQUE INDEX ux_album_dir   ON album(primary_artist_id, dir_name COLLATE NOCASE);
CREATE UNIQUE INDEX ux_album_unsorted ON album(primary_artist_id) WHERE is_unsorted = 1;

-- Co-authors only. The primary artist is album.primary_artist_id. position >= 1, user-editable order.
CREATE TABLE album_artist (
    album_id  INTEGER NOT NULL REFERENCES album(id)  ON DELETE CASCADE,
    artist_id INTEGER NOT NULL REFERENCES artist(id) ON DELETE RESTRICT,
    position  INTEGER NOT NULL CHECK (position >= 1),
    PRIMARY KEY (album_id, artist_id)
);
CREATE INDEX ix_album_artist_artist ON album_artist(artist_id);

CREATE TABLE track (
    id            INTEGER PRIMARY KEY,
    title         TEXT    NOT NULL CHECK (length(trim(title)) > 0),
    duration_ms   INTEGER NOT NULL DEFAULT 0,
    file_path     TEXT    NOT NULL,           -- the ONE physical file (the "home" location)
    file_size     INTEGER NOT NULL,
    file_mtime    INTEGER NOT NULL,           -- for quick rescan
    codec         TEXT,                       -- mp3, flac, aac, vorbis, pcm, wma ...
    content_hash  BLOB    NOT NULL,           -- SHA-256 of the SOURCE file bytes at import time (never recomputed after tag writes)
    has_embedded_cover INTEGER NOT NULL DEFAULT 0,
    play_count    INTEGER NOT NULL DEFAULT 0,
    last_played_at INTEGER,
    created_at    INTEGER NOT NULL,
    updated_at    INTEGER NOT NULL
);
CREATE UNIQUE INDEX ux_track_path ON track(file_path COLLATE NOCASE);
CREATE UNIQUE INDEX ux_track_hash ON track(content_hash);   -- duplicate detection (owner decision B10)

-- Album membership. position defines track order and the "NN" file prefix in the album folder.
CREATE TABLE album_track (
    album_id  INTEGER NOT NULL REFERENCES album(id) ON DELETE CASCADE,
    track_id  INTEGER NOT NULL REFERENCES track(id) ON DELETE CASCADE,
    position  INTEGER NOT NULL CHECK (position >= 1),
    added_at  INTEGER NOT NULL,
    PRIMARY KEY (album_id, track_id),
    UNIQUE (album_id, position)
);
CREATE INDEX ix_album_track_track ON album_track(track_id);

CREATE TABLE playlist (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL CHECK (length(trim(name)) > 0),
    dir_name    TEXT NOT NULL,
    cover_path  TEXT,
    created_at  INTEGER NOT NULL,
    updated_at  INTEGER NOT NULL
);
CREATE UNIQUE INDEX ux_playlist_name ON playlist(name COLLATE NOCASE);
CREATE UNIQUE INDEX ux_playlist_dir  ON playlist(dir_name COLLATE NOCASE);

CREATE TABLE playlist_track (
    playlist_id INTEGER NOT NULL REFERENCES playlist(id) ON DELETE CASCADE,
    track_id    INTEGER NOT NULL REFERENCES track(id)    ON DELETE CASCADE,
    position    INTEGER NOT NULL CHECK (position >= 1),
    added_at    INTEGER NOT NULL,
    PRIMARY KEY (playlist_id, track_id),
    UNIQUE (playlist_id, position)
);
CREATE INDEX ix_playlist_track_track ON playlist_track(track_id);

-- Track credits. position 0 = first credited artist. role: 'main' or 'feat' (see SPEC 5.3).
CREATE TABLE track_artist (
    track_id  INTEGER NOT NULL REFERENCES track(id)  ON DELETE CASCADE,
    artist_id INTEGER NOT NULL REFERENCES artist(id) ON DELETE RESTRICT,
    position  INTEGER NOT NULL CHECK (position >= 0),
    role      TEXT    NOT NULL DEFAULT 'main' CHECK (role IN ('main','feat')),
    PRIMARY KEY (track_id, artist_id)
);
CREATE INDEX ix_track_artist_artist ON track_artist(artist_id);

-- Tags are a CLOSED, predefined vocabulary (owner decision B11). `key` is a canonical
-- English slug from resources/genres.json (e.g. 'rock', 'hip-hop'). Localised names live
-- in the resource file, NOT in the database. Unknown keys must be rejected by the service layer.
CREATE TABLE artist_tag (artist_id INTEGER NOT NULL REFERENCES artist(id) ON DELETE CASCADE, tag_key TEXT NOT NULL, PRIMARY KEY (artist_id, tag_key));
CREATE TABLE album_tag  (album_id  INTEGER NOT NULL REFERENCES album(id)  ON DELETE CASCADE, tag_key TEXT NOT NULL, PRIMARY KEY (album_id,  tag_key));
CREATE TABLE track_tag  (track_id  INTEGER NOT NULL REFERENCES track(id)  ON DELETE CASCADE, tag_key TEXT NOT NULL, PRIMARY KEY (track_id,  tag_key));
CREATE INDEX ix_album_tag_key  ON album_tag(tag_key);
CREATE INDEX ix_artist_tag_key ON artist_tag(tag_key);
CREATE INDEX ix_track_tag_key  ON track_tag(tag_key);

-- "Recently Played" carousel source: albums and playlists, max 50, no duplicates, newest first.
CREATE TABLE recent_item (
    kind      TEXT    NOT NULL CHECK (kind IN ('album','playlist')),
    item_id   INTEGER NOT NULL,
    played_at INTEGER NOT NULL,
    PRIMARY KEY (kind, item_id)
);
CREATE INDEX ix_recent_played_at ON recent_item(played_at DESC);

-- Small library-bound key/value store: 'session' (queue+position JSON), 'home_seed', etc.
CREATE TABLE kv (key TEXT PRIMARY KEY, value TEXT NOT NULL);

PRAGMA user_version = 1;
