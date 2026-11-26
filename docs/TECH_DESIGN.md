# Semplice - Technical design (for owner review)

Status: **draft for approval, no code written yet.** Sources: `SPEC.md`, `UI-reference.md` (pages 3, 24, 36 checked against text), `schema/0001_init.sql`, and the current state of the repository.

**Section 8 (contradictions and gaps) is the part that needs your decisions. Items marked ⛔ block M0.**

---

## 1. Module and class overview

Dependencies point downward only. `core` knows nothing about Qt GUI/QML.

```
app/        main.cpp, AppBootstrap (composition root, manual DI)
  |
qml/        Main.qml, theme/, components/, views/, panels/      (presentation only)
  |
ui/         view-models + list models + ImageProvider           (QtGui/QtQml allowed)
  |
platform/   win/: Smtc, TaskbarThumbBar, RecycleBin, CodepageConverter, SingleInstance, CrashHandler
  |         (every class implements an interface owned by core; non-Windows builds get null/test fakes)
core/       domain/  db/  services/  storage/  tags/  audio/  util/  recommender/  playback/
```

### 1.1 `core` (static library `semplice_core`, Qt Core + Sql only)

| Folder | Classes (namespace `semplice`) | Role |
|---|---|---|
| `util/` | `Result<T>`, `Error`, `ErrorCode`, `Job`/`JobHandle`, `Normalizer`, `Logger`, `Clock` (injectable), `Rng` (seedable) | basics, no dependencies |
| `domain/` | `Artist`, `Album`, `AlbumType`, `Track`, `Playlist`, `TrackCredit`, `ReleaseDate`, `TagKey`, ids as strong typedefs (`ArtistId`...) | plain structs/enums, validation helpers (`ReleaseDate::validate`) |
| `db/` | `Database` (open, pragmas, connections), `Migrator`, `WriterQueue`, repositories: `ArtistRepo`, `AlbumRepo`, `TrackRepo`, `PlaylistRepo`, `TagRepo`, `RecentRepo`, `KvRepo`; `schema/NNNN_*.sql` | persistence, returns domain structs only |
| `storage/` | `PathSanitizer`, `FileStorageService` (implements `IFileStorage`), `OperationJournal`, `LibraryLocator` (create/open/validate root, `library.json`, backups, `quick_check`), `CopyJob` (SHA-256 while copying) | **the only code that touches library files** |
| `tags/` | `TagService` (`ITagService`, TagLib), `GenreMap`, `GenreVocabulary` (loads `genres.json`), `EncodingFixer`, `ICodepageConverter`, `ArtistStringSplitter` | read/write metadata |
| `audio/` | `IAudioBackend`, `QtAudioBackend` (the only user of `QMediaPlayer`/`QAudioOutput`), `FakeAudioBackend` (tests) | output |
| `playback/` | `QueueModel` (groups + order), `ShuffleBag`, `PlaybackController` (state machine, `IPlaybackService`), `SessionStore`, `PlayStatsTracker` (30 s/50 % rule) | pure logic, driven by `IAudioBackend` |
| `recommender/` | `RecommenderConfig`, `Scorer` (pure), `Selector` (constraints, look-ahead, relaxation), `RecommenderService` | M9 |
| `services/` | `LibraryService` (`ILibraryService`), `ImportService`, `RescanService`, `SearchService` + `SearchIndex`, `CategoryService` + `CategoryGenerator` (pure), `SettingsService` (QSettings wrapper), `HomeService` (greeting, recent) | use-cases; orchestrate repos + storage + tags in journaled operations |

### 1.2 Interfaces that tests fake
`IFileStorage`, `ITrash`, `ITagService`, `ICodepageConverter`, `IAudioBackend`, `IClock`, `ISettings`, `ILibraryService`, `IImportService`, `IPlaybackService`, `ISearchService`, `ICategoryService`.

### 1.3 Library-level object graph (created in `AppBootstrap`, owned by a `LibrarySession`)
Opening/changing a library = destroying one `LibrarySession` and creating another. It owns `Database`, repos, `FileStorageService`, `OperationJournal`, `SearchIndex`, all services. App-level objects (settings, logger, theme, translator, audio backend, platform glue) live outside it.

