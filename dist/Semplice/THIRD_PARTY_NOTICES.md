# Third-party notices

Semplice ships the components below. Each keeps its own licence; the full texts are in the licence files named here or at the links.

| Component | Version | Licence | Use | How it is shipped |
|---|---|---|---|---|
| Qt 6 - Core, Gui, Network, Sql (SQLite driver), Qml, Quick, Quick Controls 2 (Basic style), Quick Dialogs, Quick Effects, Svg, Multimedia | 6.11.2 | LGPL-3.0 (https://www.qt.io/licensing, https://doc.qt.io/qt-6/lgpl.html) | UI, database access, audio output | dynamic DLLs next to the program (`windeployqt`); users may replace them with their own build of Qt |
| FFmpeg (inside Qt Multimedia's FFmpeg backend) | 7.1.x | LGPL-2.1-or-later (https://ffmpeg.org/legal.html) | decoding audio files | dynamic DLLs shipped by Qt |
| SQLite (inside Qt's SQL driver) | 3.x | Public domain | the library database | part of Qt |
| TagLib | 2.3.2 | LGPL-2.1 or MPL-1.1 (https://taglib.org) | reads and writes audio tags and embedded covers | statically linked; source is fetched by `cmake/TagLib.cmake` and can be rebuilt by anyone |
| utfcpp (bundled with TagLib) | 4.x | BSL-1.0 | UTF handling inside TagLib | statically linked |
| Inter typeface (Regular, Medium, Bold) | 4.x | SIL Open Font License 1.1 - `resources/fonts/Inter-OFL.txt` | Latin and Cyrillic text | embedded in the program |

Fonts of the operating system (Segoe UI, Yu Gothic UI, Microsoft YaHei UI, Malgun Gothic) are used as fallbacks for Japanese and Chinese text; they are not shipped.

## LGPL notes
* Qt and FFmpeg are linked dynamically: do not link them statically and do not remove the licence texts from the installer.
* The installer must offer the licence texts and say where the Qt and FFmpeg sources can be obtained (links above).
* TagLib is linked statically under the LGPL: keep the TagLib source reference (`cmake/TagLib.cmake`, pinned version) available so a user can relink.
