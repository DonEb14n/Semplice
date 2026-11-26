# QA checklist (manual)

Tick items after trying them on your machine. Extended in every milestone.

## M0 - Skeleton
Build: open the `Semplice` folder in Qt Creator, choose the kit **Desktop Qt 6.x MSVC2022 64-bit**, let it configure, press Run.

- [✓] Project configures in Qt Creator with no manual setup (kit: MSVC2022 64-bit).
- [✓] Debug build runs; Release build runs.
- [✓] Window shows four zones: Library (left), "Welcome, Listener!" (middle), Player + Queue (right), hotbar (bottom).
- [✓] Window cannot be resized below 1000 x 620.
- [✓] Move/resize/maximise the window, close it, start again: position, size and maximised state are restored.
- [✓] With the window on a second monitor: close the app, disconnect/disable that monitor, start again: window appears on the main monitor.
- [✓] Start `Semplice.exe` a second time: no second window; the first one comes to the front (also when it is minimised).
- [✓] Edit `%APPDATA%\Semplice\settings.ini`, section `[app]`, set `language=ru` (or `zh_CN`, `ja`), restart: strings are in that language. Set it back to `en`.
- [✓] Log file appears at `%LOCALAPPDATA%\Semplice\logs\semplice.log` and gets a line per start/exit.
- [✓] Tests: Qt Creator "Tests" pane or `ctest --preset windows-msvc-debug` - all 4 green.
- [✓] Look at the window at 100 / 125 / 150 % Windows scaling: text and icons are sharp, nothing is cut off.
- [✓] (Optional) Text looks right in Russian: nothing overflows the panels.

Not covered in M0 (nothing to test yet): library selection, database, playback, import.

## M1 - Domain and database (no UI; run on your machine)
- [✓] Qt Creator test pane (or `ctest --preset windows-msvc-debug --output-on-failure`): all 11 tests green (`tst_library_rules` has the 39 rule checks of SPEC 5 / 6.4).
- [✓] Generate a library and check it: `build\windows-msvc-release\semplice_gen.exe --preset 50k --out C:\temp\lib50k.db --check` prints `integrity: OK`. Same with `--preset 200k`.
- [✓] Benchmarks: `semplice_bench.exe --preset 50k` and `--preset 200k` stay under the budgets quoted at the end (search p95 < 100 ms / < 250 ms). Numbers from the development PC are in `docs/PERF_M1.md`.
- [✓] (Optional) Open a generated `.db` in "DB Browser for SQLite" to look at the data: artists with Cyrillic/Japanese names, hidden artists without folders, co-authors, tags.

## M2 - Storage layer (no UI; checks you run on your machine)
- [✓] Qt Creator test pane / `ctest --preset windows-msvc-debug`: all 19 tests green (path sanitizer, file storage, journal recovery with crash at every step, library folder, storage integration, Windows attribute, the two delete-guard checks). Also green in the Release preset.
- [✓] **Real Recycle Bin** (the other tests use a fake): in a console run `set SEMPLICE_TEST_REAL_TRASH=1` and then `buildwindows-msvc-debug	st_file_storage.exe` (Qt `bin` folder on PATH). A file `semplice-trash-test-....txt` must appear in the Windows Recycle Bin and be restorable from there. Remove it afterwards.
- [✓] Hidden folder: the Windows attribute test is green (`tst_windows_attributes`); after M3/M4 you will also see that `.library` does not show up in Explorer unless "Show hidden items" is on.
- [✓] Long paths: nothing to click yet; `PathSanitizer` and `E_PATH_TOO_LONG` are covered by tests; real >260 character paths are exercised from M4.
- [✓] Case-only rename on NTFS (`abba` -> `ABBA`): covered by `tst_file_storage` and the journal recovery scenario, both run on your real NTFS temp folder.

## M3 - Tags (no UI; checks you run on your machine)
- [✓] First configure downloads TagLib (internet needed once). Qt Creator: "Re-configure" shows no error; the Release preset also builds.
- [✓] `ctest --preset windows-msvc-debug`: all 24 checks green (new: genres, encoding fixer, tag service for six formats, cover discovery, Windows platform with the real code pages and `ReplaceFileW`).
- [✓] **Open `docs/GENRES.md` and review the genre list**: missing or unwanted genres, the ru/zh_CN/ja names (machine drafts), the `related` / `opposite` relations, the ID3v1 mapping. Tell me what to change; the file to edit is `resources/tags/genres.json`.
- [✓] Real-life check with a few of YOUR files (copies!): `build\windows-msvc-debug\semplice_tags.exe <file>` is not built yet - the first end-to-end check with real music comes with M4's import. Until then the generated test files in `tests/tools/TestAudioFiles` are the proof that all six formats round-trip.

