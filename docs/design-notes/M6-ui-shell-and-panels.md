# M6 - UI shell and panels: design note

Goal: the persistent chrome of the app (Library panel, Hotbar, Player panel, Side panel, search and queue popups, banners, shortcuts) running on REAL data and the M5 playback core. The middle pane only gets a navigation host in M6 (it shows which page was asked for: Home, Settings, artist/album/playlist card, search "Show more"); the real pages are M7 (Home) and M8 (cards, editors, settings).

## 1. Threads
| Thread | Owns | Why |
|---|---|---|
| GUI | QML, view-models, `PlaybackService` + `QtAudioBackend`, `WinSmtc`, `WinThumbBar`, a **playback database connection** (point queries: resolve one track, record a play, save the session) | Qt Multimedia, SMTC and the thumbnail toolbar need the GUI thread / its event loop; the playback queries are single-row indexed statements (< 1 ms). SQLite WAL + `busy_timeout` serialise the rare concurrent write (decision 80) |
| Engine thread | `LibrarySession` (library database connection, journaled storage, import/rescan/edit services, search index) | everything slow or file-touching (SPEC 3) |

The GUI talks to the engine thread only through `LibraryEngine` (a QObject living on the engine thread; GUI calls are queued `invokeMethod`s, answers are signals). Lists are produced on the engine thread as plain value vectors and swapped into the GUI-side models in one step.

## 2. Pure, tested core pieces (no QML)
* `LibraryQueries`: rows for the three tabs with sort/filter. `ArtistRow{id, name, coverPath}`; `AlbumRow{id, title, artists text, primaryArtistId, year, type, coverPath, updatedAt}`; `PlaylistRow{id, name, coverPath}`. Sorting: alphabetical on `name_key` / `title_key` (up = Z-A, down = A-Z), recency on `updated_at`/`created_at` (default newest first), year (albums), album type filter (Unsorted album never listed; hidden artists never listed). One SQL statement per call, tested on the 50k preset (budget: < 150 ms).
* `PlayRequestBuilder`: artist discography (albums newest -> oldest, tracks in album order, every entry tagged with its album for Recently Played, `afterEnd = Recommend`), album, playlist (`Stop`), single track from search (`Stop`), search-result artist/album/playlist, hidden artist's tracks.
* `NavigationHistory`: back / forward stack of `PageRef{kind, id, title}`; navigating from a past state drops the forward part; deduplicates the same page; `canBack/canForward`.
* `BannerQueue`: max 3, newest last, oldest removed when a fourth arrives, never expires by itself (UI doc p.27).
* `UiState` on top of `ISettings`: last tab, view mode per tab (`list | large | xlarge`) - sort state is NOT persisted.

## 3. GUI-side view-models (src/ui)
| Class | Role |
|---|---|
| `AppController` | start-up flow (open stored library, "Select folder" card states from SPEC 6.6 / UI p.3-4, error banner for non-empty folder, "locate your library" wording), owns the engine thread, creates the other VMs, forwards `libraryChanged` |
| `LibraryEngine` | engine-thread object: session, list queries, play-request building, search, delete/rescan entry points used later |
| `LibraryListModel` | `QAbstractListModel` (roles: id, title, subtitle, coverUrl, kind, year, typeText); `setRows()` swaps whole vectors; scrolling is pure QML virtualisation |
| `LibraryVm` | active tab, view mode per tab, sort per tab, album type filter, list models, "Add" submenu actions, activate/play rules |
| `PlayerVm` | state of `PlaybackService` for QML: header text and target, cover url (embedded -> album cover -> placeholder, UI p.21), title/artists with targets, position/duration, progress seek, shuffle/repeat/volume/mute, canPrevious/canNext |
| `QueueVm` | previous / current / next (side panel) and the whole current group in order (hotbar popup); click and drag-to-change |
| `SearchVm` | query text retained until exit, 5 results per category from `SearchIndex`, "Show more", opening and play rules identical to the library |
| `NavigationVm` | wraps `NavigationHistory` (Home, back, forward, shortcuts Alt+Left/Right), current page for the middle pane |
| `BannerVm` | the banner list; used by every other VM and by the `tracksSkipped` signal |
| `CoverImageProvider` | `QQuickAsyncImageProvider`: `image://cover/<relative path>?w=<px>` from the library folder, scaled with `Qt::SmoothTransformation`, in-memory LRU (about 64 MB) plus a disk cache in `.library/covers`; embedded covers through `image://cover/embedded?file=...` |

## 4. QML (src/qml)
Panels: `LibraryPanel` (tab carousel switcher, view-mode / sort menu, `+` Add submenu, list / 2-per-row / 3-per-row views, empty state), `HotbarPanel` (search field, Home, back/forward, Settings, volume slider + mute with three icon states, queue button), `PlayerPanel` (all states of UI p.19-22), `SidePanel` (previous / current / next, click or drag), popups `SearchPopup`, `QueuePopup` (mutually exclusive, close on outside click, Esc), `BannerHost` (bottom of the middle pane, newest below), `MiddleHost` (page placeholder + drop-zone overlay is M7/M8). Components added: `CoverImage` (async image + placeholder), `Avatar` (letter on dark grey), `TabSwitcher`, `PopupMenu`, `Slider`, `TrackRow`-less (rows arrive with the cards in M8). Only `Theme` tokens; every text elides with a tooltip; no fixed-width text buttons.

Keyboard (SPEC 8.7): Space, Ctrl+Left/Right, Ctrl+Up/Down (5 %), M, Ctrl+F, Alt+Left/Right - `Shortcut` items that are disabled while a text field has focus where it matters (Space, M).

## 5. Rules implemented (UI doc pages 5-22)
Artists tab: name opens the card, avatar plays the whole discography (newest -> oldest, or shuffled when shuffle is on); extra-large view has no names and no card access. Albums tab: cover plays, title opens the album, artist opens the card; extra-large: playback only. Playlists: cover plays, title opens. Hidden artists never listed. View mode persisted per tab, sort not. Hotbar: Home returns the middle pane to Home, history arrows, Settings opens the settings page, volume default 50 %, clicking the speaker mutes and restores. Queue button inactive when nothing is queued. Search: order artists, albums, playlists, tracks; categories without hits are omitted; 5 per category + "Show more" only when more exist; track plays a queue of just that track; search never starts the endless stream; queue and search popups exclusive; the search text survives closing. Side panel: prev / current / next; click a cover or drag the current area up/down to change track. Player: header = playlist name / album name / artist name + "discography" / "Recommendations"; progress bar with hover time; previous button 5-second rule via the service.

## 6. Tests and benchmark
* core unit tests: `tst_library_queries` (every sort/filter, hidden artists, Unsorted album excluded, Unicode ordering), `tst_play_request_builder`, `tst_navigation_history`, `tst_banner_queue`, `tst_ui_state`.
* `semplice_bench` gets `library-list` (rows for the 50k and 200k presets).
* QML: `semplice --grab out.png` screenshot of populated, empty and long-Russian states (checked by eye, listed in the QA checklist); a scripted scroll benchmark of the 50k list through `Semplice.exe --bench-list` reporting frame times.
* Manual checklist page by page (UI doc p.3-30) in `docs/QA_CHECKLIST.md`.

## 7. Delivery order inside M6
1. core pieces + tests; 2. engine thread, `AppController`, first-launch / empty states; 3. Library panel with real lists; 4. Player + Side panels on `PlaybackService`; 5. Hotbar, search and queue popups, banners, shortcuts; 6. image provider + thumbnail cache; 7. benchmark, screenshots, QA checklist.
