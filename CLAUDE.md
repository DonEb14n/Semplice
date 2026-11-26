# CLAUDE.md - Semplice

Semplice is a fully offline Windows music player with a managed library (C++20, Qt 6.8 / QML, SQLite, TagLib). You are implementing it from a written specification. The owner is a designer-developer who works in **Qt Creator** and uses VS Code only for Git.

## Read this first (in order)
1. `docs/SPEC.md` - authoritative technical spec (decisions, schema, file rules, playback, budgets). Section 0 explains precedence and the DECIDED/DEFAULT markers.
2. `docs/UI-reference.md` - the owner's UI document, extracted page by page with screenshots in `docs/design/pages/` (original: `docs/source/UI.pdf`). When you work on a screen, open **that page's screenshot** as well as its text.
3. `docs/schema/0001_init.sql` - draft DDL (you own the final schema, see SPEC 5).
4. `docs/IMPLEMENTATION_PLAN.md` - milestones. Work on **one milestone at a time**.

If sources conflict, SPEC.md wins, then the UI document's "ACTUAL INFORMATION" sections, then the rest of the UI document, then screenshots. If nothing covers a case: choose the safest option, record it in `docs/DECISIONS.md` (date, question, choice, alternatives) and carry on. Ask the owner only for things that are expensive to undo (schema shape, on-disk layout, public interfaces) or that SPEC section 14 lists as open.

## How to work
- **Before writing feature code in a milestone, write a short design note** (`docs/design-notes/Mx-*.md`: classes, interfaces, data flow, test list) and stop for the owner's review only in milestone M0. In later milestones write the note, then proceed unless it contradicts the spec.
- Work in small, reviewable commits (one logical change each, imperative subject line, e.g. `Add PathSanitizer with reserved-name handling`). Do not push, rebase or rewrite history; the owner pushes from VS Code.
- Every milestone ends with: tests passing, `docs/QA_CHECKLIST.md` updated for anything user-visible, `docs/DECISIONS.md` updated, and a short report (what is done, what is not, anything surprising, anything that needs the owner).
- Do not add third-party dependencies without asking. Do not add features outside SPEC section 1 scope. Do not "improve" the design: implement it.
- Do not claim something works unless you ran it. State clearly what you could not verify (e.g. anything Windows-specific when you are not on Windows).

## Environment check (do this at the start of every session)
Run `uname`/`ver`. On **Windows with MinGW (or MSVC) + Qt 6.8+**: build and run everything. On **any other OS**: only `semplice_core` and its tests can be built and run; Windows-only code lives under `src/platform/` behind interfaces, compiled with `#ifdef Q_OS_WIN`, and must be reviewed by reading, not claimed as tested. Never fake a Windows API result to make a test pass.

## Build and test
**Default toolchain: MinGW (GCC) with Qt 6.11.2 `mingw_64`** (the owner's Qt Creator kit, decided 2026-10-19). MSVC 2022 stays available as a second compiler check (`windows-msvc-*` presets); keep both warning-free (`semplice_core` is built with warnings as errors, and GCC flags things MSVC does not).
```
cmake --preset windows-mingw-debug
cmake --build --preset windows-mingw-debug
ctest --preset windows-mingw-debug --output-on-failure
```
(Verified from a shell with `D:\Programs\Qt\Tools\mingw1310_64\bin`, `D:\Programs\Qt\Tools\Ninja` and `D:\Programs\Qt\6.11.2\mingw_64\bin` on PATH and `CMAKE_PREFIX_PATH=D:\Programs\Qt\6.11.2\mingw_64`; Qt Creator does all of this through the kit. For MSVC: after `vcvars64.bat`, `CMAKE_PREFIX_PATH=D:\Programs\Qt\6.11.2\msvc2022_64`, presets `windows-msvc-*`. App: `build\windows-mingw-debug\Semplice.exe`; `Semplice.exe --grab out.png` saves a screenshot of the window and quits. Test executables print to stdout only from a console; use `-o file,txt` or the Qt Creator test pane. The first configure downloads TagLib, so it needs internet once.)
`CMakePresets.json` must provide configure/build/test presets for debug, release and (for the core on Linux CI) `linux-core`. Opening the folder in Qt Creator must configure automatically.

## Architecture rules
- Layers: `qml/` -> `ui/` -> `platform/` -> `core/` -> (Qt Core/Sql only). `core` never includes QtGui/QtQml/QtQuick. QML contains presentation only; all logic is in C++ view-models/services.
- Names: namespace `semplice`; classes `PascalCase`; methods/variables `camelCase`; members `m_`; files match the class name. Use `enum class`, `std::optional`, `std::span`, RAII, smart pointers; no raw owning pointers; QObject parenting for Qt objects. No exceptions across module boundaries: use `Result<T, Error>` (SPEC 3).
- **Filesystem rules (non-negotiable):** (1) only `FileStorageService` touches library files, always via the `OperationJournal` (SPEC 4.5); (2) the user's original files are read-only to us: import copies, never moves/modifies/deletes; (3) deletions go to the Recycle Bin via `ITrash`, never permanent; (4) every path passes through `PathSanitizer`; compare paths/names case-insensitively on NFC-normalised text; (5) stored paths are library-relative.
- **Database rules:** schema changes only as new numbered migrations; foreign keys ON; WAL; one serialised writer; all multi-row changes in a transaction; repositories return domain structs, never `QSqlQuery`; the GUI thread never runs slow queries.
- **Threading:** no filesystem/TagLib/DB work longer than ~5 ms on the GUI thread; long jobs are cancellable and report progress.
- **Audio:** nothing outside `QtAudioBackend` may use `QMediaPlayer`/`QAudioOutput` types (a BASS backend is planned).
- **i18n:** every user-visible string goes through `tr()`/`qsTr()`; plural forms via `%n`; no string concatenation for sentences; on-disk names stay English.
- **UI rules:** colours, spacing, radii, fonts, durations come from the `Theme` singleton tokens only (no magic numbers in views); icons are SVG; all text truncates with ellipsis + tooltip; no fixed-width text buttons (Russian is ~35 % longer); lists/grids are virtualised and backed by lazy C++ models; images through the async provider and thumbnail cache. Fix the owner's UI typos listed in SPEC 13.
- **Performance:** budgets are in SPEC 9. Add a benchmark when you touch a hot path (startup, search, list scrolling, import).

## Testing rules
Unit tests for all logic in `core` (QtTest + CTest); integration tests with the synthetic library generator in `tests/tools/`; tests must be deterministic (inject clocks and RNG seeds). A bug fix starts with a failing test. Crash-recovery tests for the journal kill the operation at every step. Windows-only code gets a manual checklist entry.

## Definition of done (per task)
Builds without warnings (MSVC `/W4`, treat new warnings as errors in `core`), tests added and green, strings translated/marked, no hard-coded user text or colours, no TODOs without a `DECISIONS.md` entry, docs updated.

## Communication style with the owner
The owner is not a C++ specialist: explain decisions in plain language, show screenshots or short logs when reporting UI work, and list clearly what they need to test by hand. Keep reports short.
