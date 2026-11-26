# M10 - Localisation, polish, performance

Goal: nothing untranslated, nothing hard-coded, every SPEC 9 budget measured and written down, the app usable by keyboard and at every scale.

## Work items
| # | Item | How |
|---|---|---|
| A | **String check** | `scripts/check_strings.ps1` (also run by CTest as `no_untranslated_strings`): scans QML for user-visible properties (`text`, `title`, `placeholderText`, `tooltip`, `label`, `description`, `nameFilters`, `ToolTip.text`) set from a plain string literal that contains a letter and is not inside `qsTr()`; scans C++ in `src/ui`, `src/app`, `src/platform` for `QStringLiteral`/literals passed to the banner / label APIs is NOT attempted (too noisy); instead C++ user text is checked by the rule "every `tr(` / `translate(` source exists in `app_en.ts`" (lupdate is the authority) plus a review of `src/ui` for display strings. A small allow-list file holds the legitimate exceptions (symbols such as "#", "-", the app name). |
| B | **Translation drafts** | `app_ru/zh_CN/ja.ts` are machine drafts: each file gets a clear `<!-- MACHINE DRAFT -->` header comment and `docs/TRANSLATIONS.md` explains how the owner reviews them. A test checks no `type="unfinished"` remains and every numerus message has the right number of forms (ru 3, zh_CN 1, ja 1, en 2). |
| C | **CJK / fonts** | screenshots of Home, an album, an editor and Settings in ja and zh_CN; fix clipped text or wrong fallback. |
| D | **DPI matrix** | the app run with `QT_SCALE_FACTOR` 1, 1.25, 1.5, 2 (the 100 / 125 / 150 / 200 % cases) at the minimum window size; screenshots checked for clipping and overlaps; fixes go into Theme tokens / layouts. |
| E | **Keyboard and focus** | `IconButton`, `PillButton`, `Chip`, list rows and tabs take focus with Tab, show a focus ring (Theme token), and activate with Space / Enter; popups return focus; Escape closes. Shortcut list of SPEC 8.7 re-verified. |
| F | **Tooltips** | every icon-only button has a tooltip (script check on `IconButton` use without `tooltip`); truncated texts show a tooltip (already the ElideText rule, re-verified). |
| G | **Performance report** | `docs/PERF_REPORT.md`: each SPEC 9 row against measured numbers at 50k and 200k, Release, MinGW and MSVC. New measurements: cold start to visible window and Home usable (timestamps logged by the app, `--perf-log`), idle memory, scrolling (the existing `--bench-scroll`), import throughput (`semplice_libtool` on generated files), thumbnail cache size / cleanup, recommender pick. Rows that cannot be measured by a machine (60 fps on a laptop GPU) say so. |
| H | **Thumbnail cache** | check the 1 GB cap and LRU cleanup exist and are tested; add them if missing. |
| I | **Notices** | `THIRD_PARTY_NOTICES.md` lists every shipped component: Qt modules, FFmpeg (via Qt Multimedia), TagLib, utfcpp, Inter, plus the Qt LGPL relinking note. |
| J | **Crash / log review** | read the logs written by the test sessions of M8-M10, remove noisy or misleading messages, make sure warnings carry the context needed (QML warnings listed in the log are fixed, not hidden). |

## Not in M10
New features, theme variants, installer (M11).

## Tests
String check passes on the tree and fails on a deliberate sample (script self-test); translation completeness / numerus forms; thumbnail cache cap; the existing suites stay green on MinGW debug / release and MSVC.
