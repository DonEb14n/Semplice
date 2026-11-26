# M5 - Playback core: design note

Goal: correct playback logic and persistence, driven by tests with a fake audio backend (SPEC 7.1-7.5, UI doc "PLAYBACK RULES"); then the real audio backend and the Windows integration, each behind an interface. No UI yet (M6 builds the player panel on top of `PlaybackService`).

## 1. Layers
| Where | What |
|---|---|
| `core/playback` (Qt Core only) | `IAudioBackend`, `PlayQueue` (pure queue logic), `PlaybackService` (state machine), `ITrackCatalog`, `IPlaybackStore`, `IRecommendationSource`, `ISystemMediaControls`, `IThumbBar`, `SystemIntegration` (glue) |
| `platform/` | `QtAudioBackend` (the only user of `QMediaPlayer`/`QAudioOutput`; links Qt Multimedia), `WinSmtc` (System Media Transport Controls, WinRT interop), `WinThumbBar` (`ITaskbarList3`) |
| `tests/tools` | `FakeAudioBackend`, fake catalog/store/recommender, fake controls |

Single-instance activation already exists (M0). Global media keys come with SMTC (SPEC 7.7).

## 2. Model
```
QueueOrigin { kind: Album|Playlist|Discography|Carousel|Recommendations, id, title }
QueueTrack  { TrackId track; qint64 albumId }      // albumId only for Discography groups (each album is registered separately)
QueueGroup  { QueueOrigin origin; QList<QueueTrack> tracks }
PlayRequest { QList<QueueGroup> groups; int startGroup; int startIndex (-1 = first, or random when shuffle is on); AfterEnd afterEnd (Stop | Recommend) }
```
`afterEnd` is decided by the caller per UI doc: playlist window and search -> Stop; album / artist / carousel -> Recommend. Carousel item started from inside the carousel = one group per carousel item, started at the clicked one, `Recommend` after the last.

## 3. `PlayQueue` (pure, no Qt objects, fully unit-tested)
State: groups, current (group, index), per group a play order (natural, or a shuffle-bag permutation), set of tracks already played in this pass, history stack of (group, track) really played, repeat mode, shuffle flag.
* **next(natural):** repeat One + natural end -> same track. Otherwise next in the order; at the end of a group: repeat All -> the same group again (a new bag when shuffled); else the next group; after the last group: `NeedRecommendations` if `afterEnd == Recommend`, else `End`. **Manual Next on the last track** behaves the same except that repeat One does not apply (repeat All wraps inside the group).
* **previous:** `canPrevious = history not empty || a previous track/group exists in sequence || position > 5 s`. The service decides: position > 5 s -> restart; else pop the history (the real played history, also under shuffle); if the history is empty fall back to the previous track, or the last track of the previous group (carousel "go back"); disabled at the very start.
* **shuffle bag (decided):** on: the current track stays first, the rest = tracks of the group not yet played in this pass, permuted with the injected RNG; off: natural order again, the pointer set to the current track. A new group under shuffle gets a fresh bag.
* `removeTracks(ids)` (deleted/missing): removes them everywhere, keeps the pointer on the same track, drops empty groups, reports whether the current track went away.
* `toJson()/fromJson()`: groups, order per group, played sets, pointer, history, modes.

