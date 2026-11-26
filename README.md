# Semplice

**Semplice** is a fully offline music player for Windows with a *managed library*. You give it your music; it copies the files into a library folder that it keeps in order (`Artists/<artist>/<album>/`, `Playlists/`), remembers everything in a small database, and plays it. No accounts, no advertising, no internet connection.

Built with C++20, Qt 6 (QML), SQLite and TagLib.

## Features

- **Managed library** - add files or folders (drag them onto the window); albums, singles, EPs and playlists are recognised from the tags. Your original files are never moved, changed or deleted; deleted items go to the Recycle Bin.
- **Artists, albums, playlists** - cards with covers, tags (genres, lyric language, mood), an editor for everything, artists that can be hidden from the library.
- **Home** - carousels built from your own library (recently played, discographies, years, decades, genres and combinations such as "Cheerful English Rap"); a recommender chooses what plays next.
- **Player** - queue, shuffle / repeat, media keys and the Windows media panel, taskbar thumbnail buttons, a collapsible player mode with the controls in the hotbar.
- **Share** - pack a playlist, an album or a whole discography, audio included, into one `.smpl` file; drop such a file on the window, tick what you want, and it is added to the library (what you already have is skipped).
- **Your window, your layout** - Settings -> Window placement: drag the library, the main window, the player, the queue and the hotbar where you want them. Light and dark themes, text size.
- **Four languages** - English, Russian, Chinese (Simplified), Japanese.
- **Safe by design** - every change to the library goes through a journal, so a crash or a power cut is repaired at the next start.

## Download and run

Semplice is distributed as a portable folder: unpack it anywhere and run `Semplice.exe` (Windows 10 or 11, 64-bit) in 'dist' directory. On the first start you accept the license and choose an empty folder for the library.

## Build from source

You need Windows, CMake 3.25+, Ninja, Qt 6.8 or newer (6.11.2 is used for the releases) with the MinGW kit (or MSVC 2022), and an internet connection for the first configure (it downloads TagLib).

```
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release
ctest --preset windows-mingw-release --output-on-failure
```

Opening the folder in Qt Creator configures it automatically. Presets for debug and for MSVC (`windows-msvc-*`) are in `CMakePresets.json`. The portable folder (`dist\Semplice\`) is made with:

```
powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -SkipInstaller
```

## Repository layout

| Folder | What is in it |
|---|---|
| `src/core` | library rules, database, file storage, tags, search, recommender, sharing (no Qt GUI code) |
| `src/platform` | Windows-only code: audio output, media panel, taskbar buttons, file attributes |
| `src/ui` | view-models between the core and the screens |
| `src/qml` | the screens (presentation only) |
| `tests` | unit and integration tests (QtTest + CTest) |
| `resources`, `translations` | icons, fonts, genre / language / mood vocabularies, the license text; translations |
| `docs` | specification, design notes, decisions log, QA checklist |
| `scripts`, `installer`, `cmake` | packaging, icon generation, the Inno Setup script, build helpers |

## Documentation

`docs/SPEC.md` (what the program does and why), `docs/TECH_DESIGN.md` (how it is built), `docs/DECISIONS.md` (every decision with its reason), `docs/QA_CHECKLIST.md`, `docs/TRANSLATIONS.md`, `docs/GENRES.md`.

## License and third-party software

The license and user agreement is in `resources/license/LICENSE.txt` (also shown in the program: Settings -> License). The components Semplice uses (Qt, FFmpeg, TagLib, the Inter typeface and others) keep their own licenses, listed in `THIRD_PARTY_NOTICES.md`.

## Support

Semplice is free. If you like it, you can support me on Patreon: https://www.patreon.com/DonEb14n

## Changelog

26.11.2026 v1.0 
Public release