---

## 2. Public interfaces of `core` services (final shapes)

All fallible calls return `Result<T>` (`std::expected`-style, `Error` on failure). Long operations return a `JobHandle` (cancel, progress signal, finished signal with `Result`). Signatures are indicative; exact parameter structs are finalised in M1-M4.

```cpp
struct Error { ErrorCode code; QString symbol /*"E_PATH_TOO_LONG"*/; QString devMessage; QString userMessageKey; };
template<class T> class Result;           // value or Error; Result<void> for no value

// ---- ILibraryService (M4): all mutating operations are journaled use-cases
class ILibraryService : public QObject {
  // artists
  Result<ArtistId> createArtist(const ArtistDraft&);                 // visible (hidden=false) unless draft.hidden
  Result<void>     updateArtist(ArtistId, const ArtistPatch&);       // rename => folder rename (4.3)
  DeletePlan       planDeleteArtist(ArtistId) const;                  // computes outcome for the dialog text
  Result<void>     deleteArtist(ArtistId);
  // albums
  Result<AlbumId>  createAlbum(const AlbumDraft&);                    // from import form or empty
  Result<void>     updateAlbum(AlbumId, const AlbumPatch&);
  DeletePlan       planDeleteAlbum(AlbumId) const;  Result<void> deleteAlbum(AlbumId);
  // playlists
  Result<PlaylistId> createPlaylist(const PlaylistDraft&);  /* update/plan/delete as above */
  // tracks
  Result<void> updateTrack(TrackId, const TrackPatch&);               // title, credits, tags
  Result<void> addTrackToAlbum(TrackId, AlbumId);   Result<void> addTrackToPlaylist(TrackId, PlaylistId);
  Result<void> removeTrackFromAlbum(TrackId, AlbumId);  /* ... Playlist */
  DeletePlan   planDeleteTrack(TrackId) const;       Result<void> deleteTrack(TrackId);
  Result<void> addToDiscography(TrackId, ArtistId);                   // materialises Unsorted (5.2)
  Result<void> materialiseHiddenArtist(ArtistId);                     // "add to discography" on hidden card
  // queries used by view-models are on read-only `ILibraryQuery` (separate, thread-safe, own connections)
signals: artistChanged(ArtistId) albumChanged(AlbumId) playlistChanged(PlaylistId) trackChanged(TrackId)
         artistVisibilityChanged(ArtistId) libraryReset();
};
```
`DeletePlan` is a value type (what will be deleted / hidden / kept / re-homed, counts, list of files going to the Recycle Bin). The dialog text is generated from it (M8) so it always matches the real outcome.

```cpp
// ---- IImportService (M4)
ImportScan   scan(const QList<QUrl>&)  -> JobHandle<ImportScan>;      // files, tags (EncodingFixer applied), cover candidates, guessed type
ImportKind   classify(const ImportScan&) const;                        // Single/EP/Album or Playlist (UI doc p.29)
JobHandle<ImportResult> commit(const ImportRequest&);                  // copy+hash, tag write, one DB transaction, cancellable + rollback
// ---- IFileStorage (M2): the ONLY file mutator
Result<StoredFile> importCopy(src, destRelative, progress, cancel);   // hash while copying
Result<void> move(from, to);  Result<void> renameDir(...);  Result<void> trash(rel); Result<void> replaceFile(tmp, dst);
// all of them are called inside an OperationJournal::Scope, never directly by services
// ---- ITagService (M3)
Result<TrackTags> read(absPath);  Result<void> write(absPath, const TrackTags&);  // temp copy + atomic replace
// ---- IAudioBackend (M5)
load/play/pause/stop/seek(ms)/setVolume(0..1)/position()/duration()/preload(path)/release(path) ; signals: stateChanged, positionChanged, mediaEnded, errorOccurred
// ---- IPlaybackService (M5)
playGroups(QList<QueueGroup>, startGroup, startIndex), togglePlay, next, previous, seek, setShuffle/Repeat, goToGroup(i), current(), queueSnapshot() ; signals for UI
// ---- ISearchService (M6): query(text) -> {artists[5+more], albums, playlists, tracks}, synchronous on the in-memory index (< 100 ms), updated by library signals
// ---- ICategoryService (M7): recentlyPlayed(), generateCategories(count, seed) ; pure CategoryGenerator inside
```

