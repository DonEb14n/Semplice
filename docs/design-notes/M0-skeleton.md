# M0 - Skeleton and tooling: design note (for owner review)

Goal: an empty but real app that opens in Qt Creator with no manual setup, builds debug/release with MSVC, runs `ctest`, and shows the four-zone shell with placeholders. **No feature code.** After your review I implement exactly this.

## 1. Environment found
- Qt **6.11.2 msvc2022_64** at `D:\Programs\Qt\6.11.2\msvc2022_64` (has Quick, Sql, Multimedia, Test, LinguistTools), Ninja and CMake in `D:\Programs\Qt\Tools`, CMake 4.4.3 on PATH, VS 2022 Community. Qt version floor in CMake: **6.8**; I will not use APIs newer than 6.8.
- Presets must not hard-code Qt paths. Qt Creator injects `CMAKE_PREFIX_PATH` from the selected kit. For command-line use I add `CMakeUserPresets.json` (git-ignored, template `CMakeUserPresets.json.example`) that sets `CMAKE_PREFIX_PATH` to the path above.

## 2. Files created / replaced
```
CMakeLists.txt                 replaced (project, options, add_subdirectory)
CMakePresets.json              windows-msvc-debug|release, linux-core (configure/build/test)
cmake/Warnings.cmake  Version.cmake  TagLib.cmake(stub, TagLib arrives in M3)
src/app/        main.cpp, AppBootstrap.{h,cpp}, app.manifest, app.rc, CMakeLists.txt
src/core/       util/{Result.h, Error.{h,cpp}, ErrorCodes.h, Logger.{h,cpp}, Clock.h}
                services/{ISettings.h, SettingsService.{h,cpp}}   (QSettings wrapper)
                CMakeLists.txt
src/platform/   win/{SingleInstance.{h,cpp}, CrashHandler.{h,cpp}}, CMakeLists.txt
src/ui/         ShellVm.{h,cpp}, ThemeInfo (none), CMakeLists.txt
src/qml/        Main.qml, theme/Theme.qml (+qmldir singleton), theme/Icons.qml,
                panels/{LibraryPanel,HotbarPanel,PlayerPanel,SidePanel}.qml, components/Panel.qml
resources/      fonts/Inter-*.ttf (OFL), icons/*.svg, license/LICENSE.txt (placeholder)
translations/   app_en.ts app_ru.ts app_zh_CN.ts app_ja.ts
tests/          CMakeLists.txt, unit/{tst_result.cpp, tst_settings.cpp, tst_logger.cpp}
docs/           DECISIONS.md, QA_CHECKLIST.md, DESIGN_TOKENS.md, THIRD_PARTY_NOTICES.md (root)
```
Removed: starter `main.cpp`, `Main.qml`, `importedcontent/` hook. Targets: `semplice_core` (static), `semplice_platform`, `semplice_ui`, executable **`Semplice`** (`Semplice.exe`, WIN32 subsystem).

## 3. Components

**`Result<T>` / `Error` (core/util).** `Error{code, symbol, devMessage, userMessageKey}`; `ErrorCode` enum with a registry table (`E_UNKNOWN=-1` is the generic "Error -1" banner). `Result<T>` holds `std::variant<T, Error>`; `Result<void>` specialised; `value()`, `error()`, `ok()`, `operator bool`. Tests: construct/inspect, void result, error symbol round-trip.

**Logger (core/util).** `Logger::init(dir)`, levels (debug/info/warn/error), macros `SLOG_INFO(...)`. Writes `%LOCALAPPDATA%\Semplice\logs\semplice.log`, rotates at 2 MB keeping 5 files; thread-safe (mutex + `QFile`); also routes `qDebug/qWarning` via `qInstallMessageHandler`. Test uses a temp dir and checks rotation.

**Crash handler stub (platform/win).** `SetUnhandledExceptionFilter` writes `crash-<timestamp>.txt` (exception code, address, thread, app version) next to the logs. Minidump deferred (noted in DECISIONS). Enabled in release and debug.

**Single instance (platform/win).** Named mutex `Local\Semplice-SingleInstance` + a `QLocalServer` named `Semplice-activate`. A second launch connects, sends `activate`, exits with code 0; the first instance restores (if minimised) and raises its window.

**SettingsService.** `QSettings` INI at `%APPDATA%\Semplice\settings.ini` (path injectable for tests). Typed accessors: `language` (`en|ru|zh_CN|ja`, default `en`), `windowGeometry` (x,y,w,h,maximised), `volume` (0.5), `muted`, `userName`, `theme`. Test: defaults, round-trip, unknown language falls back to `en`.

**i18n.** `qt_add_translations` over the four `.ts` files (en is the source file with identity entries). At startup `AppBootstrap` reads `language` and installs a `QTranslator` before the QML engine loads; unknown language -> English. Strings in M0 (panel titles, placeholders, "Welcome, {name}!") all use `qsTr()`. ru/zh/ja files in M0 hold a few machine-drafted entries (flagged `<!-- DRAFT -->` in the file header, per SPEC 12) so you can check "change `language=ru` in settings.ini and restart" works.