## M4 - Import and library services (no UI yet; checks you run on your machine)
The services are driven by tests and by the small command-line tool `semplice_libtool` (built next to the tests). **Use COPIES of your music**: the tool copies files into the library and tags the copies, but it never changes the sources.
- [✓] Re-configure and build the default (MinGW) preset and the MSVC preset; `ctest --preset windows-mingw-debug --output-on-failure` and `ctest --preset windows-msvc-debug`: all 29 checks green (new: import, library edits, rescan, library session, artist splitter).
- [✓] Make an empty folder, e.g. `C:\temp\mylib`, and a folder with one real album (copies, ideally with tags and a cover). Run `semplice_libtool.exe import C:\temp\mylib C:\temp\album` (add `--year 2020` if the tags have no year). Expected: "album ... by ... (N tracks)", "imported N", and the last line `0 problems, 0 tag mismatches`.
- [✓] Look at `C:\temp\mylib` in Explorer: `Artists\<artist>\<album>\01 - <title>.<ext>` plus `album_cover.jpg/png`; `.library` is hidden. Open one file in your usual player or tag editor: the tags (title, artist, album, track number, genre, year, cover) are right and Russian / Chinese / Japanese text is readable.
- [✓] Run the same import again: "already in the library N", no new files, `0 problems`.
- [✓] Import a folder of unrelated songs: it becomes a playlist (`Playlists\<name>\Title - Artist.ext`); with `--playlist "Name"` you choose the name.
- [✓] Copy one more song by hand into `Artists\...` or `Playlists`, delete another one in Explorer, then `semplice_libtool.exe rescan C:\temp\mylib`: the new song is moved into a playlist `Rescan <date time>`, the deleted one is gone from the database, and `check` shows no problems. Rescan with the Recycle Bin open: a stray copy of a song you imported goes there and can be restored.
- [✓] Safety brake: delete (in Explorer) more than half of a library with 40+ songs and run `rescan`: it reports "missing kept (guard)" and removes nothing; `rescan --force` removes them.
- [✓] Open a song in a player that locks files, then rescan: nothing breaks. (Editing existing tags from a UI comes with M7; the tests cover locked files.)
- [✓] `semplice_libtool.exe check C:\temp\mylib` any time: reports files without rows, rows without files, and tag differences.
- [✓] Switching libraries: `semplice_libtool.exe info` on a second empty folder starts a new library there; on a folder with other files in it, it refuses and changes nothing.

## M5 - Playback core (checks you run on your machine)
The logic is covered by tests with a fake audio device. Sound and Windows integration need a person; use the test player `semplice_playtool`. Make a library first: `semplice_libtool.exe import C:\temp\mylib C:\temp\album --year 2020` (copies of real music!).
- [✓] `ctest --preset windows-mingw-debug --output-on-failure` and `ctest --preset windows-msvc-debug`: all 32 checks green (new: play queue, playback service, library playback).
- [✓] `build\windows-mingw-debug\semplice_playtool.exe C:\temp\mylib --album "<album name>"` (a playlist: `--playlist NAME`; everything: `--all`): a small window opens, the console prints `media controls attached: yes` and `now playing: ...`, and you HEAR the music on your usual output device.
- [✓] Keys in that window: Space = pause / play, Right = next, Left = previous (after 5+ seconds it restarts the song), Up / Down = volume, M = mute, S = shuffle, R = repeat (off, all, one), Q = quit. Pressing S or R does not restart the song.
- [✓] A track that ends is followed by the next one (a very short gap is accepted; no gapless mode). At the end of an album the player stops (recommendations come with M9).
- [✓] **Volume flyout**: click the speaker icon in the taskbar: the media widget shows title, artist, album and cover (if the album has one); its Play/Pause, Next, Previous work. Windows 10 and 11.
- [✓] **Media keys** (play/pause, next, previous) work while another program has focus.
- [✓] **Taskbar thumbnail buttons**: hover the taskbar icon; Previous / Play-Pause / Next appear, the middle one switches between play and pause, outer ones are greyed when nothing is before / after. Symbols visible on light and dark Windows themes.
- [✓] Quit with Q, start again with only the library path: the last song is back PAUSED near the same place; Space continues. Rename one song in Explorer before restarting: one line "1 track(s) could not be played and were skipped" and the player goes on.
- [✓] Delete the file of the playing song in Explorer, press Next: skipped once, no error loop.
- [✓] Afterwards `semplice_libtool.exe check C:\temp\mylib` still says `0 problems`.