---

## 3. Database: migration 0001 and changes to the draft

Location: `src/core/db/schema/0001_init.sql`, embedded as Qt resources, applied by `Migrator` using `PRAGMA user_version`. Pragmas at open: `foreign_keys=ON`, `journal_mode=WAL`, `synchronous=NORMAL`, `busy_timeout`.

The draft DDL is sound. I propose **these changes** (each is small, all are reversible only by a new migration, hence flagged for your approval):

| # | Change | Why |
|---|---|---|
| D1 | Add `track.home_kind TEXT CHECK (home_kind IN ('album','playlist'))` and `track.home_id INTEGER` (both NOT NULL) | SPEC 4.2 needs to know *where the file physically lives*. Deriving it from the path or from `added_at` is fragile (the home location changes by the 4.2 move rule, not by membership order). Cheap to store, avoids guessing. No FK (polymorphic); integrity enforced by service + a consistency check in Rescan. |
| D2 | `album_track` and `playlist_track`: replace `UNIQUE(album_id, position)` by a **non-unique index** | SQLite checks UNIQUE per row, so renumbering (insert in the middle, reorder, delete) fails mid-update unless done in two steps. Position integrity is enforced by the service (always rewrites 1..n in one transaction) and by a test. |
| D3 | Add `track.number_hint INTEGER NULL` | (optional) original tag track number to prefill the import form; not needed after import. **Dropped unless you want it** - default is not to add it. |
| D4 | Add `playlist_track.file_dir_hint` : **no**. (Considered, rejected.) | -- |
| D5 | Add `artist.sort_name TEXT` : **no** (not required by the spec; alphabet sort uses the normalised name). | -- |
| D6 | `album.year` check: keep `1744..9999` in SQL; the upper bound "current year + 1" is enforced in code (clock-dependent, cannot live in a CHECK) | -- |
| D7 | Add `track_tag.source`? **no.** | -- |
| D8 | Add index `ix_track_last_played ON track(last_played_at)` and `ix_album_year ON album(year, month, day)` | recommender penalty + year sort/carousels. |
| D9 | Add table `schema_note`? **no.** | -- |

Only D1, D2, D8 are real changes; D3-D7, D9 are "considered and not done" so you can see they were looked at (D3 is the only one you might want).

Unchanged and important: collations are `NOCASE` on **NFC-normalised, trimmed** text, so the service layer **must normalise before every write and every lookup** (`Normalizer::identity()`); SQLite `NOCASE` only folds ASCII, so *true* case-insensitive uniqueness for Cyrillic/Greek/etc. is enforced by the service through a stored **`name_key`** column (casefolded + diacritics-stripped, via `Normalizer`) with a `UNIQUE` index. **This is change D10:** add `name_key TEXT NOT NULL` to `artist` (unique), `playlist` (unique), and `title_key` to `album` (unique per primary artist), replacing the `COLLATE NOCASE` unique indexes (kept as secondary safety nets). Without it "Ёлка" and "ёлка" would be two different artists.

Hash index: `ux_track_hash` stays. Note it hashes **source** bytes, so Rescan cannot use it to recognise a library file whose tags were rewritten (see gap G6).

---

## 4. Thread model

| Thread | Work |
|---|---|
| GUI thread | QML, view-models, list-model *access* (returns cached rows), playback state machine, `QtAudioBackend` (Qt Multimedia objects live here) |
| **DB writer** (one `QThread` with an event loop / `WriterQueue`) | every INSERT/UPDATE/DELETE, migrations, `VACUUM`, backups. Callers submit a lambda/command and get a future/signal back. Multi-row changes = one transaction inside one command. |
| **Reader pool** (`QThreadPool`, max ~4) | SELECTs for models/views, each pool thread owns a read-only connection (`QSqlDatabase::cloneDatabase`, connection name per thread, WAL allows parallel reads). |
| **Job pool** (`QThreadPool`, 2-3 threads) | import scan/commit, rescan, tag writes, thumbnail generation, recommender look-ahead. Each is a `Job` with `cancel()`, progress, and a result delivered by queued signal. File mutations still go through `FileStorageService` + journal on that worker; DB changes of a job are handed to the writer queue. |
| Image provider threads | `QQuickAsyncImageProvider` runs on Qt's pool; reads thumbnails from `.library/covers`, requests generation from the job pool if missing. |

