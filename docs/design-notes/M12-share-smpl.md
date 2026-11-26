# M12 - Share (.smpl files)

Owner's request (pt7, 2026-11-06): share a playlist, an album or an artist's whole discography as one file that another user
imports without any manual work. Audio files are inside. Duplicates are skipped. The format is our own.

## The file (`*.smpl`, format version 1)
```
8 bytes   magic "SMPLFILE"
u32 LE    format version (1)
u32 LE    manifest size in bytes
32 bytes  SHA-256 of the manifest
N bytes   manifest (UTF-8 JSON)
...       blobs, back to back (audio files, pictures); the manifest says where each one is
```
Manifest: `{ app, version, created, kind: "artist"|"album"|"playlist", name, blobs: [{ id, name, size, sha256 }],
artists: [{ name, tags[], avatar: blobId|null }], albums: [{ title, type, year, month, day, tags[], cover: blobId|null,
authors: [name], tracks: [track] }], playlists: [{ name, cover, tracks: [track] }] }` and
`track = { title, artists: [{ name, role }], durationMs, extension, codec, hash (hex SHA-256 of the audio), size, blob, embeddedCover }`.
Every blob has its own SHA-256, so a damaged or cut-off file is refused before anything in the library changes. Blobs are
stored as they are (audio is already compressed), so exporting is a straight copy.

## Export (`ShareService::exportTo`)
`describe(kind, id)` returns a tree for the Share page (artist: albums -> tracks, related artists; album: tracks, authors;
playlist: tracks). The page sends back the ticked items; `exportTo` writes the file next to the chosen name, via a temporary
file that is renamed at the end (a cancelled export leaves nothing). Cancellable, reports progress.

## Import (`ShareService::importFrom`)
1. read + verify header and manifest; 2. extract the blobs into a temporary folder inside `.library` (checked hash per blob);
3. for every album of the file: if the library has an album with that title and primary artist it is **skipped**; else
   `LibraryService::createAlbum` with `TrackDraft`s (known hash -> the existing track is reused, so a track already in the
   library is never added twice); 4. playlists: `createPlaylist` (a name that exists gets " (2)"...), tracks whose hash is
   already in the library are linked, not copied; 5. artists: tags and avatar are applied only to artists that have none yet.
Everything goes through `LibraryService` (journal, sanitised names, tags written into the copies); nothing here touches
library files itself. The temporary folder is removed at the end.
Result: counts of added / skipped albums, tracks, playlists, shown as a banner.

## UI
Share button on playlist / artist / album cards opens a Share page in the middle window (`PageKind::Share`) with check boxes;
the export button asks for a file name. Importing: drop a `.smpl` on the window (the drop zone routes it) - no extra screens.

## Tests
container round trip; damaged manifest / blob / truncated file refused; export of an album and a playlist; import into an
empty library gives the same albums and tracks; importing the same file twice adds nothing; a playlist track that exists is reused.