## M6 - UI shell and panels (UI doc pages 3-22, 27-30; run `Semplice.exe` from `build\windows-mingw-debug`)
Tip: to look at a big library without importing music: `semplice_gen.exe --preset 50k --library C:\temp\lib50k`, then start Semplice with that folder (the songs of such a library cannot play). For real music use `semplice_libtool.exe import ...` (see M4).
- [✓] `ctest --preset windows-mingw-debug` and the MSVC preset: all 34 checks green (new: browse queries / requests / history / banners / UI state, UI models).
- [✓] **p.3-4 First launch**: start with no stored library (rename `%APPDATA%\Semplice\settings.ini` first): all panels are dimmed and a card "Select folder" is in the middle. Choosing a folder that has files in it shows a red banner "This folder is not empty..." and the card stays. An empty folder starts a new library and the app comes to life.
- [✓] **p.3 Locate your library**: close the app, rename the library folder, start again: card with "Locate your library" and the old path; choosing the renamed folder reopens it.
- [✓] **p.5 Artists tab**: names are listed (hidden artists are not); clicking the round avatar starts the whole discography (newest album first; with shuffle on, in random order); clicking the name opens a page (the middle window says which page is not available yet).
- [✓] **p.6 View and sort**: the button inside the active tab opens the menu: view modes on top (list, 2 per row, 3 per row), sorting below; choosing the active sort again flips the arrow (alphabet: down = A to Z). Switch to another tab and back: the sorting is the default again. Close and reopen the app: the active tab and each tab's view mode are remembered, the sorting is not.
- [✓] **p.7-8 Large icons**: 2 per row shows avatar and name; 3 per row shows pictures only (clicking plays, no names, no page).
- [✓] **p.9-12 Albums tab**: cover plays, title opens, artist line opens the artist; the tab title stays in the middle when you change tabs (click the neighbouring title). Sort menu has Year and the album type filter (all types are shown by default; hiding the last shown type shows all again).
- [✓] **p.13-16 Playlists tab**: cover plays, title opens; menu has only Alphabetical and Recently added.
- [✓] **p.17 Add**: the `+` button shows "Add" with Artist, Album, Playlist; each opens its page placeholder in the middle window.
- [✓] **p.17-18 Hotbar**: Home, Back and Forward (greyed when impossible) follow what you opened; Settings opens the settings page placeholder; volume starts at 50 %, clicking the speaker mutes (slider drops to 0, icon changes) and clicking again restores the old level; the level survives a restart. The queue button is grey while nothing is queued.
- [✓] **p.18 Search**: typing opens the results above the search field: Artists, Albums, Playlists, Tracks (categories without hits are missing), five each, "Show more" only when there are more. Avatar/picture plays, the name opens; a track starts a queue of only that track and nothing follows it. Clicking anywhere else, or Esc, closes the results; clicking the field brings them back with the same text. The search and queue popups never show together.
- [✓] **p.18 Queue popup**: shows the whole current album / playlist / discography top to bottom, the playing track highlighted; clicking a row plays it.
- [✓] **p.19 Side panel**: empty queue shows the placeholder; while playing, the previous track is above, the current one in the middle, the next one below. Clicking the previous / next track switches; so does dragging the middle one up (next) or down (previous).
- [✓] **p.20-22 Player**: empty state; header shows playlist name / album name / artist name with "Discography" below / "Recommendations"; clicking it opens that page. Cover: picture inside the file, else the album cover, else the grey note. Title and artist open their pages. Time on the left and right, progress bar with the time under the mouse; clicking or dragging seeks. Buttons: shuffle and repeat look grey when off (repeat has an "all" and a "one" look), previous is greyed at the very start, previous after 5+ s restarts the song.
- [✓] **p.27-28 Banners**: skip a file (rename one song of a playing album in Explorer): one banner "1 track could not be played and was skipped"; trigger more than three banners: the oldest disappears; each has an x.
- [✓] **Keyboard**: Space (not while typing), Ctrl+Left/Right, Ctrl+Up/Down (5 %), M (not while typing), Ctrl+F, Alt+Left/Right, Esc.
- [✓] **Languages**: set `language=ru` (also `zh_CN`, `ja`) in `settings.ini`: every label of the panels is translated (machine drafts, please review) and nothing overflows; long names show "..." and a tooltip.
- [✓] **Scrolling**: with the 200k preset library (`--preset 200k`, 20 000 albums) scroll the Albums list with the wheel and by dragging the scroll bar: smooth, no stalls. `Semplice.exe --bench-scroll` reports the frame times in the log (my run, Release, 20 000 albums: average 6.1 ms, 95th percentile 6.4 ms, worst 8.4 ms).
- [✓] **Not in M6** (they come later): the pages themselves (Home M7; cards, editors, settings, delete dialogs, import window and drop zone M8), the recommender M9.