## 4. `PlaybackService` (state machine)
`Stopped | Playing | Paused`. Operations: `play(request)`, `play()`, `pause()`, `toggle()`, `next()`, `previous()`, `playAt(group, index)` (click in the queue), `seek(ms)`, `setShuffle`, `setRepeat`, `setVolume`, `setMuted`, `stop()`. Signals: `stateChanged`, `currentChanged`, `positionChanged`, `queueChanged`, `modesChanged`, `tracksSkipped(int)` (**one** banner per batch), `recentlyPlayedChanged`.
* **Loading:** `ITrackCatalog::resolve(id)` gives the absolute path (missing file = unplayable). A backend error is treated the same way. An unplayable track is removed from the queue and the next one tried; skips are counted and `tracksSkipped(n)` is emitted once when the attempt settles (also for several consecutive failures). If nothing playable remains: stop.
* **Gapless:** after loading a track the service asks the queue for the *next natural* track and calls `backend.preload(path)`. `QtAudioBackend` accepts the call and keeps the short gap (Qt has no gapless API; SPEC 7.1 allows it).
* **Statistics / Recently Played (SPEC 7.4-7.5):** a *listened-time accumulator* per started track: position updates that move forward by at most 2 s count, seeks and pauses do not. At `min(30 s, 50 % of duration)`: `play_count++`, `last_played_at` (all playback kinds); and, unless the group is Recommendations, `RecentRepo.touch(origin)` - for a Discography group the track's own album. Repeat-one loops count again. Only the *first* track listened of a group registers its origin... implemented as "the first track that reaches the threshold in this group instance" (so playing track 5 first still registers the album).
* **Recommendations:** `IRecommendationSource::pick(seed, count, alreadyQueued)` (M9 implements it; M5 ships a `NoRecommendations` so the flow is testable with a fake). When the queue ends with `Recommend`, the service builds a `Recommendations` group (seed = tracks of the finished groups) and keeps **3 tracks of look-ahead** while playing in it; fewer than 2 playable results = stop. Recommended tracks never touch Recently Played.
* **Deletions:** `onTracksRemoved(ids)` (connected to `LibraryService::libraryChanged`/delete reports) removes them from the queue; if the current one vanished: continue with the next (keeping Playing/Paused), without stats.
* **Persistence (SPEC 7.3):** `IPlaybackStore::saveSession(json)/loadSession()` (`kv('session')` through `LibraryPlaybackStore`); saved on every track change, pause/play, mode change, queue change, seek end and by `persist()` (the UI calls it every few seconds and at exit). `restore()`: loads, drops tracks that no longer exist (one `tracksSkipped`), positions the backend **paused** at the stored position. Volume and mute are written through `ISettings` (default 50 %, mute separate).

## 5. `IAudioBackend`
`load(path)`, `play()`, `pause()`, `stop()`, `seek(ms)`, `setVolume(0..1)`, `setMuted`, `positionMs()`, `durationMs()`, `preload(path)`, plus `IFileReleaser` (`releaseFile(path)` / `fileReady(path)`) for tag writes: `QtAudioBackend` closes the source of the file being written, remembers position and state, and reopens it at `fileReady`. Signals: `stateChanged`, `positionChanged(ms)` (every ~250 ms), `durationChanged`, `mediaEnded`, `errorOccurred(path, text)`. Nothing outside `QtAudioBackend` includes Qt Multimedia.

## 6. Windows integration
* `ISystemMediaControls`: `setMetadata(title, artist, album, coverBytes)`, `setState(Playing|Paused|Stopped)`, `setEnabled(canPrev, canNext, canPlay)`; signals `playRequested/pauseRequested/toggleRequested/nextRequested/previousRequested`. `WinSmtc` = `ISystemMediaTransportControlsInterop::GetForWindow` (+ display updater); needs the HWND of the main window, so it is created by the UI layer (M6) with `attach(hwnd)`.
* `IThumbBar`: three buttons (Previous / Play-Pause / Next), `setState(playing, canPrev, canNext)`, signals for clicks. `WinThumbBar` = `ITaskbarList3::ThumbBarAddButtons` after the `TaskbarButtonCreated` message; clicks arrive as `WM_COMMAND/THBN_CLICKED` through a native event filter.
* `SystemIntegration` (core, pure Qt) connects service <-> both interfaces; tested with fakes. The real classes cannot be driven by unit tests: manual checklist (audio output, SMTC flyout in the volume panel, media keys, thumbnail buttons on Windows 10 and 11).

## 7. Tests (all `core`, fake backend, injected clock/RNG)
`tst_play_queue`: next/previous/history, repeat modes, shuffle bag on/off mid-group (current stays first, played tracks not repeated, no repeats within a pass), carousel groups + go back, end rules (Stop vs Recommend), removal, JSON round trip.
`tst_playback_service`: every rule of SPEC 7.2-7.5 with `FakeAudioBackend`: play button rules, 5-second Previous, last-track Next, repeat/shuffle scopes, missing file skip with exactly one banner, deletion of current/next, 30 s / 50 % thresholds (short tracks, seeks, pauses, repeat-one), Recently Played ordering/dedup/50 cap and discography per-album registration, recommendations excluded from Recently Played but counted, look-ahead and the <2 stop rule, persistence + restore paused with missing tracks, volume/mute persistence, release/ready handshake, thumbnail/SMTC glue.
Plus `tst_library_playback_store` (real database): resolve, recordPlay, recent, session kv.