Rules: no DB/TagLib/disk call > 5 ms on the GUI thread (a debug-build `GuiThreadGuard` logs violations). Data crosses threads as value types (domain structs), never as `QSqlQuery`/`QObject*`. The in-memory `SearchIndex` is guarded by a read/write lock and updated from domain-change signals.

**Journaled operation order** (SPEC 4.5): write journal entry (flushed) -> file steps (each step is idempotent and recorded) -> DB transaction on the writer -> delete journal entry. Startup recovery runs *before the UI is shown*, per entry decides roll-forward/back from actual file + DB state.

---

## 5. QML models and view-models

Principle: QML only binds. Every screen has one C++ view-model (`QObject` with properties + invokables) exposed through `QML_ELEMENT`/context singletons registered in the `Semplice` QML module.

- **List models** (virtualised, lazy): `ArtistListModel`, `AlbumListModel`, `PlaylistListModel`, `TrackListModel` (album/playlist/search/queue variants). Each is a `QAbstractListModel` that holds only ids + a small page cache; `canFetchMore/fetchMore` (or a `rowsWindow` approach for `GridView`) loads pages of ~100 rows on the reader pool and emits `dataChanged` on arrival. Sort/filter is done in SQL (never a QML proxy over 50k rows).
- **View-models**: `ShellVm` (window, history back/forward stack, which central view is open), `LibraryPanelVm`, `HotbarVm`, `SearchVm`, `PlayerVm`, `SidePanelVm`, `QueueVm`, `HomeVm` (+ `CarouselModel`), `ArtistCardVm`, `AlbumCardVm`, `PlaylistCardVm`, `ArtistEditorVm`, `AlbumEditorVm` (also used for import), `PlaylistEditorVm`, `TrackEditorVm`, `SettingsVm`, `BannerVm`, `DialogVm`.
- **Navigation**: `ShellVm` keeps a history stack of `ViewRef { kind, id, params }`; Back/Forward/Home are commands on it; the middle area is a `StackView`-less `Loader` switched by `ViewRef.kind`.
- **Theme**: `qml/theme/Theme.qml` singleton (colours, spacing 4/8 grid, radii, type scale, icon sizes, durations); `Icons.qml` maps names to SVG resources. `docs/DESIGN_TOKENS.md` is generated/maintained alongside.
- **Controls style**: `QtQuick.Controls.Basic` as the base and custom components (`SButton`, `SIconButton`, `SSlider`, `STextField`, `SDialog`, `Banner`, `Carousel`, `TrackRow`, `CoverImage`). No native styles (consistent look, no platform surprises).
- **Images**: `image://cover/<kind>/<id>/<size>` through one async provider + thumbnail cache (128/256/512).

---

## 6. CMake / Qt Creator project layout

```
Semplice/
  CMakeLists.txt            top-level: project(), options, FetchContent(TagLib pinned), add_subdirectory(...)
  CMakePresets.json         windows-msvc-debug / -release, linux-core (core + tests only); + a `windows-mingw-*` pair if you decide G1 that way
  cmake/                    Warnings.cmake, Version.cmake (generates version.h), TagLib.cmake
  src/
    core/CMakeLists.txt     semplice_core (STATIC)
    platform/CMakeLists.txt semplice_platform (STATIC; win/ sources only under WIN32)
    ui/CMakeLists.txt       semplice_ui (STATIC, with qt_add_qml_module for the C++-exposed types)
    qml/CMakeLists.txt      QML module `Semplice` (QML_FILES, RESOURCES: SVG, fonts, genres.json)
    app/CMakeLists.txt      executable Semplice (WIN32), manifest .rc/.manifest, icon
  resources/                icons/ fonts/ tags/genres.json license/
  translations/             app_en/ru/zh_CN/ja .ts  (qt_add_translations)
  tests/ unit/ integration/ tools/ (synthetic generator, benchmark)
  docs/
  installer/ scripts/       (M11)
```