## M7 - Home view (UI doc pages 23-26, 29; run `Semplice.exe` from `build\windows-mingw-debug`)
Tip: `semplice_gen.exe --preset small --library C:\temp\libsmall` gives a library with 40 albums and 10 playlists to look at (songs cannot play); for real music use `semplice_libtool.exe import` (M4).
- [✓] `ctest --preset windows-mingw-debug` and the MSVC preset: all 36 checks green (new: home rules and queries, Home view-models).
- [✓] **p.23 Empty library**: a new empty library shows "Welcome, Listener!" and nothing else in the middle window. Changing the name in `settings.ini` (`[app] name=Anna`) and restarting shows "Anna".
- [✓] **p.24 Greeting**: with content the header says "Good morning / afternoon / evening, <name>!" for the current time (05-11, 12-17, 18-04). (Occasion greetings on Dec 31 - Jan 2 and on Mar 1, Jun 1, Sep 1, Dec 1 are covered by tests; to see one, set the Windows date.)
- [✓] **p.24 Recently Played** is the first section, always. Before anything was played it says "Albums and playlists you play appear here." Play an album or playlist for 30 s (or half of a short song): it appears as the first tile, at once, without leaving Home. Playing the same one again moves it to the front (no duplicates). After a restart the list is still there.
- [✓] **p.24-25 Carousels**: five categories below Recently Played, each with a title and a ring of tiles: the middle one big and in front, the others smaller and fading. Drag sideways or use the mouse wheel to turn it; it never ends (round and round). Clicking a side tile brings it to the middle; clicking the middle tile starts it (the queue shows that album, the player header its name). Under the middle tile: the title and the artist(s); playlists only the title; in "<Artist>: Discography" only the album title.
- [✓] **Category choice**: the five carousels are different types (no two of the same kind while others exist); the titles are an artist's discography, a year, a decade (e.g. 1990s), a genre in your language, "Your Playlists", "Your Albums". Each year / decade / genre carousel has at least five albums. Close and start again: a different set.
- [✓] **Lazy generation**: scroll down: more carousels appear while there is content; when everything is used up, nothing more appears and nothing breaks.
- [✓] **Category page**: click a carousel title: the page shows "Carousel", the title in big letters, "N albums, H h M min", a round play button and ALL items as tiles (3 per row). The play button plays everything as one queue (the player header shows the carousel name; clicking it returns to this page). Clicking a tile cover plays that item; the next items follow as separate queues, then recommendations. The discography page shows "year · type" under each tile. Back arrow returns to Home with the same carousels as before.
- [✓] **Recently Played page**: click its title: all (up to 50) recently played items.
- [✓] **p.29 Drop zone**: drag files or a folder from Explorer over the middle window: the title changes to "Import", the page blurs and an import icon appears in the middle; leaving the window restores it; dropping opens a placeholder "Import" page (the real import window comes in M8).
- [✓] **Languages**: with `language=ru` (also zh_CN, ja): greeting, category titles ("1990-е", "Ваши альбомы", genre names), statistics with correct plural forms ("1 альбом, 2 альбома, 5 альбомов") and the empty hint are translated and never overflow.
- [✓] **Scrolling**: Home with the 200k library (`--preset 200k`) scrolls smoothly (my run, Release: average 6.1 ms per frame, worst 8.1 ms).