**Theme.qml singleton.** Tokens only, no logic: colours from SPEC 8.2 (`background #9C9C9C`, `surface #FFFFFF`, `text #1E1E1E`, `textSecondary #5A5A5A`, `disabled #B3B3B3`, `danger #C00F0C`); spacing `s1=4, s2=8, s3=12, s4=16, s5=24, s6=32`; radii `panel=12, control=8, pill=999`; type scale (`title 20`, `heading 16`, `body 13`, `caption 11` px, weights regular/medium/bold); icon sizes `16/20/24`; durations `fast 120`, `normal 200`; layout constants from SPEC 8.1 (hotbar 56, window min 1000x620, default 1280x760, library 24 % / min 260 / max 340, right column 26 % / min 300 / max 380, gap 8). Full table is written to `docs/DESIGN_TOKENS.md` for your review.

**Fonts.** Inter (Regular/Medium/Bold, OFL) bundled as resources and registered with `QFontDatabase` at startup; global fallback chain `Inter, Segoe UI, Yu Gothic UI, Microsoft YaHei UI, Malgun Gothic` applied via `QFont::setFamilies`. For `ja`/`zh_CN` the primary family becomes Segoe UI (UI doc p.52). **I need the Inter font files**: I'll download the official release from rsms/inter (OFL) *only if you allow a download*; otherwise drop the three `.ttf` files into `resources/fonts/` yourself and I add the notice. (Falls back to Segoe UI without them; nothing breaks.)

**Icons.** SVG files in `resources/icons/` (placeholder geometric set for M0: home, settings, search, play, pause, next, previous, shuffle, repeat, volume, queue, plus, music-note, folder). `Icons.qml` maps a name to `qrc:/...svg`; an `Icon.qml` component recolours via `MultiEffect`/`colorization` (Qt 6.5+) so one black SVG works for all states. Logo (faceted four-point star) redrawn as SVG from UI page 52 in M0 as `logo.svg`; replaced when you provide the file.

**Main window (QML).** `ApplicationWindow` 1280x760 default, min 1000x620, `Theme.background`. Layout is a fixed-proportion `Item` (no `RowLayout` stretch surprises): left `LibraryPanel` (24 %, clamped 260-340), right column (26 %, clamped 300-380) containing `PlayerPanel` (63 % of height) over `SidePanel` (rest, min 200), middle `Rectangle` placeholder (min 420) showing "Welcome, {name}!" ("Listener" default), bottom `HotbarPanel` 56 px full width. Panels are `Panel.qml` (white, radius 12, 8 px gaps) with a title and placeholder content; no logic. The hotbar shows non-functional icons only.

**Geometry persistence.** On close, `ShellVm` saves position/size/maximised through `SettingsService`; on start it restores, and if the saved rect does not intersect any `QScreen::availableGeometry` it falls back to centred default. Minimum size enforced by `minimumWidth/Height`.

**Manifest + DPI.** `app.manifest` embedded via `app.rc`: `longPathAware=true`, `dpiAwareness=PerMonitorV2`, supported OS GUIDs for Windows 10/11, `asInvoker`. `QGuiApplication::setHighDpiScaleFactorRoundingPolicy(PassThrough)` before construction.

**Composition root.** `AppBootstrap` (no globals): creates logger -> single-instance check -> settings -> translator -> fonts -> `ShellVm` -> QML engine -> loads `Semplice/Main.qml`. Manual DI; the `LibrarySession` concept from TECH_DESIGN section 1.3 is introduced in M1 when there is a library.

**Tests (QtTest + CTest).** `tst_result`, `tst_settings`, `tst_logger`, wired with `qt_add_test` so they appear in Qt Creator's test pane; `ctest --preset windows-msvc-debug` runs them. Linux preset builds only `semplice_core` + tests (platform/ui/qml excluded via `if(WIN32)`).

**Housekeeping docs.** `docs/DECISIONS.md` (seeded with the decisions below), `docs/QA_CHECKLIST.md` (M0 section), `THIRD_PARTY_NOTICES.md` (Qt, Inter listed; TagLib/FFmpeg added later).

## 4. Test list (M0)
1. `Result`: value/error/void/symbol lookup.
2. `SettingsService`: defaults; write+reload; invalid language -> `en`; geometry round trip.
3. `Logger`: writes lines with level; rotates at the size cap; keeps N files.
4. (manual) Items in the acceptance list below.

## 5. Acceptance (from the plan) and what **you** do to verify
1. Open the `Semplice` folder in Qt Creator, select the **Desktop Qt 6.11.2 MSVC2022 64-bit** kit -> it configures with no manual step. *(I will build with the same toolchain and report real output.)*
2. Build Debug and Release; run. You see the four zones with placeholders and "Welcome, Listener!".
3. Resize below 1000x620 -> blocked; move/resize/maximise, close, reopen -> restored. Put it on a second monitor, unplug/disable that monitor, reopen -> appears on the main one.
4. Start `Semplice.exe` a second time -> the first window comes to the front, no second window.
5. Edit `%APPDATA%\Semplice\settings.ini` -> `language=ru`, restart -> Russian placeholder strings.
6. `ctest --preset windows-msvc-debug --output-on-failure` (or Qt Creator's test pane) is green.
7. A log file appears in `%LOCALAPPDATA%\Semplice\logs`.

## 6. Not in M0 (deliberately)
Database, library folder selection, TagLib, audio, SMTC, taskbar buttons, real icons from Figma, any behaviour in the hotbar/panels.

## 7. Open points for you
- **Inter font**: may I download it from the official GitHub release, or will you place the files? (see Fonts) Download from Github
- **Warnings as errors** in `semplice_core` from day one (`/WX`): OK
- **Executable/target names**: target `Semplice`, display name "Semplice", organisation name "Semplice" for QSettings paths. OK