- **Qt Creator**: opening the folder reads `CMakePresets.json`; presets use the Ninja generator and `${sourceDir}/build/<preset>` as binary dir. The kit's Qt is found by Qt Creator, so presets do **not** hard-code a Qt path (they honour `CMAKE_PREFIX_PATH` from the kit). Tests are registered with `qt_add_test`/`add_test`, so they appear in Qt Creator's Tests pane and run with `ctest`.
- **Warnings**: MSVC `/W4 /permissive-`, GCC/Clang `-Wall -Wextra -Wpedantic`; `/WX` (`-Werror`) for `semplice_core` from day one.
- **TagLib**: `FetchContent` with a pinned release tag, built as a static lib, `BUILD_SHARED_LIBS=OFF`, no zlib dependency required for our formats (TagLib 2 builds without it; compressed ID3 frames are rare - noted in `DECISIONS.md` if it matters).
- **Existing starter files** (`main.cpp`, `Main.qml`, root `CMakeLists.txt`, `importedcontent/`): replaced in M0. The Figma-import hook in the starter CMake is dropped (you said Figma exports arrive as SVG/PNG in `docs/design/`, not as a Qt module). The `build/` and `.qtcreator/` folders are generated by Qt Creator and stay git-ignored.

---

## 7. Risk list