## M8 - Cards, editors, dialogs (UI doc pages 30-54; run `Semplice.exe` from `build\windows-mingw-debug`)
Use a COPY of a real music folder for the import checks; the library you test with lives in its own empty folder.
- [✓] **Add artist** (Library "+" -> Add artist): type a name; Save stays grey while the name is empty. Pick a picture (click the picture area), add tags with "+" (only the fixed list is offered). Save opens the new artist's card. The same name again shows "Unable to add... already exists" and nothing is created.
- [✓] **Edit artist** (artist card -> Edit): name, picture, tags change and are shown on the card after Save; the discography tiles are listed; the red trash button asks first (see Delete below).
- [✓] **Add album from files**: Library "+" -> Add album -> "Add music" (or drop a folder): the title, artists, type, year, tags and cover are proposed from the files; the track table lists every file with its time. Change the title, remove an artist chip with the x, add one with "+" (search; "Add new artist" opens the Add artist page and returns with the artist already in the chips). Save needs: a title, at least one artist, a valid date and at least one track.
- [✓] **Release date**: click the date, turn the three wheels (month and day have "-"). A day without a month, February 30, a year before 1744 or after next year turn the date RED and Save grey.
- [✓] **Album type**: the six tabs (Single / Album / EP / Live / Compilation / Other) switch; the chosen one is filled.
- [✓] **Edit album** (album card -> Edit): everything above works on the existing album; "Add music" appends files; existing rows cannot be dropped from the list; the trash button asks first.
- [✓] **Add playlist**: from the "+" menu, or by dropping files that are not one album (the window says "Add playlist" and uses the folder name). A track menu -> "Add as new playlist" opens it with that track as the first row. Save needs a name and at least one track. **Edit playlist**: rename, new cover, append files.
- [✓] **Edit track** (track row menu -> Edit): the title and the artists (with "+" and x) and tags. Save needs a title and at least one artist; an artist created here is HIDDEN (it shows only in the picker and on the track).
- [✓] **Dropping music**: on an open ARTIST page -> Add album with that artist already in; on an open ALBUM -> its Edit window with the new rows; on the Home page -> Add album (turns into Add playlist for loose files). Files that cannot be read are counted in red ("N files could not be read"). A file that is already in the library is marked "Already in the library".
- [✓] **Progress and Cancel**: while reading or saving a big folder the bottom of the editor shows the step and a bar with Cancel; Cancel stops cleanly and the library is unchanged (check the folder: no half-copied files).
- [✓] **Delete confirmations**: track (row menu), album / artist / playlist (trash in their Edit window). The text must say what REALLY happens: how many tracks go to the Recycle Bin, how many stay because they exist elsewhere, albums or playlists that disappear, artists that become hidden. The red check deletes, the x does nothing. After a delete the deleted page is not reachable with the back arrow, and the files are in the Windows Recycle Bin (restorable).
- [✓] **Settings** (gear in the hotbar): Language list (English / Русский / 简体中文 / 日本語) shows "Restart required" after Save and applies after a restart; Name changes the greeting on Home straight away; Theme shows "True White"; Current directory + Change switches to another folder (empty folder = new library); Rescan shows a bar and Cancel and ends with a banner; Logs folder opens in Explorer; Official site opens the browser (placeholder address); License opens the read-only dialog (Close); Save is grey until something changed; the x discards.
- [✓] **License on first launch**: with `ui/license/accepted` missing from `%APPDATA%\Semplice\settings.ini` the dialog appears at start with "Exit app" (red) and "Accept". Accept closes it and it does not come back; Exit app closes the program.
- [✓] **Languages**: with `language=ru` (also zh_CN, ja) every new window has translated texts, plural sentences in the delete cards read correctly ("1 трек", "2 трека", "5 треков"), and no button text is cut off (they grow with the text).
- [✓] **Not covered by a machine**: the real Recycle Bin, the native file / folder dialogs, drag from Explorer with a real music collection, and playing a track right after editing its tags.

## M9 - Recommender (UI doc "Recommendation system"; run `Semplice.exe` from `build\windows-mingw-debug`, use a real library with several albums and tags)
- [✓] **The stream continues**: play an album (play button on its card) with repeat OFF and let it end (or press Next on the last track): the next track starts by itself, the player header reads "Recommendations", and Queue shows the next tracks ("Up next" matches what then plays).
- [✓] **Same for an artist and a carousel**: the artist's Play (discography) and a Home carousel also continue with recommendations at the end. A PLAYLIST stops at the end (no recommendations); a single track from search plays alone.
- [✓] **Fits the music**: after a rock album, the recommendations are mostly rock and related genres, rarely the opposite (a metal album should not flow into a lullaby). Tracks by the same artist come up often. Note what feels wrong: the numbers are tunable.
- [✓] **Variety**: over 20 recommended tracks, the same album is never twice in a row and rarely more than twice within 10 tracks; tracks do not repeat until everything has been heard.
- [✓] **Small library**: with only two albums the stream keeps going (it may reuse the finished album); with a single album of one track nothing is recommended and playback simply stops.
- [✓] **Counted as played**: a recommended track you listen to for 30 s (or half) raises its play count, and it never appears in Recently Played.
- [✓] **After editing the library** (add an album while listening): the new album can be recommended later, without a restart.
- [✓] **Tuning view**: `semplice_recotool.exe <library folder> --album <id> --top 20` (from the build folder) lists the best candidates with their score parts; send me odd results and I will adjust `RecommenderConfig`.

## M10 - Localisation, polish, performance
- [✓] **Languages**: set `language=ru`, `zh_CN`, `ja` (Settings, then restart): walk through Home, an artist, an album, the editors, Settings, delete confirmations, banners. Nothing in English that should be translated (artist / album names stay as they are), nothing cut off. **Please have the drafts read by a native speaker** (`docs/TRANSLATIONS.md`).
- [✓] **Scale**: run Windows at 100 / 125 / 150 / 200 % (or start with `set QT_SCALE_FACTOR=1.5`): at the smallest window (1000 x 620) nothing overlaps or is cut off on Home, an album and the editors.
- [✓] **Keyboard**: Tab moves through the round and pill buttons with a visible dark ring; Space or Enter presses the focused button; Escape closes popups and menus; the SPEC 8.7 shortcuts (Space, Ctrl+arrows, M, Ctrl+F, Alt+arrows, F5) work.
- [✓] **Speed on your own library**: start the app and watch: the window appears at once, Home fills within a second or two; scrolling the library list and the carousels stays smooth; search shows results as you type.
- [✓] **Memory**: Task Manager, idle with your library open: note "Memory (private working set)" and tell me the number (the budget is 250 MB).
- [✓] **Thumbnails**: after browsing a lot of covers, `<library>\.library\covers` holds small PNGs and stays far below 1 GB; deleting the folder is harmless (it fills again).
- [✓] **Licences**: `THIRD_PARTY_NOTICES.md` lists everything that ships; tell me if you add anything.

