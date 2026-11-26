# Semplice - Technical Specification (v1.0)

Status: authoritative technical addendum. Written from the owner's UI document (`docs/source/UI.pdf`, text + screenshots in `docs/UI-reference.md`) and the owner's answers to the design questions.

## 0. How to read this document

**Sources and precedence** (highest first):
1. This SPEC (decisions made after the UI document).
2. The UI document, sections under the heading **"ACTUAL INFORMATION"** (Playback rules, Recommendation system, Player header titles). These are the newest part of the UI document.
3. The rest of the UI document.
4. The screenshots (layout intent only - see 8.2).

If two sources conflict, follow the higher one and note it in `docs/DECISIONS.md`. If *nothing* covers a case, do not guess silently: pick the safest option, record it in `docs/DECISIONS.md` (what, why, alternatives) and continue.

**Markers**
- **DECIDED** - the owner answered explicitly.
- **DEFAULT** - not explicitly confirmed; chosen to keep work moving; flagged in section 14 for later confirmation. Implement it, but keep it easy to change (config constant or one function).

**Naming:** the product, repository, CMake project, executable and installer are all called **Semplice**. The words "The Player" / "MusicPlayer" in the UI document mean Semplice. Namespace: `semplice`. Executable: `Semplice.exe`.

## 1. Product summary and scope

Semplice is a fully offline Windows desktop music player with its own **managed library**. The user picks an empty folder (the library root); Semplice creates the folder tree, **copies** imported music into it, and indexes everything in SQLite. The library is organised around **artists -> albums (albums, singles, EPs, live, compilations, other)** plus free-form **playlists**.

- Platform: Windows 10 1809+ and Windows 11, x64.
- UI languages: English (source), Russian, Chinese (Simplified), Japanese.
- Audio formats: MP3, FLAC, M4A/AAC, OGG Vorbis, WAV, WMA. Image formats: JPEG, PNG.
- Free and open source, distributed as a standalone installer. No network features, no auto-update, no telemetry.

**Out of scope for v1.0 (do not build, but do not paint the design into a corner):** other themes, equalizer/ReplayGain/crossfade, BASS backend, queue editing ("play next", reorder), mood tags, right-click context menus, multi-select, file-association "Open with", auto-update, signing of the installer.

## 2. Toolchain and stack (DECIDED unless marked)