| # | Risk | Impact | Mitigation |
|---|---|---|---|
| R1 | Toolchain mismatch (see G1) | M0 cannot start; SMTC/taskbar code is hard under MinGW | decide G1 first |
| R2 | Qt Multimedia FFmpeg backend: gapless, exact seek on VBR MP3, WMA support, file handle release for tag writes | playback glitches; tag write blocked | everything behind `IAudioBackend`; `release(path)` = stop+unload+restore position; manual QA list; BASS later |
| R3 | TagLib write correctness across 6 formats (WAV, WMA weak) | corrupted/unwritable files | temp copy + atomic replace; round-trip tests per format; failure never fails import (SPEC 6.2) |
| R4 | Journal/recovery bugs (data loss) | worst-case outcome | fault-injection at every step, written first; no permanent deletes anywhere |
| R5 | `NOCASE` ASCII-only folding | duplicate artists in Cyrillic/Greek | `name_key` columns (D10) |
| R6 | Carousel (`PathView`, 50 items) performance and drag feel | 60 fps budget | prototype in M0/M7 with placeholder items; only visible delegates |
| R7 | Long Windows paths (> 260) | import failures | manifest `longPathAware`, `\\?\` prefixes in `FileStorageService`, `E_PATH_TOO_LONG` |
| R8 | EncodingFixer false positives | corrupts correct tags | only touch Latin-1/ID3v1 text; plausibility scoring; user reviews prefilled form |
| R9 | Fractional DPI / CJK font fallback | blurry or clipped text | `PassThrough` rounding, SVG icons, DPI matrix in M10 |
| R10 | 200k-track memory/time (search index, thumbnails) | budgets | compact index (ids + normalised UTF-16/UTF-8 arrays), lazy models, benchmark from M1 |
| R11 | Scope: the spec is large (11 milestones) | schedule | strict milestone gating as in the plan |
| R12 | I cannot run Windows-only paths in a non-Windows sandbox | untested code | this session *is* on Windows, but I only claim "verified" for what I actually build/run; note per milestone |

---

## 8. Contradictions, gaps and proposed resolutions

**⛔ G1 - Toolchain (blocks M0).** The SPEC says **MSVC 2022 + Qt 6.8 LTS**. Your machine's Qt Creator project (`.qtcreator`, `build/`) was created with **Qt 6.11.2, MinGW 64-bit** (`MinGW Makefiles`), and the starter CMake requires `qt_standard_project_setup(REQUIRES 6.10)`. Visual Studio 2022 is installed. I cannot see where Qt is installed (no `C:\Qt` / `D:\Qt` found), so I do not know which kits you have.
*Proposal:* target **Qt 6.8+ (accept 6.8-6.11), MSVC 2022**, because SMTC (WinRT), `ITaskbarList3`, `IFileOperation` and `/W4` are straightforward with MSVC and awkward with MinGW. If your Qt installation has no "MSVC 2022 64-bit" kit, install it via the Qt Maintenance Tool (same Qt version is fine). *Alternative:* keep MinGW and drop SMTC/WinRT niceties or hand-write the COM headers (not recommended). The code will not use anything newer than 6.8 so either version works. **Please tell me: which kit do you want, and is an MSVC kit available?**

**G2 - CMake minimum.** The starter says 3.16; SPEC says >= 3.25 (presets v6 need 3.25). *Resolution:* 3.25 (your CMake is already installed under `D:\Programs\CMake`; I will check its version in M0).

**G3 - Uniqueness of names beyond ASCII** (R5). Resolution D10 (`name_key`).

**G4 - Position uniqueness** (D2). Resolution: service-enforced.

**G5 - Home location is implicit** (D1). Resolution: store it.

**G6 - Rescan "hash only if changed" vs hash-of-source.** The stored hash is of the *source* file; after tag writing the library copy's bytes differ, so a hash can never re-identify a library file. *Resolution:* Rescan compares size+mtime to the stored values (stored *after* the tag write); if changed, it re-reads tags and updates size/mtime but never touches `content_hash`. For orphan files (not in DB) it hashes and checks against `content_hash`; a match is treated as a duplicate of an existing track (extremely rare, only if the user copied an untouched original). Documented in `DECISIONS.md`.

**G7 - Artist-string splitting default** (SPEC 5.3: split on `&` only if the whole string is not an existing artist). It is silent about *names that contain `&` and are new* ("Simon & Garfunkel" on a fresh library would be split into two artists). *Proposal:* import form shows the split as editable chips (as the spec says the user can fix credits), and the splitter has a small built-in exception list; the owner's open item #1 covers confirming this.

**G8 - Unsorted album for visible artists vs `album_track.position`.** The Unsorted folder holds files named `NN - Title.ext` like normal albums; adding a track to discography appends. Fine, no conflict, noted for completeness.

**G9 - Album title "required" (DEFAULT) vs the UI doc** (a blank album name "remains blank by default" for auto-fill, but cannot be saved blank). Resolution: Save disabled while empty (SPEC 8.3 wins).

**G10 - "Add" button vs "+" button.** UI doc p.17 calls it "Add" at the top of the Library panel; the mock-up (p.3) shows a `+` icon. SPEC 8.5 says `+`. *Resolution:* `+` icon button with tooltip "Add".

**G11 - Playlist track file names `Title - Artist.ext`** can collide (same title+artist, different files). Collision rule 4.1 (` (2)`) covers it.

**G12 - Repeat-all in a multi-group queue and "recommendations".** SPEC 7.2: repeat-all repeats the *current group*; so with repeat-all on, recommendations never start for that group. Implemented as written; mentioned because it matches the album-card text on UI p.44.

**G13 - Delete key on lists** is in the shortcut table but there is no multi-select and no selected-row concept in the UI document for library lists (hover only). *Proposal:* "selected" = keyboard-focused row; Delete acts on it with confirmation. Minor; decided in M6.

**G14 - Default `Result`.** The SPEC says `Result<T, Error>`; I will implement it as `Result<T>` with `Error` fixed (alias `Result<T, Error>` available) - C++20 has no `std::expected` (C++23). Own small implementation, ~100 lines.

**G15 - Qt SQL driver and `VACUUM INTO`, `PRAGMA quick_check`** work with the bundled SQLite in Qt's `QSQLITE` driver (SQLite >= 3.27 needed for `VACUUM INTO`; Qt 6.8 bundles recent). Verified in M1 tests; if the driver is older the backup falls back to file copy after a checkpoint.

---

## 9. What I will do on approval

1. Resolve G1 with you (kit), then start **M0** exactly as in the plan: write `docs/design-notes/M0-skeleton.md`, stop for your review (M0 only), then implement.
2. Apply D1, D2, D8, D10 in `0001_init.sql` (final copy under `src/core/db/schema/`, the `docs/schema` draft stays as history) unless you object.
3. Because you will test: every milestone report ends with a numbered "what to click / run" checklist and a list of what I could not run myself.