## M11 - Packaging and beta (details in `docs/RELEASE_CHECKLIST.md`)
- [✓] `powershell -ExecutionPolicy Bypass -File scripts\package.ps1` runs to the end; `dist\Semplice-0.1.0\Semplice.exe` starts by double click; the file properties show "Semplice", version 0.1.0 and the star icon.
- [✓] With Inno Setup 6 installed the script also produces `dist\Semplice-0.1.0-setup.exe`; run it on a clean Windows 10 1809 and a Windows 11 machine (per-user, licence page, Start menu, first start, import, play, uninstall keeps the library).
- [✓] Play music from the deployed folder / installed copy (sound, media keys, taskbar buttons): I could only start and screenshot it.
- [✓] `docs/BETA_GUIDE.md` reads well for a non-technical tester; the log-sending steps match what you see in Settings.

## First test round - re-check (from "Need to fix.pdf")
- [✓] Tooltips of the player buttons / progress area are fully visible above everything.
- [✓] "+" in the Library title fits its row; the settings button is a gear; the volume bar is half as long.
- [✓] Carousel: the wheel scrolls the carousel only over the middle cover; elsewhere the page scrolls. Turning the carousel and resizing the window never shows blank covers.
- [✓] Library menu: "Large icons (3 per row)" and "Extra large icons (2 per row)" match the layout.
- [✓] Clicking the Add "+", the view/sort button, the language / text size buttons a second time closes their menu and it stays closed.
- [✓] Search: results appear only after typing; click outside, switch windows, minimise / restore: they do not come back; the cursor leaves the field; no "Search" text in the field.
- [✓] Album / playlist card and the album / playlist editor: track "..." -> Edit turns the row into an editor (title, artist chips with x and +, cancel, save). Saving changes the track everywhere; a typed new artist becomes a hidden artist.
- [✓] Album editor: "..." on a track offers Remove / Delete; after it the list updates and your typed title is still there. Delete (trash) is at the left of the footer. Add a second and third artist with "+": each pick adds a chip.
- [✓] Import a file whose artist tag is "A, B": two artists. Tags cannot be edited on a single track any more.
- [✓] Queue panel: the playing track stays in the vertical middle.
- [✓] Windows volume panel (install with the installer, or from the zip): app name and cover are shown.
- [✓] Settings -> Visual -> Text size: 130 % / 150 % make all text larger after Save; maximise the window: everything grows, no big empty areas, the player cover and controls stay together.
- [✓] Motion: switching pages, library tabs and opening / closing menus is animated, nothing flickers.

## Second test round - re-check (from "Need to fix pt2.pdf")
- [✓] The small round letters in artist chips (album header, editors) fit inside their circles at every text size.
- [✓] Every save button is the floppy disk (album / playlist / artist editor, Settings, track row); only the delete confirmation keeps the check mark.
- [✓] Settings -> Text size: 80 %, 90 %, 100 %, 115 %, 130 %, 150 %.
- [✓] Click the Add "+", the view / sort button, a "..." button, the language and text size buttons twice: the second click closes the menu and it stays closed (also when clicking quickly).
- [✓] Import files tagged "Artist One, Artist Two": both artists exist; in the track table and in the player each name is its own link and opens its own page.
- [✓] Volume panel: the program name is "Semplice" (start the program once, then play something). If it still says "unknown application" tell me what Windows version you have.

- [✓] **Icon and volume panel (second look)**: the window title bar, the taskbar and the exe show the round icon. Start `Semplice.exe` once from the unpacked folder (it creates `Semplice.lnk` in your Start menu), close it, start it again and play something: the volume panel shows "Semplice" and the icon. If it still says "unknown application", sign out and in once (Windows caches the names) and tell me.

