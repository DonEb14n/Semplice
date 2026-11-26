# Translations

Languages: English (source), Russian, Chinese (Simplified), Japanese. Files: `translations/app_<lang>.ts` (Qt Linguist format), compiled into the program by the build.

**ru, zh_CN and ja are MACHINE DRAFTS** written by Claude Code (SPEC 12). They are complete and grammatically plausible but have not been reviewed by a native speaker. Please have them read before the beta.

## Review
1. Open `translations/app_ru.ts` (or zh_CN / ja) in Qt Linguist (installed with Qt: `D:\Programs\Qt\6.11.2\mingw_64\bin\linguist.exe`).
2. Edit a translation; Linguist saves the .ts file. Keep `%1`, `%2` and `%n` exactly as they are: they are filled in by the program ("%n track(s)" has one form per plural case; Russian has three, Chinese and Japanese one).
3. Rebuild; the new text shows after a restart with that language.

## Checks that run automatically
* `tst_translations`: no untranslated message, every plural message has the right number of forms (en 2, ru 3, zh_CN 1, ja 1).
* `no_untranslated_strings`: fails when a QML file sets visible text without `qsTr()`.

## Adding a string
Write it in English inside `qsTr()` / `tr()`; run `lupdate src -ts translations/app_*.ts`; translate the new entries (an untranslated one fails `tst_translations`).
