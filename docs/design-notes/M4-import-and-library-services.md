# M4 - Import and library services: design note

Goal: the complete non-UI behaviour of managing a library, on real files: import, edit, rename, delete, rescan, switch library. Everything runs against the journaled storage of M2, writes tags with M3, and is tested on a real folder plus a real database. `core` only. Toolchain: MinGW is now the default (decision 56); MSVC is kept as a second compiler check.

## 1. The big change: `LibraryService` becomes two-phase (SPEC 4.5)
Until M3 the service committed first and handed the file effects to `IFileStorage::apply()` afterwards. Now every mutating call is:

```
DB transaction opens  ->  rules run, rows change, FileOps are collected
journal entry written (begin)             \
phase A: create folders, COPY, MOVE        |  journaled, cancellable, progress
in-place tag writes of brand-new files     |
commit marker (op_log) + DB COMMIT        /
phase B: Recycle Bin, journal entry removed
after-commit: tag sync of touched files (failures never roll anything back)
```
Failure or cancel before the commit undoes the files (journal) and the rows (rollback); a crash is repaired at the next start by `recover()` using the commit marker. `IFileStorage` gets `begin()` (returns an `IStorageOperation`), so fakes (`RecordingFileStorage`) keep working and the M1 tests stay valid.

**Delete-before-move conflicts.** Phase A runs before the Recycle-Bin phase. A step list such as "trash `01 - Intro.mp3`, then rename `02 - Intro.mp3` to `01 - Intro.mp3`" would collide. `LibraryService` therefore rewrites a `Trash(P)` whose path is reused later in the same operation into `Move P -> P.semplice-del-N` (phase A, undoable) plus `Trash(P.semplice-del-N)` (phase B).

## 2. New and extended pieces
| Piece | What |
|---|---|
| migration `0003_tags_dirty.sql` | `track.tags_dirty`: set inside the transaction for every track whose file tags must change; cleared after a successful tag write. A crash or a locked file leaves it set; **Rescan retries** (SPEC 6.2) |
| `TagSync` (inside `LibraryService`) | builds a `TagWrite` from the database (title, credits, album/authors of the *home* album, track number/total, date, genres = track tags + home album tags in English, album cover). Playlist-homed tracks leave album fields untouched. New files are written in place (`TagService::writeNewFile`), existing files through temp copy + replace |
| `ArtistStringSplitter` | SPEC 5.3: `;`, ` feat. `, ` ft. `, ` featuring `, ` & ` (only if the whole text is not an existing artist); before the feat separator = main, after = feat; never `,` or `/` |
| `LibraryService` edits | `updateArtist` (rename = folder rename + every path + tags, avatar, tags), `updateAlbum` (rename, type, date, tags, cover, authors incl. primary change = folder move, reorder), `updatePlaylist`, `updateTrack` (title, credits, tags), `appendTracksToAlbum/Playlist`; drafts accept artist *names* (created in the same transaction, hidden unless they author an album) |
| `ImportService` | `scan` (recursive, supported formats, tags, EncodingFixer, SHA-256, duplicate detection against the library, skipped files with reasons), `propose` (classification 1-3 Single / 4-7 EP / 8+ Album if the tracks share album metadata or more than half share an artist, otherwise playlist; prefilled form), `commitAlbum` / `commitPlaylist` (copy, cancel with rollback, progress) |
| `RescanService` | SPEC 6.5 on a worker-ready API: files not in the database -> moved to `Playlists/Rescan <date time>/` and imported there, duplicates by hash are not duplicated (the stray copy goes to the Recycle Bin), rows whose file vanished are removed with the normal cascades, changed size/mtime refreshed, dirty tags retried, maintenance |
| `Maintenance` | `PRAGMA optimize`, `VACUUM` when more than 20 % of the pages are free, backup rotation (M2) |
| `LibrarySession`, `LibrarySwitcher` | one object per open library (database, storage, services, search index); folder classification for "Select folder" (empty -> create, library -> open, other -> E_FOLDER_NOT_EMPTY, missing stored path -> "locate your library"), `ISettings::libraryPath` |
| `JobContext` | `std::stop_token` + progress callback passed to long calls |

Threading: the services are single-threaded objects; the UI layer (M6) will run all of them on one dedicated worker thread that owns the `LibrarySession`. M4 tests call them directly.

## 3. Import details
* **Hashing.** `scan` hashes every source once (SHA-256, streaming) so duplicates are known before the user saves; the copy hashes again and a mismatch (source changed meanwhile) fails and rolls back. A duplicate is never copied: the existing track gets the extra membership and the report says so.
* **Cover.** user choice > `cover.*`/`album_cover.*` in the source folder > embedded picture of the first track. Embedded pictures are first written to `.library/tmp/<uuid>.<ext>` (own housekeeping file, swept at start) and then copied by the journaled storage like any file; stored as `album_cover.<ext>`; also embedded in the library copies of the tracks.
* **Names.** the form holds artist *names*; existing artists are reused (hidden ones become visible when they author an album), new authors are created visible, credits-only artists hidden. A playlist track without any artist tag is credited to the hidden artist "Unknown artist".
* **Tags in the copies.** written before the commit (the new files belong to the uncommitted operation); `file_size`/`file_mtime` are stored after that. A failed tag write keeps the import and sets `tags_dirty`.

## 4. Tests (all on a real temp folder + real SQLite file, real tiny audio files)
* import: scan (formats, tags, mojibake repair, skipped files, duplicates, hashes), classification table (3/4/7/8 tracks, shared album, majority artist, mixed -> playlist, preselected artist), prefill, commit album/playlist (paths, DB, tags in the files equal the DB, cover stored and embedded, sources byte-identical, journal empty), duplicate import, cancel at every chunk (nothing left), **crash at every checkpoint of an import** (recover, then disk == database, all-or-nothing), unreadable file in the middle.
* edits: rename artist/album/playlist/track (case-only included), type/date/tags/cover, reorder, author change, credits change with hidden-artist GC, tag-write failure on a locked file (DB change stands, `tags_dirty`, retried by rescan); after each step: files == database and tags in files == database.
* delete: the two artist outcomes of the UI document with real files, album, playlist, track.
* rescan: manual files, duplicates, missing files, changed files, dirty retry, cancel, maintenance.
* session/switcher: first launch, non-empty folder, existing library, missing stored path, newer schema.

## 5. Decisions to record
Delete-conflict rewrite (move aside + Recycle Bin phase B); `tags_dirty` instead of an in-memory retry list; duplicates found by Rescan are sent to the Recycle Bin (recoverable) rather than left in place; scan hashes up front; `Unknown artist` hidden credit for playlist tracks without artist; new files get their tags written in place before the commit.