## Third test round - re-check (from "Need to fix pt3.pdf")
- [✓] Artist card: click the title of any carousel (Discography, Albums, Singles, ...): a page with all of its albums as tiles.
- [✓] Edit album opens with title, artists, date, tags and tracks every time (also right after saving or deleting something else).
- [✓] Add a folder without cover files whose first track has a picture inside: the picture shows in the Add window at once and is the saved album cover (`album_cover.jpg` in the album folder).
- [✓] Add one single track: the album is named like the track.
- [✓] Date wheel: one notch of the mouse wheel moves one row.
- [✓] Artist avatars are smooth round (no pixels at the edge); artist chips of the album editor show the real avatar.
- [✓] Drag a picture onto: an open album editor (cover), an artist editor (avatar), the Home page (a new album with that cover); a folder of music plus a picture (the picture is the cover).
- [✓] Artist editor: all albums are visible (wrapping rows).
- [✓] Edit a track on an album page (title, artists): the album page stays open.
- [✓] Play an album and, while it plays: rename it in Edit and save / change its artist; delete the playing album or one of its tracks. The program does not freeze; a renamed album keeps playing from the same second; a deleted playing track jumps to the next one.
- [✓] Rename an artist to the name of another visible artist: one artist with both discographies (check the folders in Explorer: all albums under the new name). Same with the name of a hidden artist (a credit-only name).
- [✓] **Merge question**: in Edit artist type the name of another artist (any capitalisation) and save: the question appears; the x keeps the editor as it was and nothing changes; the check mark merges. Same with a hidden artist name (the text says hidden). Renaming to a name nobody has saves without a question.

- [✓] **Tags**: in Edit artist / Edit album the "+" next to the tags offers the list; picking one adds it (and removes it from the list); x removes it.
- [✓] **Icon**: perfectly round, transparent corners, recognisable at the taskbar size (title bar, taskbar, Alt+Tab, Start menu entry, exe in Explorer at several sizes).
- [✓] **No freezing**: play music and save / delete albums and tracks (also the playing one): the sound continues without stalls (a brief pause while the PLAYING file itself is rewritten or moved is normal), the buttons stay responsive. Tell me exactly when a stall happens if it still does.

