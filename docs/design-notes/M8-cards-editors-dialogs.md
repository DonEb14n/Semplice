# M8 - Cards, editors and dialogs: design note

Goal: every remaining screen of the UI document (pages 30-54) against the M4 services, looking like the Figma files in `docs/design/README.md`. M8 is big, so it is built in six slices, each compiling, tested and checked with screenshots before the next one starts.

## 1. Slices
| # | Slice | Contents |
|---|---|---|
| A | Page host + shared components | `PageHost` (routes `NavigationVm::Kind` to pages), round action buttons (cancel / save / trash / confirm), `Chip` + `ChipRow` (artists, tags), `ConfirmCard`, `PopoverMenu`, `SearchPicker` (artist / playlist / album / tag pickers), `DateWheel`, `TrackTable` |
| B | Cards | Artist card (statistics, tags, play, Edit, carousels Discography / Albums / Singles / EP's / Collaborations / Feat's, only those that exist), hidden-artist card ("Unsorted tracks", "Add to Discography"), album card (track table, tags, date, artists, play, Edit), playlist card; the type carousels open a category page (`artist:<id>:<type>`), the track row menu ("..." with Add to playlist / Add to album / Edit / Remove / Delete) |
| C | Editors | Add / Edit artist, album, playlist and the track editor, with validation per SPEC 8.3 and the date rules of UI p.40 |
| D | Deletes | confirmation cards whose text is generated from the service's `plan*` result (UI p.33, 38, 43, 47) |
| E | Import | drop and "Add" entry points, scan -> proposal -> prefilled Add Album / Add Playlist window, progress bar with Cancel, tracks added to an open album |
| F | Settings and license | Settings page (language, name, theme, directory change, rescan, logs, official site, license), license dialog, restart markers, rescan with progress |

## 2. Data flow
* **Read side**: engine slots return plain value structs (`ArtistCardData`, `AlbumCardData`, `PlaylistCardData`, `TrackRow`) built from the repositories on the engine thread; the GUI side wraps them in view-models (`ArtistCardVm`, `AlbumCardVm`, `PlaylistCardVm`) that QML binds to. A page asks for its data when it is opened and again after every `libraryChanged`.
* **Write side**: editor view-models hold a *draft* on the GUI thread. Save sends one command struct to the engine (`saveArtist`, `saveAlbum`, `savePlaylist`, `saveTrack`, `deleteEntity`, `addTrackTo...`); the engine runs the matching `LibraryService` call (journaled, cancellable) and answers with `operationFinished(token, ErrorCode, report)`. Success: banner-free, navigate to the card of what was saved; failure: the right banner (already exists, wrong values, generic "Error editing", "Error adding").
* **Confirmations** ask the engine for the dry-run `plan*` result first and build the text from it: which albums go, which tracks go to the Recycle Bin, whether the artist stays as a hidden artist, which album is re-homed.
* **Import**: `ImportService::scan` / `propose` run on the engine thread; the GUI gets a proposal summary and opens the Add window with the form prefilled (the user edits it), `Save` commits through `commitAlbum` / `commitPlaylist` with a progress callback and a stop token the GUI can set (Cancel).

## 3. Rules taken from the UI document
Names unique (artist, album per primary artist, playlist) with an informational "already exists" banner; album needs >= 1 artist, a valid date (year 1744 .. current year + 1, day needs month, invalid = red, save disabled) and >= 1 track; a track edit needs a title and >= 1 artist; a new artist created from a track editor is hidden, from an album editor visible (and a hidden artist added as album author becomes visible); tags only from the vocabulary, copied to a new artist once; "Remove" from an album only while the track exists elsewhere; "Delete" removes the track everywhere; a track's file stays where it first lived and moves to its next home when that one goes (M1 rules); album / playlist with no tracks left disappears.

## 4. Tests
Core/engine level (no QML): card data builders against a real library (counts, carousels present only when non-empty, hidden-artist card, track row menu flags), draft validation (`AlbumDraftValidator`: name, artists, tracks, date rules incl. 1744 / current year + 1, month/day combinations), delete plan texts (`DeleteSummary` for every outcome of the UI doc), the editors' command building (draft -> `AlbumDraft` / `AlbumPatch`), engine round trips on a real library folder (create artist / album / playlist from a draft, edit, delete, error banners), import proposal -> form -> commit including cancel, settings changes. QML: screenshots of every window in normal, error and Russian states.
