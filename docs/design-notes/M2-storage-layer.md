# M2 - Storage layer: design note

Goal: safe file operations. After M2 nothing in the app touches library files except `FileStorageService`, every multi-step change is journaled, a crash at any point leaves disk and database in agreement after the next start, and the library folder can be created, opened, backed up and checked. `core` only (Qt Core + Sql); Windows-only pieces sit behind interfaces.

## 1. Pieces
| Class (`core/storage/`) | Job |
|---|---|
| `PathSanitizer` | every row of SPEC 4.1: illegal characters, trailing dots/spaces, reserved device names, empty result -> English fallback, 120 UTF-16 units cut on a code-point boundary, NFC, case-insensitive collision suffix ` (2)`, `E_PATH_TOO_LONG` check |
| `SanitizingPathNamer` (`core/services/`) | replaces M1's `SimplePathNamer`; `IPathNamer::dirName(name, NameKind)` now knows the kind so the fallback is "Unknown artist" / "Unknown album" / "Untitled" |
| `ITrash`, `QtTrash` | Recycle Bin through `QFile::moveToTrash` (IFileOperation with undo on Windows). Test fake moves into a sandbox folder instead |
| `OwnFiles` | the **single** place allowed to remove a file or folder directly (see 5) |
| `OperationJournal` | journal entries `.library/journal/<uuid>.json`, written atomically with fsync (`QSaveFile`) |
| `DbCommitLog` | table `op_log` (migration `0002_op_log.sql`): the DB transaction of an operation inserts its uuid, so recovery knows exactly whether that transaction committed |
| `FileStorageService` | implements `IFileStorage`; executes `FileOp`s (EnsureDir, Copy, MoveFile, MoveDir, Trash) journaled; two-phase API for the DB-in-the-middle flow; startup `recover()`; fault-injection hook |
| `LibraryFolder`, `openLibrary()` | create (empty folder required) / inspect / open, `library.json`, hidden `.library` (via `IFileAttributes`; `WindowsFileAttributes` in `platform/`), schema check, `quick_check`, backup rotation, restore from the newest good backup, then journal recovery |

## 2. The operation protocol (SPEC 4.5, with the DB commit in the middle)
```
begin(ops)        write journal entry (intent + undo info), fsync               [checkpoint]
execute()         phase A: EnsureDir / Copy / Move ... (each step a checkpoint)  [cancel + progress]
-- caller --      one DB transaction: its own changes + DbCommitLog::mark(id) -> commit   [checkpoints]
finish()          phase B: Trash ops (after the commit!), delete journal entry
rollback()        (DB failed or user cancelled) undo phase A in reverse, delete entry
```
* **Trash only after the commit.** Rolling back a Recycle-Bin move is not reliable, so deletions are phase B; before the commit nothing is ever deleted.
* The entry stores per op: kind, paths, `createdDirs` (folders that did not exist before), `dstPreexisting`, and for case-only renames the temporary `via` name (NTFS cannot rename `abba` -> `ABBA` in one step).
* **Recovery** (`recover(oracle)`, run before the UI): for every pending entry ask the oracle "did this operation's DB transaction commit?" (`op_log`; entries flagged `autoCommit`, used by the one-shot `apply()`, count as committed). Committed -> roll **forward** (finish missing steps, then trash). Not committed -> roll **back** (undo executed steps in reverse). Every step decides from the real state of the disk (exact-case lookups for case-only renames), so recovery is idempotent: running it twice, or after a crash during recovery, is safe. Result: `RecoveryReport` -> one info banner if anything was repaired.
* **Copy** (import): SHA-256 while copying in chunks, destination opened with "new only" (never overwrites), size verified, source opened read-only and never touched, `std::stop_token` for cancel, progress callback; failure/cancel undoes everything the operation did.
* `apply(ops)` (the M1 `IFileStorage` contract, effects after a DB commit) stays as a journaled one-shot with `autoCommit`; M4 moves `LibraryService` to the two-phase API.

## 3. Tests (QtTest)
* `tst_path_sanitizer`: one table row per SPEC 4.1 rule and edge (surrogate pairs at the 120 limit, extension kept, NFC vs NFD collisions, `CON.txt`, `COM10` allowed, trailing illegal characters removed, only-illegal names -> fallback, 32 k path limit).
* `tst_file_storage`: copy hash/size/progress/cancel, source untouched, existing destination never overwritten, moves (file/dir, missing parents, **case-only rename**), trash of missing path is a no-op, path escaping (`..`, absolute) rejected.
* `tst_journal_recovery` (fault injection): scenarios import, rehome, delete (trash), case-only rename, one-shot apply. For **every** checkpoint of a scenario: crash there, restart (new service object), `recover()` (twice), then assert (a) committed <=> final tree, uncommitted <=> original tree byte for byte, (b) journal folder empty, (c) conservation: every original file's content is still in the tree or in the (fake) trash, never lost.
* `tst_library_folder`: create layout and `library.json`, hidden attribute requested, non-empty folder refused, foreign folder refused, newer schema refused (json and database), open runs migrations and recovery, backups (first, +24 h rotate, max three, valid databases), corrupt database detected, restore from backup.
* `tst_storage_integration`: M1 `LibraryService` operations (home move, delete album, rehome, add to discography, delete artist) executed with the real `FileStorageService` on a materialised temp library: afterwards every `track.file_path` exists on disk and nothing else audible is left behind (trashed files are in the fake trash).
* `tst_windows_attributes` (Windows only): `.library` really becomes hidden.
* CTest `no_direct_deletes`: a script that fails if any source file outside `OwnFiles.cpp` calls a direct delete API (`QFile::remove`, `removeRecursively`, `DeleteFile`, `std::filesystem::remove` ...). Enforces "no code path deletes permanently".
* Manual (QA checklist): real Recycle Bin, hidden folder in Explorer, long paths, case-only rename on a real NTFS library.

## 4. Changes to existing code
`ErrorCode` gets `FolderNotEmpty`, `NotALibrary`, `DatabaseCorrupt`, `CrashInjected` (tests only). `FileOp::Kind::Copy` is added. `IPathNamer` gets `NameKind`. `LibraryService` uses `PathSanitizer::withCollisionSuffix`. `SimplePathNamer` is removed. `Logger` rotation goes through `OwnFiles`.

## 5. Decisions to record (also in DECISIONS.md)
1. **What "never permanent" means in code.** User data (audio, covers) only leaves the library through `ITrash`. Semplice's *own* housekeeping files may be removed directly, but only through `OwnFiles`: journal entries, old backups, logs, and files an *uncommitted* operation itself created (copied tracks of a cancelled import; guarded by `dstPreexisting`, so a file that was there before is never removed). The `no_direct_deletes` test makes `OwnFiles.cpp` the only allowed place.
2. **Deletions are phase B** (after the commit), so a crash can never lose data that a rollback would need.
3. **Commit marker in the database** (`op_log`) instead of guessing from file/row state: exact, and cheap.
4. **Case-only renames** use a recorded temporary name so recovery can continue from any point.
5. **Limitations:** a destination that already existed and was vacated by an earlier op *in the same entry* is not removed on rollback (a stray file is kept rather than risking a loss); no fsync of directories (Windows has no equivalent API call that Qt exposes).
6. **Open question for you (not blocking):** the M2 plan lists "ITrash Windows impl". `QFile::moveToTrash` is the Windows Recycle Bin on Windows (it calls `IFileOperation`), so no separate `platform/` class is needed; say if you want a hand-written `IFileOperation` wrapper anyway.