| Item | Choice |
|---|---|
| Language | C++20 |
| UI | Qt 6.8 LTS, Qt Quick / QML |
| Build | CMake (>= 3.25) + `CMakePresets.json` |
| Compiler | MSVC 2022 (x64) |
| IDE | **Qt Creator** is the owner's IDE: the project must open, configure, build, run and debug there with no manual steps. VS Code is used only for Git. |
| DB | SQLite via Qt SQL (`QSQLITE`) |
| Tags | TagLib 2.x (read **and write**) |
| Audio v1 | Qt Multimedia (use the FFmpeg backend, Qt's default on Windows since 6.5) behind `IAudioBackend` |
| Tests | QtTest + CTest (runs inside Qt Creator) |
| Installer | Inno Setup |
| Dependencies (DEFAULT) | TagLib via CMake `FetchContent` with a pinned tag (simplest to keep Qt Creator-friendly; vcpkg manifest mode is an acceptable alternative if it works in Qt Creator without manual steps). No other third-party libraries without asking. |

Licensing note for the owner: Qt (LGPL-3, link dynamically and ship via `windeployqt`), TagLib (LGPL/MPL), Qt's FFmpeg (LGPL), Inter font (OFL) all have attribution/relinking obligations. Claude Code must create `THIRD_PARTY_NOTICES.md` listing every shipped component and license (see M10).

**Environment rule:** Windows-only code (shell integration, taskbar, SMTC, Recycle Bin, code-page conversion) lives behind interfaces in `src/platform/` and is compiled only under `Q_OS_WIN`. `core/` must compile and pass tests on any OS with Qt Core + Sql + Test, so it can be verified in a Linux sandbox. Never claim UI behaviour or Windows integration is verified unless it was actually run on Windows.

## 3. Architecture

Layering (dependencies point downward only):

```
qml/          Views, components, theme                      (QML only, no business logic)
ui/           C++ models, view-models, image provider        (Qt GUI/QML types allowed)
platform/     Windows integration (SMTC, taskbar, trash...)  (Qt + WinAPI/WinRT, behind interfaces)
core/         domain, db, services, storage, tags, audio     (Qt Core + Sql only; NO QML/GUI)
app/          main.cpp, composition root (manual DI)
```

- `core` is a static library, `semplice_core`. Services expose interfaces (`ILibraryService`, `IImportService`, `IPlaybackService`, `ISearchService`, `ICategoryService`, `ISettingsService`, `IAudioBackend`, `ITagService`, `IFileStorage`, `ITrash`, `ICodepageConverter`) so tests can fake them.
- **Errors:** no exceptions across module boundaries. Fallible functions return `Result<T, Error>`. `Error` has a stable numeric `code`, a symbolic name (e.g. `E_PATH_TOO_LONG`), a developer message and a translatable user message key. The UI shows an error banner for it; any error without a user message shows the generic banner **"Error -1"** (UI doc).
- **Threading:** the GUI thread never touches the filesystem, TagLib or the database for anything that can take > 5 ms. Import, rescan, tag writes, thumbnail generation and recommendation look-ahead run on worker threads (`QThreadPool`/`QtConcurrent` or a dedicated `Job` abstraction with progress + cancel). SQLite: WAL mode; one **serialised writer** (dedicated thread/queue); readers use their own connection per thread. Results return to the UI through signals.
- **Domain change notifications:** services emit fine-grained signals (`albumChanged(id)`, `artistVisibilityChanged(id)`...) so models update without full reloads.
- **In-memory search index:** at startup build a compact index of normalised artist/album/playlist/track names (casefolded, diacritics stripped, NFC), update it incrementally on change. Substring match, ranked prefix > word-start > substring. This meets the search budget and works for CJK. Do not use SQL `LIKE` for the search box.

## 4. Library on disk

```
<LibraryRoot>/
  .library/                  hidden (set FILE_ATTRIBUTE_HIDDEN), owned by Semplice
    library.db  (+ -wal, -shm while running)
    library.db.bak1 .. bak3  rotating backups
    library.json             { "app": "semplice", "library_uid": "<uuid>", "schema": 1 }
    covers/                  thumbnail + extracted-cover cache (rebuildable, LRU, cap 1 GB)
    journal/                 pending file operations (crash recovery)
  Artists/
    <artist dir_name>/
      artist_cover.jpg|png            optional
      <album dir_name>/
        album_cover.jpg|png           optional
        01 - Track title.mp3
        02 - Other title.flac
      Unsorted tracks/                real folder of the artist's "Unsorted tracks" album
  Playlists/
    <playlist dir_name>/
      playlist_cover.jpg|png          optional, user-chosen
      Track title - Artist.mp3        only tracks that have no album
```

### 4.1 Naming and path-sanitising rules (DECIDED - owner's table)

| Rule | Detail |
|---|---|
| Illegal characters | Replace `< > : " / \ | ? *` and control characters (0-31) with `_` (or remove if at the end). |
| Trailing dots and spaces | Strip. Windows silently drops them, which breaks path comparison. |
| Reserved device names | CON, PRN, AUX, NUL, COM1-9, LPT1-9 (with or without extension): append `_`. |
| Empty result | Use "Unknown artist", "Unknown album", "Untitled". Keep **English on disk** for portability (never localised). |
| Length | Each component <= 120 characters (UTF-16 units); truncate on a code-point boundary. Full path may still exceed 260: the application manifest declares `longPathAware`, the installer enables/mentions Windows long-path support; on failure return error **`E_PATH_TOO_LONG`**. |
| Collisions | Case-insensitive comparison (NTFS). On collision append ` (2)`, ` (3)` ... to the new name. Two different artists that sanitise to the same name get distinct `dir_name` values; the database keeps the true names. |
| Unicode | Store and compare **NFC**. Keep non-Latin names as they are (no transliteration). |
| Case-only rename | Renaming "abba" to "ABBA" needs two renames through a temporary name (NTFS is case-insensitive). |

Additional rules (DECIDED unless noted):
- `dir_name` values are **stored in the database and never recomputed** on read. They change only on an explicit rename operation (4.3).
- Track file names: in albums `NN - Title.ext` (NN = zero-padded `album_track.position`, at least 2 digits); in playlists `Title - Artist.ext` (first credited artist). Extension is preserved from the source file, lower-cased.
- Cover file names: accepted on **input** from a source folder: `cover.*` first (as in the UI doc), then `album_cover.*`. When the cover is stored in the library it is **always named `album_cover.<ext>`** (`artist_cover`, `playlist_cover` likewise). If no cover file exists, the cover is extracted from the first track's tags and saved as `album_cover.*` in the album folder. A manually chosen cover beats both.
- Reserved album folder name: `Unsorted tracks` (real folder of the system album). A user album with that title (any case) is rejected for the same artist.
- The folder for a hidden artist does not exist (`artist.dir_name IS NULL`); it is created when the artist becomes visible.

### 4.2 File location of a track

A track has exactly one physical file (`track.file_path`) - the **home location**: where it was first added (an album folder or a playlist folder). It may be a member of several albums and playlists (`album_track`, `playlist_track`) without copies.
- When the home membership is removed (track removed from that album/playlist, album deleted, playlist deleted) and other memberships remain, **move the file to the next location**: the membership with the earliest `added_at`, **albums preferred over playlists**. Renumber/rename the file per the new location's naming rule. Journal the move.
- If no memberships remain the track is deleted (file -> Recycle Bin).

### 4.3 Renames and moves (DECIDED: folders follow names)

Renaming an artist, album or playlist renames its folder (`dir_name` recomputed through the sanitiser, collision rules apply) and updates `track.file_path` of every affected track, all inside one journaled operation. Reordering tracks in an album renames the `NN - ` prefixes.

**Re-homing the primary credit (DECIDED):** when the primary artist of a collaborative album is deleted, the **co-author with the lowest `album_artist.position`** becomes `primary_artist_id`, the album folder moves under that artist's folder (created if needed), and remaining co-authors renumber. If that artist already has an album with the same title, append ` (2)` to the moved album's **title** (and dir_name) and show an info banner (DEFAULT).

### 4.4 Deleting files

Anything the app deletes from the library goes to the **Windows Recycle Bin** (`QFile::moveToTrash` or `IFileOperation` behind `ITrash`), never permanent delete. There is no undo/rollback tool inside the app; clicking the confirm/save button means the user accepts the change. The user's **original source files are never modified, moved or deleted** by import.

### 4.5 Operation journal (crash safety)

Every multi-step filesystem operation (import, move, rename, delete, tag write) follows: (1) write a journal entry `.library/journal/<uuid>.json` describing intent + undo info, flushed; (2) perform filesystem steps; (3) commit the DB transaction; (4) delete the journal entry. On startup, before opening the UI: for each pending entry decide **roll forward or roll back** from the actual state (files present? DB row present?) so that DB and disk agree, log the outcome, and show a single info banner if anything was repaired. Needs tests that kill the operation at each step.

## 5. Data model

The draft DDL is `docs/schema/0001_init.sql` (validated against SQLite). Claude Code owns the final schema in `src/core/db/schema/` as numbered migrations (`PRAGMA user_version`); never edit an applied migration, add a new one.

### 5.1 Entities and invariants
- **Artist**: unique by name (NFC, trimmed, case-insensitive) - **DECIDED**. `hidden` artists exist only because something references them (a credit on a playlist track or as co-author/feat). Hidden artists are not shown in the Library Artists tab but are searchable in the Add-Artist submenu.
- **Album**: lives under one **primary artist**; title unique **per primary artist**; one album can have co-authors in a user-ordered list (`album_artist`). Types: `single, album, ep, live, compilation, other`.
- **Track**: one file; credits (`track_artist`); optional track tags.
- **Playlist**: unique name; ordered tracks; tracks may or may not be in albums.
- An **album or playlist cannot exist without tracks**: when cascading removal empties one, delete it too. An artist *may* exist with no albums if the user created it explicitly.
- **Hidden-artist garbage collection:** a hidden artist with no track credits, no album credits and no co-author rows is deleted automatically.

### 5.2 "Unsorted tracks"
- It is an album of type `other`, **title not stored** (`title = ''`, `is_unsorted = 1`), displayed through translation (`tr("Unsorted tracks")`), no cover (placeholder), no tags, release date = creation date. Shown only on the artist's own page; excluded from year/decade/genre carousels, from year sorting, and from the Albums library tab.
- **Hidden artist:** virtual. No `album` row; the view is a query: tracks credited to the artist with role `main` that live in playlists. The "Feat's" carousel works as for normal artists.
- **Visible artist:** a real album row and real folder `Artists/<Artist>/Unsorted tracks/`. It is created when a playlist track is "Added to discography" for that artist (the chosen artist becomes visible if hidden; the file moves there), and by the "add to discography" button on a hidden artist's card (materialises all of the artist's tracks).
- When a visible artist loses its last real album and still has track credits (tracks in playlists), the artist becomes hidden, the real Unsorted album row/folder disappears and its tracks stay where playlists need them (file moved per 4.2).