- [✓] **Saving while music plays** (different album, same album, the playing track itself): the window stays responsive, the sound does not stop. After the track changes, the tags of the former file are written (check with another program if you like).
- [✓] **Renaming or deleting the album that plays**: the music pauses for about a second and continues from the same second (renamed) or jumps to the next track (deleted); the window never stops responding.
- [✓] **Edit album again after saving**: all fields filled every time.
- [✓] **Long titles** in track tables: beginning visible, end cut with "..."; hover and the text runs to show the rest.
- [✓] **Play right after saving an album**: pressing play on an album starts the music within a moment, even while a save or tag write is still running.
- [✓] **Rename / change the album that is playing** (title, year, artist): the music keeps playing without a gap or with a very short one, the window never stops responding. Deleting the playing album jumps to the next track.
- [✓] **Slow save while playing**: if a save takes longer than a moment the music pauses and continues by itself after "saved"; a quick save does not interrupt it.
- [✓] **No file-rewrite storm**: start the app on the real library, play something, watch the log (`%LOCALAPPDATA%\Semplice\logs\semplice.log`): no long run of "tags still out of sync" lines; the music does not stutter 1.5 s after a track change.
- [✓] **Pictures look as before** (artist avatars round, album covers with rounded corners, queue and track rows, search results, pickers) and an album with many tracks opens and reloads after "save" without the window stopping.
- [✓] **Start-up on a slow PC**: the window is usable within about a second (log line "perf: first frame"); Japanese / Chinese / Korean track names still show their characters.
- [✓] **No freeze on save**: play a track, edit its album (tag, year, title), save: the window answers all the time and the music does not break up. Same on the slower laptop. (Cause was a thread started by the recommender after every change; see DECISIONS 176.)
- [✓] **Carousel with an album of two artists** (Home): both names under the cover are links; each opens its own artist page.
- [✓] **Resize**: drag the window height / width up and down quickly, maximise and restore: no flickering covers; note any black strip.
- [✓] **Settings button**: on the Settings page it returns to the previous page; a changed theme / text size / name is kept without pressing Save; the Cancel button still drops changes.
- [✓] **Queue panel (right, below the player)**: wheel down = next track, wheel up = previous.
- [✓] **Queue popup (hotbar button)**: clicking its first rows many times only plays that track; it never opens an album or artist page.
- [✓] **Window header**: name centred, minimise / maximise / close work, drag moves, double-click maximises and restores, the borders resize, snapping to screen edges works, a maximised window leaves the taskbar visible, close turns red when hovered. Check in both themes.
- [✓] **Sound output**: play music, then change Windows' default output (or plug in headphones): the sound moves to it without restarting the player.
- [✓] **Dark theme**: Settings -> Theme -> Dark (and System with Windows in dark mode): everything readable (texts, icons, queue, carousels, popups, dialogs, settings, editor pages), no white boxes left.
- [✓] **pt5 quick fixes**: the progress bar keeps its length while the time runs; the hover time bubble is readable above the artist names; player title/artist are the same size as the queue rows; hovering a link underlines under the letters; buttons grow slightly on hover and have 2 px outlines; clicking the taskbar icon of the open window minimises it.
- [✓] **pt5 pages**: back/forward returns to the old scroll position; Recently Played updates after a track is played; carousels span the whole middle window (also full screen) on Home and the artist page; category titles highlight only their own text; "Add new artist" is clickable on its text and never plays a track below; scrolling the queue popup never changes the track; a new artist avatar preview is round while editing; a new album without tags gets its artist's tags; the track menu has no "Add to discography"; Settings shows "Version: 0.1.0" bottom-left.
- [✓] **Hidden artists**: Edit artist -> Status in Library -> Hide, Save: the artist leaves the Library Artists tab and the Home discography carousels, still found by the hotbar search (grey name); Library -> menu -> Filter -> Hidden lists only hidden artists; Show brings the artist back.
- [✓] **Reorder artists**: in the album / track editor drag an artist chip onto another one: the order changes; dropping elsewhere puts it back; the first chip is the primary artist.
- [✓] **Collapsible player**: the chevron at the bottom right of the player collapses it; the queue shows 2 previous, current, 2 next tracks; the hotbar has previous / play / next and the progress bar (seek works, times do not jump); the chevron in the hotbar brings the player back; nothing else moves.
- [✓] **Collapsed player (pt6 mock)**: "^" below the controls collapses it: the controls and the bar leave the player, the queue grows to five tracks, the hotbar shows the controls and the bar; "v" at the end of the title line expands it; the title's hover underline / click covers only the text; with the queue popup open, clicking a row over the player's title never opens the album; Settings -> Theme names read correctly in Russian, Chinese and Japanese.
- [✓] **pt7 hover areas**: the title under a carousel, a track title in an album card and the names in the Library list underline and click only over the letters.
- [✓] **Taskbar**: hover the taskbar icon, click Next: the preview stays open; minimise and restore from the taskbar animates like other programs; a maximised window fits the screen without a gap or overflow.
- [✓] **Artist tags**: the Add artist picture is a person; Genres / Lyric language / Mood sections in the artist and album editors (pickers show translated names); "Copy tags from artists" adds, never replaces; a new album without tags gets the artist's tags; the album card shows only genres and "date | artists"; the artist card shows no tags.
- [✓] **Combined carousels**: with enough albums that share a genre and a mood / language, Home shows e.g. "Cheerful English Rap".
- [✓] **Window placement**: Settings -> Configure: drag Library / Main window / Player column / Queue / Hotbar, the others move away while dragging; Save applies at once and is kept after a restart; with the hotbar on top the search and queue popups open downward; the player column never ends up between the other two.
- [✓] **Share**: Share button on a playlist, album and artist card opens the Share page with check boxes (all ticked); the arrow of an album shows its tracks; the button at the bottom right asks for a name and saves `Name.smpl`; cancel leaves no file.
- [✓] **Import of a .smpl**: drop the file on the window in another (or an empty) library: the albums / playlists appear, the tracks play, a banner says how many tracks were added; dropping the same file again says everything is already in the library; a damaged file (change one byte) shows an error and changes nothing.
- [✓] **Share page (design)**: from an artist, album and playlist card the Share page lists everything that goes into the file (artists, albums, tracks, tags); every card / track has a check box; the arrow on a card shows its details; Hide / Show of an artist changes its status line ("Artist" / "Hidden Artist") and is saved into the file.
- [✓] **Import page**: dropping a `.smpl` opens the same page with the content of the file; untick a track or an album, choose Hide / Show for a new artist, press the arrow: only the ticked items are added and the new artist has the chosen status; items the library already has are dimmed and skipped.
- [✓] **pt8**: the library "+" menu: clicking Artist / Album / Playlist never also changes the tab below; the "+" next to Genres / Lyric language / Mood in the editors opens the list; "Configure" has no icon; the Share / import page opens with all cards closed; covers and artist pictures show on the import page; scrolling the Share page is smooth; the release date wheels open at once and a second click on the date closes them.
- [✓] **Add album from files**: an artist named only in the files' tags appears in the "+" artist list of the album and can be added as an author; "Copy tags from artists" shows the artists' genres / language / mood at once, also for artists that came from the files; the artist chips show their pictures.
- [✓] **Pre-release**: Settings -> About has GitHub and License, "Support" has Patreon (the buttons open the pages); the title bar shows the text logo (light and dark theme); the app / taskbar / exe icon is the new round logo; Home discography carousels show artist names; tab switching in the library slides, pages slide in (back from the left), carousels open with the tiles coming out of the middle, a played album slides into Recently Played, collapsing / expanding the player and the queue is animated.
- [✓] **License**: first start shows the new text with Exit app / Accept; Settings -> License shows it read-only.
