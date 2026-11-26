# M1 - Domain and database: design note

Goal: the library model with correct invariants and no UI. Everything here is `semplice_core` (Qt Core + Sql), testable on any OS. Contradicts nothing in the spec; deviations are listed in section 7.

## 1. Pieces
| Area | Files (`src/core/...`) | Notes |
|---|---|---|
| Domain | `domain/Ids.h`, `Types.h`, `ReleaseDate.{h,cpp}` | strong ids (`ArtistId`...), `AlbumType`, `CreditRole`, plain structs, date validation (clock injected) |
| Normalizer | `util/Normalizer.{h,cpp}` | `identityKey` (trim, NFC, collapse spaces, casefold) for uniqueness; `searchKey` = identityKey + strip combining marks U+0300-036F |
| DB | `db/Database.{h,cpp}`, `Migrator.{h,cpp}`, `schema/0001_init.sql` | one connection per `Database`, WAL, FK on, `Transaction` RAII; migrations embedded as resources, `PRAGMA user_version` |
| Repositories | `db/ArtistRepo`, `AlbumRepo`, `TrackRepo`, `PlaylistRepo`, `TagRepo`, `RecentRepo`, `KvRepo` | return domain structs, never `QSqlQuery`; no business rules (those are in the service) |
| Rules | `services/LibraryService.{h,cpp}`, `DeletePlan.h`, `TagVocabulary.h`, `IPathNamer.h` (+`SimplePathNamer`), `storage/IFileStorage.h` (+`RecordingFileStorage`) | the SPEC 5 / 6.4 invariants as DB-only operations. File effects are *recorded* as operations and handed to `IFileStorage` after commit (M2/M4 replace this with the journaled real storage) |
| Search | `services/SearchIndex.{h,cpp}` | in-memory, 4 kinds, rank prefix > word start > substring, incremental `refresh` driven by the service's change set |
| Tools | `tests/tools/`: `SyntheticLibrary` (DB-only generator, presets 50k/200k), `IntegrityChecker`, executables `semplice_gen`, `semplice_bench` | generator is built for tests and for you to run |

## 2. Final schema (migration 0001) = draft + approved changes
D1 `track.home_kind/home_id`, D2 non-unique position indexes, D8 extra indexes, D10 unique keys: `artist.name_key`, `playlist.name_key`, `album.title_key` (unique per primary artist, not for the Unsorted album). **Deviation from D10:** the key is *trim + NFC + collapsed spaces + casefold*, **without** stripping diacritics. Reason: "Beyonce" and "Beyoncé", or Russian "й" and "и", are different names; stripping is used only for search. Recorded in DECISIONS.

## 3. Rules and where they live (all in `LibraryService`, one DB transaction each)
- Artist name unique (`E_ALREADY_EXISTS`); album title unique per primary artist; "Unsorted tracks" (any case) rejected as an album title; playlist name unique.
- Album: title non-empty, >= 1 primary artist, >= 1 track, valid date (1744..current year+1, no day without month, real calendar day); tags must exist in `TagVocabulary`.
- Tag seeding: every credited artist whose `tags_seeded = 0` receives the album's tags once (flag set only when tags were actually copied).
- Memberships: a track can be in many albums/playlists; positions are always rewritten `1..n` after a change; the track's home (`home_kind/id`, `file_path`) follows SPEC 4.2: when the home membership goes, move to the earliest remaining membership, albums before playlists; no memberships left -> track deleted.
- Cascades (6.4): remove track from container, delete track, delete album, delete playlist, delete artist. Empty album/playlist is deleted. Re-home primary credit (4.3) incl. ` (2)` title suffix when the new owner already has that title.
- Artist state (5.1/5.2): hidden artist with no track credits and no co-author rows is deleted; a visible artist that *loses its last real album* and still has track credits becomes hidden (Unsorted album dissolved, `dir_name` NULL); `addToDiscography` / `materialiseHiddenArtist` create the real Unsorted album (folder `Unsorted tracks`) and make the artist visible; the hidden artist's Unsorted list is a query (`TrackRepo::unsortedOfHiddenArtist`).
- Deletion previews: `planDeleteX()` runs the real operation inside a transaction and **rolls it back**, returning the report. The dialog text therefore always matches what will happen (SPEC 6.4).

## 4. Threading (M1)
Synchronous, one connection per `Database` object. The serialised writer queue and reader pool from TECH_DESIGN section 4 are added in M4 when the services move to workers; repositories do not change for that (they only need a `Database&`).

## 5. Test list (QtTest)
`tst_normalizer`, `tst_releasedate`, `tst_migrator` (fresh DB, idempotent, refuses newer `user_version`), `tst_repos` (CRUD, unique violations map to errors, FK on), and `tst_library_rules` covering: artist uniqueness (case, Cyrillic, NFC vs NFD), album uniqueness per primary artist, reserved name, album/playlist need tracks, date validation table, tag key validation, tag seeding once (incl. co-authors), duplicate hash reuse, multi-membership home-move (album preferred, earliest added), remove-from-album only allowed when elsewhere, delete track/album/playlist cascades incl. emptied containers, hidden-artist GC, hidden artist virtual Unsorted vs real Unsorted (addToDiscography, materialise, dissolve on last album loss), delete-artist both outcomes + re-home with ` (2)`, plan == actual for every delete, positions contiguous, `IntegrityChecker` clean after every scenario. `tst_search_index` (ranking, CJK, diacritics, incremental refresh). `tst_synthetic_library` (small preset valid; checker clean).

## 6. Benchmarks reported at the end of M1
Generator time and DB size for 50k and 200k tracks; index build time and memory; search p95/max for 100 typical queries on both sizes (budget: p95 < 100 ms at target size, < 250 ms at 200k); DB open + index build time (startup).

## 7. Deviations / choices to record
1. Name key without diacritic stripping (above).
2. Co-author rows survive when an artist becomes hidden (SPEC 5.1 allows hidden artists with co-author rows); an artist deleted while primary of a collaborative album stays as the *last co-author* of the re-homed album and is hidden (matches UI p.33/38).
3. File effects: recorded and applied after commit in M1 (spec order files-then-commit arrives with the journal in M2/M4).
4. `LibraryService` update/rename operations (renaming folders) are M4; M1 has create/delete/membership operations only.
5. No `WriterQueue` yet (section 4).
