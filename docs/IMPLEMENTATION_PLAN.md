# Semplice - Implementation plan

Work one milestone at a time. Each milestone has a **goal**, **scope**, **acceptance criteria** and a **prompt** you can paste into Claude Code (the repository's `CLAUDE.md` is loaded automatically, so prompts stay short). Do not start milestone N+1 until the owner has accepted N.

Dependency order: M0 -> M1 -> M2 -> M3 -> M4 -> M5 -> M6 -> M7 -> M8 -> M9 -> M10 -> M11. (M5 playback core can run before the UI; M9 recommender plugs into M5.)

---

## Kickoff (paste this first, in an empty repo containing this documentation)

> Read `CLAUDE.md`, then `docs/SPEC.md` fully, then `docs/UI-reference.md` (open the page screenshots under `docs/design/pages/` as you go). Do not write code yet. Produce `docs/TECH_DESIGN.md` for my review with: (1) the module/class diagram for all layers, (2) the final public interfaces of `core` services, (3) the full SQL schema as migration 0001 with any changes to `docs/schema/0001_init.sql` explained, (4) the thread model, (5) how the QML models/view-models are organised, (6) the CMake/Qt Creator project layout, (7) a risk list, (8) any contradiction or gap you found in the spec, with a proposed resolution. Keep it under ~12 pages. Stop and wait for my approval.

---

## M0 - Skeleton and tooling
**Goal:** an empty but real app that builds in Qt Creator and shows the four-zone shell.
**Scope:** repo layout from the UI doc's source tree (`semplice/`), CMake + presets, `semplice_core` static lib, `app/` bootstrap with manual DI, `Result<T,Error>` + error-code registry, logger (rotating files, crash handler stub), single-instance guard, `QSettings` wrapper, i18n scaffolding (4 `.ts` files, language loaded from settings), `Theme.qml` singleton with the palette + a **design-token table** (`docs/DESIGN_TOKENS.md`), Inter font bundled + fallback chain, SVG icon pipeline, main window with fixed-proportion zones and sizes from SPEC 8.1 (placeholders only), window geometry persistence, manifest (`longPathAware`, per-monitor DPI), QtTest setup, `THIRD_PARTY_NOTICES.md` stub, `docs/DECISIONS.md`, `docs/QA_CHECKLIST.md`.
**Accept:** opens in Qt Creator with no manual setup; debug/release build; `ctest` runs; window respects min size and restores position; second launch focuses the first; changing language in `settings.ini` and restarting switches the strings.
**Prompt:**
> Implement milestone M0 from `docs/IMPLEMENTATION_PLAN.md` following my approved `docs/TECH_DESIGN.md`. Provide `CMakePresets.json` that works from Qt Creator. At the end, update the build commands in `CLAUDE.md`, and give me a checklist of what to click to verify it on my machine.

## M1 - Domain and database
**Goal:** the library model with correct invariants, no UI.
**Scope:** domain structs/enums, migrations from the approved schema, repositories (artist, album, playlist, track, tags, recent, kv), `Normalizer` (NFC, casefold, diacritics strip), uniqueness and cascade rules from SPEC 5 and 6.4 as **service-level operations on the DB only** (file operations are stubbed behind `IFileStorage`), in-memory search index, synthetic library generator (DB-only mode, 50k and 200k presets), benchmark skeleton.
**Accept:** unit tests for every invariant in SPEC 5 (uniqueness per primary artist, hidden-artist GC, Unsorted tracks virtual vs real, empty album/playlist cascade, re-home, tag key validation, date validation); generator creates 200k tracks; search index p95 within budget on 50k and 200k.
**Prompt:**
> Implement M1. Start with the migration and repository tests. Include a test for each rule in SPEC section 5 and the cascade table in 6.4. Report benchmark numbers for the generator at 50k and 200k tracks.

## M2 - Storage layer
**Goal:** safe file operations.
**Scope:** `PathSanitizer` (every row of SPEC 4.1 with tests, including reserved names, trailing dots, case-only rename, NFC, 120-char truncation on code points, collisions, long paths), `FileStorageService`, `OperationJournal` with startup recovery, `ITrash` (Windows Recycle Bin impl + test fake), copy-with-hash (SHA-256 while copying, cancellable, progress), home-location move rule (4.2), rename operations (4.3), library bootstrap (`.library`, `library.json`, hidden attribute, backups rotation, `quick_check`).
**Accept:** crash-injection tests at every journal step leave disk and DB consistent; no code path deletes permanently; path tests all green; library create/open/refuse-newer-schema works.
**Prompt:**
> Implement M2. Write the PathSanitizer tests first from the table in SPEC 4.1. Then the journal with a fault-injection hook that can abort at any step, and tests that restart recovery after each abort.

## M3 - Tags
**Goal:** read/write metadata and the genre vocabulary.
**Scope:** `TagService` (TagLib read + write for MP3, FLAC, M4A, OGG, WAV, WMA; multi-value artists; embedded cover read/write; temp-copy + atomic replace; released-file handshake with `IAudioBackend::release`), `ICodepageConverter` (Windows impl via `MultiByteToWideChar`, test fake) and `EncodingFixer`, `resources/tags/genres.json` (draft ~60-80 genres, `en/ru/zh_CN/ja` names, aliases incl. ID3v1 0-191, `related`/`opposite`), `GenreMap`, cover extraction + cover-file discovery rules (SPEC 4.1).
**Accept:** round-trip tests per format using generated tiny files; mojibake fixtures for CP1251/GBK/Shift-JIS/Big5 are corrected and correct UTF text is untouched; vocabulary file validates (every alias resolves, relations symmetric where intended, no unknown keys). **The owner reviews the genre file** before M4 ends.
**Prompt:**
> Implement M3. For the EncodingFixer, build the fixtures from known-good strings re-encoded to the wrong code page so tests prove both directions. List the genre vocabulary and relations in a table in `docs/GENRES.md` for my review.

## M4 - Import and library services
**Goal:** the complete non-UI behaviour of managing a library.
**Scope:** `ImportService` (scan, classify 1-3/4-7/8+, form-prefill model, commit job with hash dedupe, cancel + rollback, progress), `LibraryService` (create/edit/delete artist, album, playlist, track; add/remove memberships; add to discography; home-move; renames; tag seeding rules; hidden-artist transitions), `RescanService` (SPEC 6.5), change-library flow (6.6), maintenance (6.7).
**Accept:** integration tests on a generated library covering import -> edit -> rename -> delete, duplicate import, cancel mid-import, rescan with manual files and missing files, re-homing, delete-artist outcomes matching the UI doc's two cases; tags in files match the DB after each operation.
**Prompt:**
> Implement M4. Every public operation must be covered by an integration test that asserts both the database state and the files on disk. Pay special attention to SPEC 5.2 (Unsorted tracks) and 6.4.

## M5 - Playback core
**Goal:** correct playback logic and persistence, driven by tests (UI later).
**Scope:** `IAudioBackend`, `QtAudioBackend` (FFmpeg backend, gapless preload where possible, `release(path)`), queue-group model, shuffle-bag, repeat modes, Previous (5-second rule), end-of-queue rules, carousel multi-group queues with go-back, session persistence/restore (paused, skip missing), Recently Played rule (30 s / 50 %), play stats, missing-file skip + one banner event, volume/mute persistence. Windows integration in `platform/`: SMTC, taskbar thumbnail toolbar, single-instance activation, media keys.
**Accept:** state-machine unit tests with a fake backend for all rules in SPEC 7.2-7.5; manual checklist for audio output, SMTC flyout, thumb-bar buttons and media keys on Windows 10 and 11.
**Prompt:**
> Implement M5. First write the playback state-machine tests against a fake backend. Then QtAudioBackend. Then the Windows integration in `platform/`, each behind an interface. Add manual QA steps for everything that cannot be unit tested.

## M6 - UI shell and panels
**Goal:** the persistent chrome of the app working with real data.
**Scope:** Library panel (tabs carousel-switcher, three view modes, sort/filter menu, persistence rules, `+` submenu, activate/play rules), Hotbar (search, Home, back/forward history, settings, volume + mute, queue button), Player panel (all states from the UI doc, header titles, cover rules, progress bar with hover time, controls), Side panel (prev/current/next with click and drag), search popup (5 per category, "Show more", rules for playing), queue popup, banner system (max 3), keyboard shortcuts (SPEC 8.7), async image provider + thumbnail cache.
**Accept:** matches the UI doc's behaviour page by page (checklist in `docs/QA_CHECKLIST.md`); lists with 50k items scroll at 60 fps in the benchmark; Russian strings never overflow.
**Prompt:**
> Implement M6. Go through UI-reference pages 1-30 one at a time, open each screenshot, and tick them off in a checklist in `docs/QA_CHECKLIST.md`. Use only theme tokens. Send me screenshots of each panel in empty, populated and long-Russian-text states.

## M7 - Home view
**Goal:** the greeting, Recently Played and generated categories.
**Scope:** greeting logic and draft English texts (SPEC 8.6), carousel component (circular, <= 50 items, drag, click), Recently Played, category generator (types, randomisation cycle, minimums, lazy generation on scroll), category tile view, empty state with "Select folder", drop zone overlay.
**Accept:** deterministic tests for the generator with a seeded RNG; carousel stays at 60 fps with 50 items; empty library and first-launch flows match the UI doc.
**Prompt:**
> Implement M7. Make the category generator a pure core class with a seedable RNG and tests for the "no type repeats until all used" rule and the minimum-5 rule.

## M8 - Cards, editors and dialogs
**Goal:** every remaining screen in the UI doc.
**Scope:** artist card, hidden-artist card, album card, playlist card, tile views, Add/Edit artist, album, playlist, track editor (with tag picker), Add-artist/Add-to-playlist/Add-to-album submenus, release date wheel picker, delete confirmation dialogs with outcome-accurate text, import progress bar with Cancel, drag-and-drop import entry points (including dropping onto an open artist page), settings screen (restart markers), license dialog (both modes), validation per SPEC 8.3.
**Accept:** every flow in UI-reference pages 30-54 works end to end against M4 services; validation matches the table; all destructive dialogs state the real consequence.
**Prompt:**
> Implement M8 in this order: cards, then Add/Edit windows, then deletes, then settings. For the delete dialogs, compute the outcome from the service layer first and generate the warning text from it.

## M9 - Recommender
**Goal:** endless stream.
**Scope:** pure scorer + selector with `RecommenderConfig`, variety constraints, look-ahead, relaxing rules, integration with the queue ("Up next" accuracy), player header "Recommendations".
**Accept:** unit tests for scoring (including related/opposite genre), selection constraints (<= 2 per album per 10, no immediate same album, no repeats until exhausted), relaxation order; manual listening session notes.
**Prompt:**
> Implement M9. Keep the scorer pure. Provide a debug-only view (hidden behind a command-line flag) that prints the top candidates with their score components, so I can tune the weights.

## M10 - Localisation, polish, performance
**Scope:** machine-translated ru/zh_CN/ja drafts (flagged as drafts), plural forms, CJK font check, DPI matrix at 100/125/150/200 %, keyboard/focus order, tooltips, benchmark run against every row of SPEC 9 with a written report, memory/thumbnail cache checks, `THIRD_PARTY_NOTICES.md` complete, crash/log review.
**Accept:** performance report with numbers vs. budget; no untranslated strings; no hard-coded strings (script check); owner signs off on the visual pass.
**Prompt:**
> Implement M10. Write a script that fails if a user-visible string is not wrapped in tr()/qsTr(). Produce `docs/PERF_REPORT.md` comparing each budget in SPEC 9 with measured numbers on 50k and 200k libraries.

## M11 - Packaging and beta
**Scope:** `windeployqt` deployment folder, Inno Setup script (license page from `resources/license/LICENSE.txt`, long-path note/registry option, per-user install, uninstaller keeps the user's library), version stamping, release build with symbols, beta checklist for the owner's three-month test (known issues template, how testers send logs from the "Logs folder" button).
**Accept:** clean install/uninstall on a fresh Windows 10 1809 VM and Windows 11; app launches, plays, imports; uninstall never touches the library folder.
**Prompt:**
> Implement M11. Produce `installer/Semplice.iss`, a `scripts/package.ps1`, and `docs/BETA_GUIDE.md` for testers.