### 5.3 Credits, "Collaborations" and "Feats"
- **Collaborations carousel** (artist page): albums where the artist is a co-author but not primary.
- **Feats carousel**: albums that contain tracks crediting the artist where the artist is not the primary artist or a co-author of that album.
- `track_artist.role`: from tags, artists listed before a `feat.`/`ft.`/`featuring` separator or separated by `;` are `main`; those after the feat separator are `feat`. In the track editor a newly added artist defaults to `feat` if the track is in an album the artist is not an author of, otherwise `main`. (DEFAULT - section 14.)
- **Splitting artist strings from tags (DEFAULT):** split on `;`, ` feat. `, ` ft. `, ` featuring `, and ` & ` **only if** the whole string does not already match an existing artist name. Never split on `,` or `/` automatically. The user can always fix credits in the Add Album screen before saving.

### 5.4 Tags (DECIDED)
- Tags are a **closed predefined vocabulary**, currently genres only; users cannot create tags. Later: mood tags (design `kind` so it can be extended).
- Source of truth: `resources/tags/genres.json`: canonical key (English slug), names in `en/ru/zh_CN/ja` (four localised vocabularies), plus `aliases` (raw tag strings -> key; include the ID3v1 numeric genre list 0-191 and common spellings such as "Hip Hop", "Hip-Hop/Rap", "R&B"), plus a relation table: `related: [keys]` and `opposite: [keys]` per genre (used by the recommender, 7.6). The DB stores only keys.
- `GenreMap` maps raw metadata genre strings to keys (case/diacritics/punctuation-insensitive; unknown strings are ignored, never create tags).
- Claude Code drafts the first vocabulary (~60-80 genres with relations) in M3 as a **reviewable file**; the owner reviews names and relations.
- Inheritance: album tags are prefilled from the tracks' metadata **and** from the artist's tags; the user can remove any. The artist receives the tags of its **first** album once (`artist.tags_seeded` flag), never automatically again - so manual edits are not overwritten. For a collaborative album, **every** credited artist (primary and co-authors) receives the tags under the same once-only rule. (The owner's addition "second and subsequent albums inherit the artist's tags" is implemented as the *prefill* above - DEFAULT, section 14.)
- Effective tags of a track for scoring = `track_tag` U `album_tag` of the albums containing it.

### 5.5 Dates
Year required, 1744..(current year + 1). Month and day optional ("-" in the UI). Invalid states (day without month, impossible day such as Feb 30, year missing) -> red highlight, Save disabled. For sorting a missing day is the 1st, a missing month is January. Date format is identical in all languages (ISO style `YYYY-MM-DD`, partial `YYYY-MM`/`YYYY`).

## 6. Operations

### 6.1 Import (DECIDED: always copy)
1. **Drop or "Add" selects files/folders** -> scan (recursive for folders, supported extensions only) on a worker; read tags (6.2); apply the EncodingFixer; no files are copied yet.
2. **Classification** per the UI doc: 1-3 tracks -> Single, 4-7 -> EP, 8+ -> Album; applies when tracks share album metadata or more than half share the same artist; otherwise open the Add Playlist window. Only these three types are auto-detected; Live/Compilation/Other are manual. If an artist page is open, the Add Album window opens with that artist preselected.
3. The user edits the form (UI doc "Album addition window") and presses Save.
4. **Commit job** (journaled, cancellable): per track compute SHA-256 while copying the file into the library; if the hash already exists in `track.content_hash` the file is **not copied again** - the existing track is added to the album/playlist membership (a track may belong to several albums). Then write tags (6.2), place cover, insert DB rows in **one transaction**, update tag seeding, add info banner if duplicates were reused.
5. Progress bar in the UI (UI doc "Import progress bar") with a **Cancel** button. Cancel rolls back everything created by this job (copied files, rows) via the journal. Originals are never touched.

Duplicate detection is by hash only (DECIDED). The hash is of the **source** bytes and is stored; it is not recomputed after tag writes (writing tags changes the library copy's bytes, but re-importing the same original must still be recognised).

### 6.2 Tags: read and write (DECIDED: the app writes tags into the library copies)
- **Read:** title, artist(s), album, album artist(s), track number, year/date, genre, embedded cover, duration, codec. Use TagLib's generic `PropertyMap` where possible; format-specific code only for covers.
- **Write:** on every Save of an album/track/artist-credit/playlist-membership edit, write the user's values into the library copy so tracks get **clean, consistent metadata** (the owner's motivation: messy tags). Fields: TITLE, ARTIST (multi-value), ALBUM, ALBUMARTIST (primary + co-authors, multi-value), TRACKNUMBER (position), DATE, GENRE (canonical **English** names so files stay portable) and the embedded front cover. For a track with multiple memberships write the **home** location's album data. Album-less (playlist) tracks: write title, artist, genre; leave album fields untouched (DEFAULT).
- MP3: write **ID3v2.4, UTF-8** and remove ID3v1 to avoid stale mojibake (DEFAULT). Write what each format supports (WAV has weak tag support: write best-effort, never fail an import because of it). Never write to the user's originals.
- Writing happens on a worker through the journal; write to a temporary copy and atomically replace (`ReplaceFileW`) so a crash cannot leave a half-written audio file.
- **File locked while playing:** Windows holds the file open. `IAudioBackend::release(path)` must let the tag writer release the file (stop/unload the track, restore position after) or queue the write until the track changes. Test this.
- If a tag write fails (read-only, locked): the DB change still stands, show a warning banner, record the file as "tags out of sync" (log; retried by Rescan).

### 6.3 EncodingFixer (DECIDED: fix on import)
Old ID3v1 / ID3v2.2-2.3 text stored as Latin-1 but really CP1251 / GBK (CP936) / Big5 (CP950) / Shift-JIS (CP932) / EUC-KR (CP949) shows as mojibake. Heuristic: only touch text frames declared ISO-8859-1 (and ID3v1); re-encode to bytes, try candidate code pages, score by script plausibility (all-Cyrillic words, all-CJK) and discard if still implausible. Windows implementation via `MultiByteToWideChar` behind `ICodepageConverter` (Qt's own codecs are limited without ICU); provide a deterministic fake for tests. The fixed values prefill the Add Album form so the user can correct them; the saved values are then written back to the files (6.2). UTF-8/UTF-16 frames are trusted as-is.

### 6.4 Deletions and cascades (UI doc + decisions)
- **Remove track from album** - only enabled if the track exists elsewhere. Home-move per 4.2.
- **Delete track** - removes it from everything, file -> Recycle Bin; albums/playlists emptied by this are deleted; artist visibility re-evaluated (5.2, GC in 5.1). Confirmation dialog first.
- **Delete album** - deletes the album and its tracks that exist nowhere else; tracks still used elsewhere stay (home-move). If this was the artist's last album and some tracks remain in playlists -> artist becomes hidden (5.2).
- **Delete playlist** - deletes the playlist and tracks that exist nowhere else.
- **Delete artist** - per UI doc: if the artist has only solo albums and appears on no playlist track and no other artist's album: delete everything. Otherwise delete the albums that have no dependencies and keep the artist as **hidden**. Primary of a collaborative album -> re-home (4.3). The warning text must describe what will actually happen (compute the outcome before showing the dialog).
- The Delete key in lists always asks for confirmation.

### 6.5 Rescan (Settings button, F5) (DECIDED)
Housekeeping that also finds files the user placed in the library folder manually. Incremental (size + mtime; hash only if changed).
1. Files on disk but not in the DB (non-indexed) -> **moved** to `Playlists/Rescan <YYYY-MM-DD HH-mm>/` and imported into a new playlist of that name (tags read, EncodingFixer applied). Tracks with an existing hash are not duplicated.
2. Rows whose file no longer exists -> removed with the normal cascades; one info banner summarising.
3. Maintenance: `PRAGMA optimize`; `VACUUM` if free pages > 20%; refresh backups; purge thumbnail cache entries for missing covers.
Runs on a worker with a progress banner and cancel. F5 runs the same incremental scan (the UI doc's "quick scan").

### 6.6 Library directory (DECIDED)
First launch: "Select folder" card (empty folder required; if not empty show an error banner and ask again). Settings -> Change opens **another existing library** (folder containing `.library/library.json` with the right app id and a schema version <= known) or an empty folder to start a new one; it never moves the current library. The user can move the whole library folder manually in Explorer; on the next launch, if the stored path no longer exists, show the "Select folder" card in a "locate your library" wording. Schema newer than the app understands -> refuse with a clear message.

### 6.7 Backups and maintenance
`library.db.bak1..bak3`: at startup, if the newest backup is older than 24 h, rotate (`bak2->bak3`, `bak1->bak2`) and create a fresh `bak1` with `VACUUM INTO`. Integrity check (`PRAGMA quick_check`) at startup; on failure offer restore from newest good backup.

## 7. Playback

### 7.1 Audio backend (DECIDED)
`IAudioBackend` (load/play/pause/seek/volume/position/duration, signals for state, end-of-media, error, `release(path)` for tag writes, `preload(path)` for gapless) with `QtAudioBackend` for v1. Everything beyond that (BASS, equalizer) is future work: **no `QMediaPlayer` types outside `QtAudioBackend`**. Gapless: preload the next track if the backend allows, otherwise a short gap is accepted. An unplayable/missing file: skip it, remove it from the queue, show **one** banner per batch (DECIDED).

### 7.2 Queue model
A playback session consists of ordered **queue groups**. A group = `{ origin: {kind: album|playlist|discography|carousel|recommendations, id, title}, tracks[] }`.
- Play button behaviour and end-of-queue rules exactly as the UI doc "PLAYBACK RULES": playlist window -> one group, stops at the end; album/artist/carousel window -> after the end, **recommendations** start. Search results and playlist cards never start the endless stream; single track from search -> queue of that track only.
- Starting an item from within a carousel (not "play all"): **one independent group per carousel item**, started at the clicked item; when one ends the next begins; the player remembers earlier groups so the user can go back; after the last group -> recommendations.
- **Repeat all** repeats the *current group*; **repeat one** repeats the track; **shuffle** shuffles the current group.
- Pressing **Next on the last track** continues into the next group, or into recommendations if applicable (this supersedes the older UI text "the infinite stream will start the next album"; recommendations are track-level).
- **Previous:** if played <= 5 s go to the previous track; otherwise restart the current track. Disabled at the very start of playback.
- **Shuffle (DECIDED, shuffle-bag):** turning shuffle on keeps the current track first and permutes the rest; already played tracks are not repeated in the pass; turning it off restores the original order from the current track. Previous walks the actual played history.
- **Queue panel:** the right-column side panel shows previous / current / next (current centred, click or drag to change track). The hotbar queue popup shows the whole current group (top to bottom). The queue and search popups cannot be open together; any outside interaction closes them; the search text is retained until exit.
- If the playing or next track is deleted: skip and remove.
- **Queue editing ("play next", reorder) is out of scope.**
- Player header (above the cover) per the UI doc's final section: recommendations -> "Recommendations"; album -> album title; playlist -> playlist title; discography -> artist name (+ "discography" indication); whole carousel -> carousel name. Clicking it opens the corresponding view.
- Cover shown: track's embedded cover first; else the playing album's cover; else the placeholder (music-note) - exactly the cases in the UI doc.

### 7.3 Persisted state (DECIDED)
Persist current track, position in ms, the full group list with order (and shuffle order), repeat/shuffle modes, volume (default 50 %; mute state separately). Restore on launch **paused**, skipping tracks that no longer exist. Stored in `kv('session')` and QSettings.

### 7.4 Recently Played (DECIDED)
An album or playlist is registered after its **first track has played min(30 s, 50 % of the track's duration)**. Replaying moves it to the front, no duplicates, max 50. Discography playback registers each album separately. Recommended (endless-stream) tracks never touch this list but **do** update `track.play_count`/`last_played_at`.

### 7.5 Play statistics
`play_count` and `last_played_at` are updated on the same 30 s/50 % rule, for every kind of playback.

### 7.6 Recommender (UI doc + decisions)
Implement per the UI doc: seed = albums/artists of the source that just finished; candidates = every playable track not in seed albums (including playlist-only tracks); score = weighted sum; pick randomly weighted by score from the top ~100; variety constraints (<= 2 tracks of one album per 10 positions, same album never twice in a row, no repeats until the pool is exhausted); look-ahead of 2-3 tracks so "Up next" is accurate; relaxing rules (seed albums, then already played tracks; stop only if < 2 playable tracks).

Weights live in one constants file `RecommenderConfig` (tunable, DEFAULT values):

| Signal | Weight |
|---|---|
| shared tags with seed (Jaccard-style overlap) | +0.50 |
| same artist credit (track level, includes feats) | +0.30 |
| **related genre** (any tag of the candidate listed `related` for a seed tag) | +0.20 |
| year / decade proximity | +0.10 |
| **opposite genre** (candidate tag listed `opposite` for a seed tag) | -0.30 |
| recently played penalty (decays over ~7 days using `last_played_at`) | -0.40 |

Keep the scorer pure (inputs -> score) so it is unit-testable; randomness injected via a seedable RNG.

### 7.7 Windows integration (DECIDED)
- **Single instance:** a second launch activates the running window.
- **System Media Transport Controls (SMTC)** via WinRT interop (`ISystemMediaTransportControlsInterop::GetForWindow`): publishes title, artist, album title and cover thumbnail; handles Play, Pause, Next, Previous. This is the widget the owner wants in the **volume/sound flyout**, and it also gives **global media keys**. (The owner's shortcut table says media keys work "when the window is focused"; SMTC makes them global, which is the normal behaviour for players - DEFAULT, section 14.)
- **Taskbar thumbnail toolbar:** Previous / Play-Pause / Next buttons in the taskbar-icon hover preview via `ITaskbarList3::ThumbBarAddButtons` (Qt 6 has no WinExtras: implement in `platform/` with a native event filter for `WM_COMMAND`/`THBN_CLICKED` and the `TaskbarButtonCreated` message; update icons/tooltips with play state; disabled states mirror the in-app buttons).
- File association / "Open with" is **not** in v1.

## 8. UI specifics that complete the UI document

### 8.1 Window and layout (DECIDED - owner's table)

| Property | Value |
|---|---|
| Default / minimum size | 1280 x 760 / 1000 x 620 logical px. Remember size, position and maximised state; restore onto a visible monitor if the saved position is off-screen. |
| Zone widths | Library ~24 % (min 260, max 340 px). Right column ~26 % (min 300, max 380 px). Middle takes the rest (min 420 px). Fixed proportions (no splitters) in v1.0. |
| Zone heights | Hotbar 56 px fixed. Player panel ~63 % of the right column, side panel the remainder (min 200 px). |
| Panel style | Rounded panels (12 px radius) on a neutral window background with 8-12 px gaps. |
| DPI | Qt 6 high-DPI with fractional scaling `PassThrough`. Device-independent units, SVG icons; test at 100/125/150/200 %. |
| Fonts | Inter for English and Russian; Segoe UI for Japanese and Chinese. |
| Text expansion | Russian text is up to ~35 % longer. Every label truncates with an ellipsis and shows a tooltip; **no fixed-width buttons that contain text**. |

**Font fallback (important for a music app):** track and artist names can be in any script regardless of UI language. Set the global fallback chain `Inter, Segoe UI, Yu Gothic UI, Microsoft YaHei UI, Malgun Gothic` (Inter has no CJK). Bundle Inter (OFL) as a resource; Segoe UI is a system font - never bundle it.

### 8.2 Using the mock-ups
The screenshots come from a first-time designer. Use them for structure, behaviour and relative proportions, **not** as pixel specs; their spacing is inconsistent. In `Theme.qml` define a token set (spacing on a 4/8 px grid, radii, type scale, icon sizes, animation durations) and use only tokens in views, so spacing is consistent. Present the token table in `docs/DESIGN_TOKENS.md` for the owner to review. Where a screenshot disagrees with the layout table above, the table wins. The owner may later export Figma frames/SVG icons into `docs/design/`; if present, prefer them for icons and the logo.

Palette (single theme "True White" in v1; every colour comes from the theme singleton so a second theme can be added later):

| Token | Value | Use |
|---|---|---|
| background | `#9C9C9C` | window background behind panels |
| surface | `#FFFFFF` | blocks/panels |
| text | `#1E1E1E` | text, icons |
| textSecondary | `#5A5A5A` | secondary text |
| disabled | `#B3B3B3` | disabled text, icons |
| danger | `#C00F0C` | destructive actions |

Button states, volume-icon states (0 % / 1-50 % / 51-100 %), repeat states (off / all / one), track-name hover underline, progress-bar hover time tooltip, avatar hover/play-overlay: see UI doc page "BUTTON STATES". The logo is in the UI doc (a faceted four-point star/gem; light, dark and gradient variants): recreate as SVG in M0 from the page image, or use the owner's file if provided.

### 8.3 Validation rules (consolidated)

| Entity | Rule | UI behaviour |
|---|---|---|
| Artist | name non-empty after trim; unique (NFC, case-insensitive) | Save disabled when empty; duplicate -> info banner "already exists" |
| Album | title non-empty (DEFAULT); unique per primary artist; >= 1 artist; >= 1 track; valid date (5.5) | Save disabled; invalid date red; duplicate banner |
| Playlist | name non-empty; unique; >= 1 track | same |
| Track edit | title non-empty; >= 1 artist | Save disabled |
| Settings name | <= 50 chars; empty -> localised "Listener" | applied on Save |
| Tags | only keys from the vocabulary | picker offers existing tags only |

All dialogs: Save and Cancel; Cancel discards edits; Esc closes popups/dialogs.

### 8.4 Settings (DECIDED)
Language (English/Russian/Chinese/Japanese; **restart required**, marked in the UI), Name (applies on Save), Theme (single option "True White"; **restart required**; keep the control so more can be added), Current directory + Change, Rescan, Logs folder (opens `%LOCALAPPDATA%\Semplice\logs`), Official site (opens URL in the browser; URL TBD - section 14), License (opens the license dialog). Save/Cancel at the bottom. App-level settings (language, theme, name, volume, window geometry, last library path, per-tab view modes, license acceptance) are stored with `QSettings` (INI in `%APPDATA%\Semplice\settings.ini`). Library-bound state (recent items, session, play stats) lives in the library DB so a library moved to another PC keeps it.

### 8.5 Library panel details
Tabs Playlists / Artists / Albums with the carousel-style tab switcher; remember the last active tab and **view mode per tab** across launches; sort state per tab is *not* persisted (default: Recent, newest first). Views: list, 2-per-row large, 3-per-row extra-large (no names, playback only). Artists: Alphabet, Recent. Albums: Alphabet, Recent, Year, plus type filter (all shown by default). Playlists: Alphabet, Recent. Alphabet arrows: down = A->Z, up = Z->A. The `+` button opens a submenu: Add Artist / Add Album / Add Playlist. Hidden artists never appear in the Artists tab.

### 8.6 Home view
- Greeting header: "Welcome back, {name}!" when the library has content; "Welcome, {name}!" otherwise; default name "Listener". Occasion greetings override the default (DEFAULT texts, English, drafted by Claude Code in M7 and translated later): New Year (Dec 31 - Jan 2), first day of each meteorological season (Mar 1, Jun 1, Sep 1, Dec 1; Northern hemisphere), otherwise time-of-day: morning 05-11, afternoon 12-17, evening 18-04.
- "Recently Played" carousel is always first (<= 50 items, circular, first item active).
- **Categories (DECIDED):** types = artist discography, year, decade, genre, "Your Playlists", "Your Albums". 5 carousels at launch, chosen randomly from eligible types, **no type repeats until every eligible type has been used once**, then the cycle restarts (a type picks an unused instance, e.g. another artist). More categories are generated on the fly while scrolling as long as there is content. Year/decade/genre carousels need >= 5 albums and hold <= 50; artist carousel shows albums in random order; excluded: Unsorted tracks. Clicking a category title opens the tile view of all its items.
- Carousel: `PathView`-style circular layout, <= 50 items, centre item enlarged and in front, drag horizontally to rotate, click the centre item to play, title (and artist for albums) under the centre item. Only instantiate visible delegates.
- Banners: max 3 visible, newest below, oldest auto-dismiss; they never auto-dismiss otherwise (UI doc).
- The central area is a drop zone: while dragging files the header becomes "Import" with blur + icon.

### 8.7 Keyboard (DECIDED - owner's table)

| Shortcut | Action |
|---|---|
| Space | Play / pause (when no text field has focus) |
| Ctrl+Right / Ctrl+Left | Next / previous track |
| Ctrl+Up / Ctrl+Down | Volume +/- 5 % |
| M | Mute |
| Ctrl+F | Focus search |
| Alt+Left / Alt+Right | Back / Forward in the middle window |
| Alt+Home or Ctrl+H | Home |
| Ctrl+, | Settings |
| Ctrl+N | Add album (opens the Add Album window in the middle pane) |
| Esc | Close popup / dialog; clear search |
| Delete | Delete selected item in lists (always asks) |
| F5 | Rescan library (incremental) |
| Media keys | Play/Pause, Next, Previous (global through SMTC) |

Right-click context menus: not in v1 (the three-dot menu covers it).

### 8.8 License and first launch
The installer shows the license text and requires agreement (Inno Setup `LicenseFile`). In the app, Settings -> License opens the same text (resource `resources/license/LICENSE.txt`, **placeholder until the owner supplies it**). The UI doc's license dialog has **Exit app / Accept** buttons - implement the dialog component with both modes (accept-required and read-only/close) and show the accept-required mode on first launch (DEFAULT).

## 9. Performance budgets (DECIDED - owner's table)

| Metric | Target | Hard limit tested |
|---|---|---|
| Library size | 50,000 tracks / 5,000 albums / 2,000 artists / 500 playlists | 200,000 tracks / 20,000 albums / 8,000 artists |
| Cold start to visible window | < 1.5 s | < 3 s |
| Home usable (first 4 categories) at target size | < 2.5 s after start | < 5 s |
| Search latency (typing to results) | p95 < 100 ms | p95 < 250 ms at hard limit |
| Scrolling library lists / carousels | 60 fps on a mid-range integrated-GPU laptop | >= 30 fps |
| Import throughput (excluding file copy) | >= 20 tracks/s (tag read + DB) | - |
| Memory, idle at target size | < 250 MB | < 500 MB |
| Database size | ~1 KB/track + indexes ~ 60 MB at target | < 300 MB at hard limit |
| Thumbnail cache | 128/256/512 px JPEG, ~40 KB per album ~ 200 MB at target | cap 1 GB, LRU cleanup |

Consequences: virtualised lists everywhere (`ListView`/`GridView` with `reuseItems`, C++ models with lazy row fetch, no full-library QML models), async image provider with the thumbnail cache and size-appropriate source (128 px for list rows, 256 px for tiles, 512 px for carousel/cover), window shown before heavy loading, startup work on workers. Provide a **synthetic library generator** (`tests/tools/`) that can create 50k/200k-track libraries (DB-only mode and real tiny audio files mode) and a benchmark executable that checks the budgets.

## 10. Testing (DECIDED: yes, within reason)
- Unit tests (QtTest/CTest): `Normalizer`, `PathSanitizer` (every row of 4.1), `GenreMap`, `EncodingFixer` (with fake converter), repositories and cascades (5.x, 6.4), re-home, home-move rule 4.2, validation, recommender scorer and constraints, queue/shuffle/repeat state machine, recently-played threshold, `OperationJournal` crash-recovery (kill at every step), date validation.
- Integration tests: import -> edit -> delete flow on a temp library built by the generator; rescan; duplicate import; cancel rollback; tag write round-trip for each format (generate tiny valid files).
- Benchmarks for section 9. No automated UI tests in v1; instead a manual QA checklist (`docs/QA_CHECKLIST.md`, started in M6, extended per milestone). The owner plans ~3 months of beta testing by a few people: add a "Open logs folder" path and make logs useful (see 11).

## 11. Logging and crash information
Rotating log files in `%LOCALAPPDATA%\Semplice\logs` (size-capped), levels, no personal data beyond file paths. Install a crash handler that writes a text crash log (and a minidump if cheap) there. Every `Error` is logged with code and context. The Settings "Logs folder" button opens this folder.

## 12. Localisation
English source strings in code (`tr()`, `qsTr()`); `translations/app_{en,ru,zh_CN,ja}.ts`. Claude Code produces **machine-translated draft** ru/zh_CN/ja files marked clearly as drafts for the owner to review (DECIDED). Use `%n` plurals ("3 songs", "8 min 20 sec"): Russian has 3 plural forms, Chinese/Japanese 1. No concatenated sentences. No hard-coded user-visible strings, including error banners. On-disk names stay English (4.1). Dates use one format everywhere (5.5). Language change applies after restart.

## 13. Corrections to the UI document
Implement the corrected form; do not copy these defects:
- UI text typos: "Atrists" -> **Artists** (Library tab), "Alphabeth" -> **Alphabet** (sort menu), "RECOMENTATION" (heading), "colliection" (Select-folder card; should read "music collection").
- The Album card paragraph is cut off at `then "endless stream"`. **Meaning (owner-confirmed):** with repeat-all off, the endless stream (recommendations) starts after the album ends.
- Cover file name: UI doc says both `cover.jpg` and `album_cover.jpg` - see 4.1.
- "All artist names and album titles must be unique" - albums are unique **per primary artist** (5.1).
- Queue section says the stream "will start the next album"; the later Playback/Recommendation sections define track-level recommendations - those win (7.2).
- "A track-level setting keeps it hidden" - **removed**. An artist created from the track editor is hidden and becomes visible when added to a discography or listed as an album author; there is no setting for it.
- The "Add to Album" and "Add to discography" dialogs are not drawn: "Add to Album" reuses the "Add to Playlist" submenu pattern (search + list of existing albums + "Add as new album" which opens Add Album with the track included). "Add to discography" (playlist edit window) = choose an artist (submenu like Add Artist) and materialise it per 5.2.
- Source tree root `MusicPlayer/` -> `semplice/`; "Working title" -> final name Semplice.

## 14. Open items for the owner (not blocking; DEFAULT applies until answered)
1. **Artist-string splitting** from tags (5.3) - the rule is about *artist names* in tags, not genre tags. Confirm the default.
2. **Tag inheritance** (5.4): confirm that "second and later albums inherit the artist's tags" means *prefilled in the Add Album form, removable*.
3. **Track credit role** (`main` vs `feat`) rules (5.3) - confirm, since the UI has no explicit control.
4. **Media keys** global via SMTC (7.7) vs only when the window is focused.
5. **License:** text, and whether to also show Accept/Exit on first launch (8.8). **Official site URL** and donation link text.
6. **Genre vocabulary and relations** (5.4): review the draft that Claude Code produces in M3.
7. **Tag writing details** (6.2): ID3v2.4/UTF-8 + ID3v1 removal; album-less tracks keep their album fields.
8. **Album title required** (8.3) and the re-home title collision rule (4.3).
9. Figma exports / SVG icons / final logo file, when available.
